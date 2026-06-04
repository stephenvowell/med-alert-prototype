/**
 * Medical alert prototype — MR60BHA2 (XIAO ESP32-C6) + NeoPixel ring.
 *
 * Runtime modes:
 *   - SoftAP + captive portal: first boot or D9 grounded at reset, or no WiFi in NVS.
 *   - STA + WebServer: normal operation; polls radar, runs alarm FSM, optional Twilio SMS.
 *
 * SMS is issued from loop() via tryDispatchSms() when the FSM asks for primary/family sends.
 * See README for regulatory limits (Twilio is not carrier 911).
 */

#include <Adafruit_NeoPixel.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <WiFi.h>

#include "Seeed_Arduino_mmWave.h"

#include "alarm_fsm.h"
#include "config_store.h"
#include "twilio_client.h"

#if __has_include("build_version.h")
#include "build_version.h"
#else
#define MEDALERT_FW_VERSION "dev"
#define MEDALERT_FW_GIT ""
#define MEDALERT_FW_BUILD_UNIX 0ULL
#endif

#if __has_include("credentials.h") && !defined(MEDALERT_SKIP_CREDENTIALS_H)
#include "credentials.h"
namespace {
void applyEmbeddedCredentialsIfEmpty(DeviceConfig& c) {
#ifdef MEDALERT_WIFI_SSID
  if (c.wifi_ssid.length() == 0) c.wifi_ssid = MEDALERT_WIFI_SSID;
#endif
#ifdef MEDALERT_WIFI_PASSWORD
  if (c.wifi_password.length() == 0) c.wifi_password = MEDALERT_WIFI_PASSWORD;
#endif
#ifdef MEDALERT_TWILIO_ACCOUNT_SID
  if (c.twilio_account_sid.length() == 0) c.twilio_account_sid = MEDALERT_TWILIO_ACCOUNT_SID;
#endif
#ifdef MEDALERT_TWILIO_AUTH_TOKEN
  if (c.twilio_auth_token.length() == 0) c.twilio_auth_token = MEDALERT_TWILIO_AUTH_TOKEN;
#endif
#ifdef MEDALERT_TWILIO_FROM_E164
  if (c.twilio_from_e164.length() == 0) c.twilio_from_e164 = MEDALERT_TWILIO_FROM_E164;
#endif
#ifdef MEDALERT_SMS_PRIMARY_E164
  if (c.sms_primary_e164.length() == 0) c.sms_primary_e164 = MEDALERT_SMS_PRIMARY_E164;
#endif
#ifdef MEDALERT_SMS_FAMILY_E164
  if (c.sms_family_e164.length() == 0) c.sms_family_e164 = MEDALERT_SMS_FAMILY_E164;
#endif
#ifdef MEDALERT_MEDICAL_TEMPLATE
  if (c.medical_template.length() == 0) c.medical_template = MEDALERT_MEDICAL_TEMPLATE;
#endif
}
}  // namespace
#endif

// ---------------------------------------------------------------------------
// Hardware (Seeed XIAO ESP32-C6 + external NeoPixel ring)
// ---------------------------------------------------------------------------
#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 10  // D10 — change if you wire DIN elsewhere
#endif
#ifndef PIN_SETUP
// Seeed XIAO ESP32-C6: silkscreen **D9** is SoC **GPIO20** (Arduino `D9`), not GPIO9 — see `pins_arduino.h`.
#define PIN_SETUP D9
#endif

// If NVS has WiFi but join never succeeds, start setup SoftAP automatically (wrong password / SSID changed).
static constexpr uint32_t kStaJoinPortalFallbackMs = 60'000;
static uint32_t g_sta_join_start_ms = 0;

static constexpr uint8_t kNeoCount = 16;
static constexpr uint32_t kNeoFlashMs = 400;

static Adafruit_NeoPixel g_strip(kNeoCount, PIN_NEOPIXEL, NEO_GRB + NEO_KHZ800);
static SEEED_MR60BHA2 g_mmWave;
// UART0 per Seeed MR60BHA2 + XIAO ESP32-C6 integration examples.
static HardwareSerial g_mmWaveSerial(0);

static WebServer g_server(80);
static DNSServer g_dns;
static DeviceConfig g_cfg{};

enum class NetMode { kNone, kPortal, kStation };
static NetMode g_net{NetMode::kNone};

static VitalsSnapshot g_vitals{};
// Shown on dashboard JSON; cleared on successful send or manual cancel.
static String g_last_sms_error;

// Red blink only while alarm is "active" before family SMS; idle/cooldown leaves ring off.
static void applyNeoFromAlarmState(uint32_t now_ms) {
  const AlarmUiState st = alarmUiState();
  const bool flashZone = (st == AlarmUiState::kAlarmPending) ||
                           (st == AlarmUiState::kWaitingFamilySms);
  if (!flashZone) {
    g_strip.clear();
    g_strip.show();
    return;
  }

  const bool on = ((now_ms / kNeoFlashMs) % 2U) == 0U;
  const uint8_t bri = g_cfg.neo_brightness > 0 ? g_cfg.neo_brightness : 24;
  if (on) {
    g_strip.setBrightness(bri);
    for (int i = 0; i < kNeoCount; ++i) {
      g_strip.setPixelColor(i, g_strip.Color(255, 0, 0));
    }
  } else {
    g_strip.setBrightness(bri);
    g_strip.clear();
  }
  g_strip.show();
}

// Polls MR60BHA2 UART stack; *_valid flags gate FSM (invalid readings are not "low vitals").
static void readVitalsFromSensor() {
  (void)g_mmWave.update(50);

  g_vitals.heart_valid = g_mmWave.getHeartRate(g_vitals.heart_bpm);
  g_vitals.breath_valid = g_mmWave.getBreathRate(g_vitals.breath_rpm);
  g_vitals.distance_valid = g_mmWave.getDistance(g_vitals.distance_m);
  g_vitals.human_present = g_mmWave.isHumanDetected();
}

// Escape minimal set for JSON string values (dashboard error text).
static void appendJsonEscaped(String& o, const String& s) {
  for (unsigned i = 0; i < s.length(); ++i) {
    const char c = s[i];
    switch (c) {
      case '"':
        o += "\\\"";
        break;
      case '\\':
        o += "\\\\";
        break;
      case '\n':
      case '\r':
        o += ' ';
        break;
      default:
        if ((unsigned char)c < 0x20) {
          o += ' ';
        } else {
          o += c;
        }
        break;
    }
  }
}

// Dashboard GET /api/status — hand-built JSON (avoids ArduinoJson + clangd template noise).
static void sendJsonStatus() {
  String out;
  out.reserve(680);
  out += '{';
  out += "\"fw_ver\":\"";
  appendJsonEscaped(out, String(MEDALERT_FW_VERSION));
  out += "\",\"fw_git\":\"";
  appendJsonEscaped(out, String(MEDALERT_FW_GIT));
  out += "\",\"fw_build\":";
  out += String(static_cast<unsigned long long>(MEDALERT_FW_BUILD_UNIX));
  out += ",\"hr\":";
  out += String(g_vitals.heart_bpm, 2);
  out += ",\"hr_ok\":";
  out += g_vitals.heart_valid ? "true" : "false";
  out += ",\"br\":";
  out += String(g_vitals.breath_rpm, 2);
  out += ",\"br_ok\":";
  out += g_vitals.breath_valid ? "true" : "false";
  out += ",\"dist\":";
  out += String(g_vitals.distance_m, 2);
  out += ",\"dist_ok\":";
  out += g_vitals.distance_valid ? "true" : "false";
  out += ",\"human\":";
  out += g_vitals.human_present ? "true" : "false";
  out += ",\"state\":\"";
  out += alarmUiStateName();
  out += "\",\"to_primary_s\":";
  out += String(static_cast<unsigned long>(alarmSecondsToPrimary(millis())));
  out += ",\"to_family_s\":";
  out += String(static_cast<unsigned long>(alarmSecondsToFamily(millis())));
  out += ",\"wifi\":\"";
  out += (WiFi.status() == WL_CONNECTED) ? "connected" : "disconnected";
  out += "\",\"last_sms_err\":\"";
  appendJsonEscaped(out, g_last_sms_error);
  out += "\",\"sms_opt_in\":";
  out += g_cfg.sms_opt_in_acknowledged ? "true" : "false";
  out += ",\"sms_opt_in_recorded_at\":\"";
  appendJsonEscaped(out, g_cfg.sms_opt_in_recorded_at);
  out += "\"}";
  g_server.send(200, "application/json", out);
}

static const char kPortalPage[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="color-scheme" content="light dark"/>
<title>MedAlert Setup</title>
<style>
  :root {
    --bg: #f4f6f8;
    --surface: #ffffff;
    --text: #0f172a;
    --muted: #475569;
    --border: #e2e8f0;
    --accent: #e3f2fd;
    --primary: #1565c0;
    --primary-press: #0d47a1;
    --danger: #b71c1c;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #121826;
      --surface: #1e293b;
      --text: #f1f5f9;
      --muted: #94a3b8;
      --border: #334155;
      --accent: #1e3a5f;
      --primary: #42a5f5;
      --primary-press: #1976d2;
      --danger: #ef5350;
    }
  }
  html { -webkit-text-size-adjust: 100%; background: var(--bg); }
  *, *::before, *::after { box-sizing: border-box; }
  body {
    font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    margin: 0;
    color: var(--text);
    padding: max(12px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right))
             max(24px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left));
    max-width: 36rem;
    margin-inline: auto;
    font-size: 1rem;
    line-height: 1.45;
    min-height: 100dvh;
    touch-action: manipulation;
  }
  h1 {
    font-size: 1.35rem;
    margin: 0 0 0.5rem;
    border-bottom: 3px solid var(--primary);
    background: linear-gradient(var(--accent), transparent);
    padding: 0.6rem 0.75rem 0.5rem;
    margin-inline: -0.25rem;
    border-radius: 10px 10px 0 0;
  }
  .note { font-size: 0.9rem; color: var(--muted); margin: 0.75rem 0 1rem; }
  .ver { font-size: 0.85rem; color: var(--muted); margin: 0 0 1rem; line-height: 1.4; word-break: break-word; }
  label { display: block; margin-top: 0.85rem; font-weight: 600; font-size: 0.9rem; color: var(--text); }
  input, textarea {
    width: 100%;
    margin-top: 0.35rem;
    padding: 0.65rem 0.75rem;
    font-size: 16px;
    min-height: 48px;
    border: 1px solid var(--border);
    border-radius: 10px;
    background: var(--surface);
    color: var(--text);
  }
  textarea { min-height: 6rem; resize: vertical; }
  input[type="checkbox"] { width: auto; min-height: auto; margin-right: 0.5rem; vertical-align: middle; accent-color: var(--primary); }
  .chk { display: flex; align-items: center; gap: 0.35rem; margin-top: 0.85rem; font-weight: 600; color: var(--text); }
  hr { border: 0; border-top: 1px solid var(--border); margin: 1.25rem 0; }
  button[type="submit"] {
    width: 100%;
    margin-top: 1.25rem;
    padding: 0.85rem 1rem;
    font-size: 1.05rem;
    min-height: 48px;
    border-radius: 9999px;
    border: 0;
    background: var(--primary);
    color: #fff;
    font-weight: 600;
    cursor: pointer;
  }
  button[type="submit"]:active { background: var(--primary-press); }
</style>
</head>
<body>
<h1>MedAlert setup</h1>
<p class="note">WiFi joins your home network. SMS uses Twilio over HTTPS. Do not use real 911 until legally cleared—use Twilio test numbers. US phone fields: enter 10 digits, then tap <strong>Next</strong> or <strong>Save</strong> — the form shows <strong>+1</strong>… automatically (same rules apply on the device if JavaScript is off).</p>
<p class="ver">%%MEDALERT_FW_LINE%%</p>
<form id="portal_form" method="POST" action="/save">
<label>WiFi SSID<input name="wifi_ssid" autocomplete="username" required/></label>
<label>WiFi password<input id="fld_wifi_pass" name="wifi_pass" type="password" autocomplete="current-password"/></label>
<hr/>
<label>Twilio Account SID<input name="tw_sid" inputmode="text" autocapitalize="none" required/></label>
<label>Twilio Auth Token<input id="fld_tw_token" name="tw_token" type="password" required/></label>
<label class="chk"><input type="checkbox" id="show_pw"/><span>Show Wi-Fi password and Twilio Auth Token</span></label>
<label>Twilio From (E.164)<input id="fld_tw_from" name="tw_from" placeholder="+15551234567" inputmode="tel" required/></label>
<label>Primary SMS (E.164 — test or monitored)<input id="fld_sms_pri" name="sms_pri" placeholder="+15005550006" inputmode="tel" required/></label>
<label>Family SMS (E.164)<input id="fld_sms_fam" name="sms_fam" inputmode="tel" required/></label>
<label class="chk"><input type="checkbox" name="sms_opt_in" value="1" required/><span>I confirm each destination number has <strong>opted in</strong> to receive SMS from this device (carrier / Twilio consent rules). This acknowledgment is stored on the device when you save.</span></label>
<input type="hidden" name="sms_opt_in_client_ts" id="sms_opt_in_client_ts" value=""/>
<label>Medical / location template<textarea name="med_tpl" rows="4">Medical alert prototype. Verify before dispatch.</textarea></label>
<hr/>
<label>Heart rate below (BPM)<input name="thr_hr" type="number" step="0.1" value="40" inputmode="decimal"/></label>
<label>Breath rate below (breaths/min)<input name="thr_br" type="number" step="0.1" value="6" inputmode="decimal"/></label>
<label>Debounce (ms)<input name="deb_ms" type="number" value="15000" inputmode="numeric"/></label>
<label>Max distance gate (m)<input name="dist_max" type="number" step="0.1" value="1.5" inputmode="decimal"/></label>
<label class="chk"><input type="checkbox" name="dist_gate" checked/><span>Use distance gate</span></label>
<label>NeoPixel brightness (0-255)<input name="neo_bri" type="number" value="24" inputmode="numeric"/></label>
<button type="submit">Save &amp; reboot</button>
</form>
<script>
(function(){
function normalizeE164(raw){
  raw=String(raw||'');
  var i,d='',s=0,c;
  for(i=0;i<raw.length;i++){
    c=raw[i];
    if(c==='+'&&d.length===0&&!s){s=1;continue;}
    if(c>='0'&&c<='9')d+=c;
  }
  if(d.length===0)return raw;
  if(s)return '+'+d;
  if(d.length===10)return '+1'+d;
  if(d.length===11&&d[0]==='1')return '+'+d;
  if(d.length>=8&&d.length<=15)return '+'+d;
  return '+'+d;
}
function wirePhone(id){
  var el=document.getElementById(id);
  if(!el)return;
  el.addEventListener('blur',function(){
    if(!this.value)return;
    this.value=normalizeE164(this.value);
  });
}
['fld_tw_from','fld_sms_pri','fld_sms_fam'].forEach(wirePhone);
var f=document.getElementById('portal_form');
if(f)f.addEventListener('submit',function(){
  var ts=document.getElementById('sms_opt_in_client_ts');
  if(ts)ts.value=new Date().toISOString();
  ['fld_tw_from','fld_sms_pri','fld_sms_fam'].forEach(function(id){
    var el=document.getElementById(id);
    if(el&&el.value)el.value=normalizeE164(el.value);
  });
});
document.getElementById('show_pw').addEventListener('change',function(){
  var t=this.checked?'text':'password';
  document.getElementById('fld_wifi_pass').type=t;
  document.getElementById('fld_tw_token').type=t;
});
})();
</script>
</body>
</html>
)HTML";

static const char kDashPage[] = R"HTML(
<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8"/>
<meta name="viewport" content="width=device-width,initial-scale=1,viewport-fit=cover"/>
<meta name="color-scheme" content="light dark"/>
<title>MedAlert</title>
<style>
  :root {
    --bg: #f4f6f8;
    --surface: #ffffff;
    --text: #0f172a;
    --muted: #475569;
    --border: #e2e8f0;
    --accent: #e3f2fd;
    --primary: #1565c0;
    --danger: #b71c1c;
    --danger-press: #7f1010;
    --warn-text: #b71c1c;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #121826;
      --surface: #1e293b;
      --text: #f1f5f9;
      --muted: #94a3b8;
      --border: #334155;
      --accent: #1e3a5f;
      --primary: #42a5f5;
      --danger: #ef5350;
      --danger-press: #e53935;
      --warn-text: #ffcdd2;
    }
  }
  html { -webkit-text-size-adjust: 100%; background: var(--bg); }
  *, *::before, *::after { box-sizing: border-box; }
  body {
    font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif;
    margin: 0;
    color: var(--text);
    padding: max(12px, env(safe-area-inset-top)) max(16px, env(safe-area-inset-right))
             max(24px, env(safe-area-inset-bottom)) max(16px, env(safe-area-inset-left));
    max-width: 40rem;
    margin-inline: auto;
    font-size: 1rem;
    line-height: 1.45;
    min-height: 100dvh;
    touch-action: manipulation;
  }
  h1 {
    font-size: 1.35rem;
    margin: 0 0 0.75rem;
    padding: 0.6rem 0.75rem 0.5rem;
    border-radius: 10px 10px 0 0;
    border-bottom: 3px solid var(--primary);
    background: linear-gradient(var(--accent), transparent);
    margin-inline: -0.25rem;
  }
  .grid {
    display: grid;
    grid-template-columns: 1fr;
    gap: 12px;
    padding-bottom: 14px;
    margin-bottom: 10px;
    border-bottom: 3px solid var(--primary);
  }
  @media (min-width: 480px) {
    .grid { grid-template-columns: 1fr 1fr; }
  }
  .card {
    border: 1px solid var(--border);
    border-radius: 12px;
    padding: 14px 14px 12px;
    min-height: 5.5rem;
    background: var(--surface);
    box-shadow: 0 1px 2px rgba(15, 23, 42, 0.06);
  }
  @media (prefers-color-scheme: dark) {
    .card { box-shadow: 0 1px 3px rgba(0, 0, 0, 0.35); }
  }
  .card > div:first-child { font-size: 0.85rem; color: var(--muted); font-weight: 600; }
  .card > div:last-child { font-size: 1.5rem; font-weight: 700; margin-top: 0.25rem; word-break: break-word; color: var(--text); }
  .meta { font-size: 0.95rem; margin: 0.35rem 0; color: var(--text); }
  .ver { font-size: 0.85rem; color: var(--muted); margin: 0.35rem 0 0.75rem; line-height: 1.4; word-break: break-word; }
  #err { font-size: 0.9rem; word-break: break-word; color: var(--warn-text); min-height: 1.2em; }
  #kill {
    width: 100%;
    max-width: 24rem;
    margin-top: 1rem;
    padding: 0.85rem 1rem;
    font-size: 1.05rem;
    min-height: 48px;
    border-radius: 12px;
    border: 0;
    background: var(--danger);
    color: #fff;
    font-weight: 600;
    cursor: pointer;
  }
  #kill:active { background: var(--danger-press); }
</style>
</head>
<body>
<h1>MedAlert</h1>
<div class="grid">
<div class="card"><div>Heart (BPM)</div><div id="hr">--</div></div>
<div class="card"><div>Breath (/min)</div><div id="br">--</div></div>
<div class="card"><div>Distance (m)</div><div id="dist">--</div></div>
<div class="card"><div>State</div><div id="st">--</div></div>
</div>
<p class="meta">Primary SMS in <span id="tp">--</span>s · Family SMS in <span id="tf">--</span>s</p>
<p class="meta">SMS consent (NVS): <span id="optin">--</span></p>
<p class="meta">WiFi: <span id="wf">--</span></p>
<p class="ver" id="fwver">%%MEDALERT_FW_LINE%%</p>
<p id="err"></p>
<button type="button" id="kill">Cancel alert / clear LEDs</button>
<script>
async function poll(){
  const r=await fetch('/api/status');const j=await r.json();
  document.getElementById('hr').textContent=(j.hr_ok?j.hr.toFixed(1):'n/a');
  document.getElementById('br').textContent=(j.br_ok?j.br.toFixed(1):'n/a');
  document.getElementById('dist').textContent=(j.dist_ok?j.dist.toFixed(2):'n/a');
  document.getElementById('st').textContent=j.state;
  document.getElementById('tp').textContent=j.to_primary_s||0;
  document.getElementById('tf').textContent=j.to_family_s||0;
  var oi=document.getElementById('optin');
  if(oi)oi.textContent=j.sms_opt_in?(j.sms_opt_in_recorded_at||'yes (no browser timestamp)'):'not recorded — SMS disabled';
  document.getElementById('wf').textContent=j.wifi;
  document.getElementById('err').textContent=j.last_sms_err||'';
  var fw=document.getElementById('fwver');
  if(fw&&j.fw_ver!=null)fw.textContent=j.fw_ver+' · git '+(j.fw_git||'')+' · build '+(j.fw_build!=null?j.fw_build:'');
}
setInterval(poll,500);poll();
document.getElementById('kill').onclick=async()=>{
  await fetch('/api/alarm/cancel',{method:'POST'});poll();
};
</script>
</body>
</html>
)HTML";

static String firmwareInfoLine() {
  return String("Firmware ") + String(MEDALERT_FW_VERSION) + " · git " + String(MEDALERT_FW_GIT) + " · build " +
         String(static_cast<unsigned long long>(MEDALERT_FW_BUILD_UNIX));
}

static void handlePortalRoot() {
  String html(kPortalPage);
  html.replace("%%MEDALERT_FW_LINE%%", firmwareInfoLine());
  g_server.send(200, "text/html", html);
}

// Captive portal: ISO-ish timestamp from the browser at submit (audit only; not notarized).
static String sanitizeSmsOptInClientTs(const String& raw) {
  String o;
  o.reserve(40);
  for (size_t i = 0; i < raw.length() && o.length() < 40; ++i) {
    const char c = raw[i];
    if ((c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || c == '-' || c == ':' ||
        c == '.' || c == '+' || c == 'T' || c == 't' || c == 'Z' || c == 'z') {
      o += c;
    }
  }
  return o;
}

// Strip spaces/dashes/parens; ensure leading + for Twilio E.164.
// 10 digits only -> +1… (NANP). 11 digits starting with 1 -> +…. Other digit runs -> +digits.
static String normalizeE164Phone(const String& raw) {
  String digits;
  bool saw_plus = false;
  for (size_t i = 0; i < raw.length(); ++i) {
    const char c = raw[i];
    if (c == '+' && digits.length() == 0 && !saw_plus) {
      saw_plus = true;
      continue;
    }
    if (c >= '0' && c <= '9') {
      digits += c;
    }
  }
  if (digits.length() == 0) {
    return raw;
  }
  if (saw_plus) {
    return String("+") + digits;
  }
  if (digits.length() == 10) {
    return String("+1") + digits;
  }
  if (digits.length() == 11 && digits[0] == '1') {
    return String("+") + digits;
  }
  if (digits.length() >= 8 && digits.length() <= 15) {
    return String("+") + digits;
  }
  return String("+") + digits;
}

// POST /save from captive portal: validate bounds, persist NVS, then reboot into STA.
static void handlePortalSave() {
  DeviceConfig c;
  c.wifi_ssid = g_server.arg("wifi_ssid");
  c.wifi_password = g_server.arg("wifi_pass");
  c.twilio_account_sid = g_server.arg("tw_sid");
  c.twilio_auth_token = g_server.arg("tw_token");
  c.twilio_from_e164 = normalizeE164Phone(g_server.arg("tw_from"));
  c.sms_primary_e164 = normalizeE164Phone(g_server.arg("sms_pri"));
  c.sms_family_e164 = normalizeE164Phone(g_server.arg("sms_fam"));
  c.medical_template = g_server.arg("med_tpl");
  if (!g_server.hasArg("sms_opt_in") || g_server.arg("sms_opt_in") != "1") {
    g_server.send(400, "text/html",
                  "<!doctype html><html><head><meta charset=\"utf-8\"/></head><body><p>Save was "
                  "<strong>not</strong> applied: you must check the <strong>SMS opt-in</strong> box "
                  "confirming recipients have agreed to receive messages.</p><p><a href=\"/\">Back to "
                  "setup</a></p></body></html>");
    return;
  }
  c.sms_opt_in_acknowledged = true;
  c.sms_opt_in_recorded_at = sanitizeSmsOptInClientTs(g_server.arg("sms_opt_in_client_ts"));
  c.thr_heart_bpm = g_server.arg("thr_hr").toFloat();
  c.thr_breath_rpm = g_server.arg("thr_br").toFloat();
  c.debounce_ms = (uint32_t)g_server.arg("deb_ms").toInt();
  c.use_distance_gate = g_server.hasArg("dist_gate");
  c.max_distance_m = g_server.arg("dist_max").toFloat();
  c.neo_brightness = (uint8_t)constrain(g_server.arg("neo_bri").toInt(), 0, 255);

  if (c.thr_heart_bpm <= 0) {
    c.thr_heart_bpm = 40;
  }
  if (c.thr_breath_rpm <= 0) {
    c.thr_breath_rpm = 6;
  }
  if (c.debounce_ms < 1000) {
    c.debounce_ms = 1000;
  }
  if (c.max_distance_m <= 0) {
    c.max_distance_m = 1.5f;
  }

  if (!configSave(c)) {
    g_server.send(500, "text/plain", "NVS save failed");
    return;
  }

  Serial.printf("[cfg] SMS opt-in saved (operator ack); client_ts=%s\n", c.sms_opt_in_recorded_at.c_str());

  g_server.send(200, "text/html", "<html><body>Saved. Rebooting…</body></html>");
  // Drain client before reset (flush() deprecated on ESP32 Arduino 3.x).
  g_server.client().clear();
  delay(300);
  ESP.restart();
}

// SoftAP + DNS hijack on 53 so phones show the setup page without typing an IP.
static void startPortal() {
  g_net = NetMode::kPortal;
  // Clean slate: leftover STA state can prevent the AP from advertising on some C6 builds.
  WiFi.disconnect(true);
  delay(50);
  WiFi.mode(WIFI_OFF);
  delay(50);
  WiFi.mode(WIFI_AP);

  const IPAddress ap_ip(192, 168, 4, 1);
  const IPAddress ap_gw(192, 168, 4, 1);
  const IPAddress ap_mask(255, 255, 255, 0);
  if (!WiFi.softAPConfig(ap_ip, ap_gw, ap_mask)) {
    Serial.println(F("[cfg] softAPConfig returned false (continuing)"));
  }

  constexpr const char* kApSsid = "MedAlert-Setup";
  constexpr const char* kApPass = "medalert1";
  const bool ap_ok = WiFi.softAP(kApSsid, kApPass, 1 /*channel*/, 0 /*ssid visible*/, 4);
  delay(200);

  const IPAddress sip = WiFi.softAPIP();
  Serial.printf("[cfg] SoftAP \"%s\" %s  AP_IP=%s  STA_count=%u\n", kApSsid, ap_ok ? "OK" : "FAILED",
                sip.toString().c_str(), (unsigned)WiFi.softAPgetStationNum());
  if (sip[0] == 0) {
    Serial.println(F("[cfg] ERROR: softAPIP is 0.0.0.0 — check USB power and WiFi stack; try full flash erase"));
  } else {
    Serial.printf("[cfg] Portal: join \"%s\" / \"%s\" then open http://%s/\n", kApSsid, kApPass, sip.toString().c_str());
  }

  g_dns.start(53, "*", sip);

  g_server.on("/", HTTP_GET, handlePortalRoot);
  g_server.on("/generate_204", HTTP_GET, handlePortalRoot);  // Android captive probe
  g_server.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);  // Apple
  g_server.on("/canonical.html", HTTP_GET, handlePortalRoot);
  g_server.on("/save", HTTP_POST, handlePortalSave);
  g_server.onNotFound(handlePortalRoot);  // always show setup form in AP mode

  g_server.begin();
}

static void fallbackStaToPortal() {
  Serial.println(F("[cfg] Saved WiFi did not connect in time — starting MedAlert-Setup portal"));
  g_sta_join_start_ms = 0;
  g_server.stop();
  g_dns.stop();
  WiFi.disconnect(true);
  delay(100);
  startPortal();
}

// Home WiFi client + LAN routes for dashboard, JSON API, and cancel.
static void startStation() {
  g_net = NetMode::kStation;
  WiFi.mode(WIFI_STA);
  Serial.printf("[cfg] STA joining saved SSID (len=%u) — dashboard will be http://<this-ip>/ when connected\n",
                (unsigned)g_cfg.wifi_ssid.length());
  WiFi.begin(g_cfg.wifi_ssid.c_str(), g_cfg.wifi_password.c_str());

  g_server.on("/", HTTP_GET, []() {
    String html(kDashPage);
    html.replace("%%MEDALERT_FW_LINE%%", firmwareInfoLine());
    g_server.send(200, "text/html", html);
  });
  g_server.on("/api/status", HTTP_GET, sendJsonStatus);
  g_server.on("/api/alarm/cancel", HTTP_POST, []() {
    alarmCancel();
    g_last_sms_error = "";
    g_server.send(200, "application/json", "{\"ok\":true}");
  });
  g_server.onNotFound([]() { g_server.send(404, "text/plain", "Not found"); });

  g_server.begin();
  g_sta_join_start_ms = millis();
}

// At most one Twilio attempt per loop pass; on failure set g_last_sms_error and backoff 30s.
static void tryDispatchSms() {
  if (WiFi.status() != WL_CONNECTED) {
    return;
  }

  if (!g_cfg.sms_opt_in_acknowledged) {
    if (alarmNeedsPrimarySms() || alarmNeedsFamilySms()) {
      g_last_sms_error =
          "SMS blocked: opt-in not recorded — open setup (D9 at boot), check SMS consent, Save";
    }
    return;
  }

  static uint32_t s_sms_backoff_until_ms = 0;
  const uint32_t now = millis();
  if ((int32_t)(now - s_sms_backoff_until_ms) < 0) {
    return;
  }

  TwilioCredentials cred{g_cfg.twilio_account_sid, g_cfg.twilio_auth_token, g_cfg.twilio_from_e164};

  if (alarmNeedsPrimarySms()) {
    String err;
    const String body = alarmBuildPrimaryBody(g_cfg.medical_template);
    if (!twilioSendSms(cred, g_cfg.sms_primary_e164, body, err)) {
      g_last_sms_error = "primary: " + err;
      s_sms_backoff_until_ms = now + 30'000;
    } else {
      g_last_sms_error = "";
      s_sms_backoff_until_ms = 0;
      alarmMarkPrimarySmsDone(now);
    }
    return;
  }

  if (alarmNeedsFamilySms()) {
    String err;
    const String body = alarmBuildFamilyBody(g_cfg.medical_template);
    if (!twilioSendSms(cred, g_cfg.sms_family_e164, body, err)) {
      g_last_sms_error = "family: " + err;
      s_sms_backoff_until_ms = now + 30'000;
    } else {
      g_last_sms_error = "";
      s_sms_backoff_until_ms = 0;
      alarmMarkFamilySmsDone();
    }
  }
}

void setup() {
  Serial.begin(115200);
#if ARDUINO_USB_CDC_ON_BOOT
  // USB-CDC: host opens the virtual COM port after enumeration — without a short wait, the first
  // boot lines are often dropped if you open Serial Monitor slightly after reset.
  const uint32_t serial_wait_until = millis() + 4000;
  while (!Serial && (int32_t)(millis() - serial_wait_until) < 0) {
    delay(20);
  }
#endif
  delay(200);
  Serial.printf("[MedAlert] %s | git %s | build %llu\n", MEDALERT_FW_VERSION, MEDALERT_FW_GIT,
                static_cast<unsigned long long>(MEDALERT_FW_BUILD_UNIX));

  pinMode(PIN_SETUP, INPUT_PULLUP);

  // NVS: WiFi, Twilio, thresholds. First boot may return defaults / empty WiFi.
  configLoad(g_cfg);
#if __has_include("credentials.h") && !defined(MEDALERT_SKIP_CREDENTIALS_H)
  applyEmbeddedCredentialsIfEmpty(g_cfg);
#endif
  alarmInit();
  alarmSetConfig(g_cfg.thr_heart_bpm, g_cfg.thr_breath_rpm, g_cfg.debounce_ms, g_cfg.use_distance_gate,
                 g_cfg.max_distance_m);

  g_strip.begin();
  g_strip.clear();
  g_strip.show();

  // D9 low at boot: always enter setup AP even if WiFi credentials exist (recovery path).
  // XIAO ESP32-C6: silkscreen **D9** = this GPIO (see README wiring table).
  const int setup_pin_read = digitalRead(PIN_SETUP);
  const bool force_portal = (setup_pin_read == LOW);

#if __has_include("credentials.h") && !defined(MEDALERT_SKIP_CREDENTIALS_H)
  Serial.println(
      F("[cfg] credentials.h is compiled in — MEDALERT_WIFI_* can fill empty NVS and skip SoftAP unless D9→GND at boot"));
#endif
  Serial.printf("[cfg] Setup pin silkscreen D9 (SoC GPIO%u) reads %s — force_portal=%s\n",
                static_cast<unsigned>(PIN_SETUP), setup_pin_read == LOW ? "LOW" : "HIGH",
                force_portal ? "yes (AP mode)" : "no");
  Serial.printf("[cfg] saved WiFi SSID in NVS/config: %s (len=%u)\n", configHasWifi(g_cfg) ? "non-empty" : "empty",
                (unsigned)g_cfg.wifi_ssid.length());

  g_mmWave.begin(&g_mmWaveSerial, 115200);

  if (!configHasWifi(g_cfg) || force_portal) {
    Serial.println(F("[cfg] Starting captive portal (SoftAP)"));
    startPortal();
  } else {
    Serial.println(F("[cfg] Connecting STA (no SoftAP yet — hold D9→GND & reset to force portal, or wait 60s if join fails)"));
    startStation();
  }
}

void loop() {
  const uint32_t now = millis();

  // SoftAP portal mode was otherwise silent in loop(); heartbeat helps if the host missed boot logs or the wrong COM was fixed mid-session.
  static uint32_t s_last_serial_hb = 0;
  if (now - s_last_serial_hb >= 20'000) {
    s_last_serial_hb = now;
    const char* mode =
        (g_net == NetMode::kPortal) ? "portal" : (g_net == NetMode::kStation) ? "sta" : "none";
    Serial.printf("[hb] net=%s uptime_ms=%lu\n", mode, static_cast<unsigned long>(now));
  }

  // Order: DNS (portal) → HTTP → vitals → FSM → SMS (STA) → LEDs.
  if (g_net == NetMode::kPortal) {
    g_dns.processNextRequest();
  }

  g_server.handleClient();

  if (g_net == NetMode::kStation) {
    static uint32_t s_last_wifi_log = 0;
    static bool s_logged_sta_ip = false;
    static bool s_sta_ever_connected = false;
    if (WiFi.status() == WL_CONNECTED) {
      s_sta_ever_connected = true;
      if (!s_logged_sta_ip) {
        s_logged_sta_ip = true;
        Serial.printf("[wifi] connected  IP=%s  dashboard: http://%s/\n", WiFi.localIP().toString().c_str(),
                      WiFi.localIP().toString().c_str());
      }
    } else {
      s_logged_sta_ip = false;
      if (!s_sta_ever_connected && g_sta_join_start_ms != 0 &&
          (now - g_sta_join_start_ms) > kStaJoinPortalFallbackMs) {
        fallbackStaToPortal();
      } else if (now - s_last_wifi_log > 5000) {
        s_last_wifi_log = now;
        Serial.println(F("[wifi] disconnected (wrong password / 2.4 GHz only / out of range?)"));
      }
    }
  }

  readVitalsFromSensor();
  alarmTick(g_vitals, now);

  // Twilio only in STA mode (needs routed internet).
  if (g_net == NetMode::kStation) {
    tryDispatchSms();
  }

  applyNeoFromAlarmState(now);
}
