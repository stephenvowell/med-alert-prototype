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

// ---------------------------------------------------------------------------
// Hardware (Seeed XIAO ESP32-C6 + external NeoPixel ring)
// ---------------------------------------------------------------------------
#ifndef PIN_NEOPIXEL
#define PIN_NEOPIXEL 10  // D10 — change if you wire DIN elsewhere
#endif
#ifndef PIN_SETUP
#define PIN_SETUP 9  // Tie to GND at boot to force captive portal
#endif

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
  out.reserve(400);
  out += '{';
  out += "\"hr\":";
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
  out += "\"}";
  g_server.send(200, "application/json", out);
}

static const char kPortalPage[] = R"HTML(
<!doctype html><html><head><meta charset="utf-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>MedAlert Setup</title><style>body{font-family:system-ui,sans-serif;margin:16px;max-width:520px}
label{display:block;margin-top:10px;font-weight:600}input,textarea{width:100%;box-sizing:border-box;padding:8px}
button{margin-top:16px;padding:10px 16px}.note{font-size:12px;color:#444;margin-top:8px}</style></head><body>
<h1>MedAlert setup</h1><p class="note">WiFi joins your home network. SMS uses Twilio over HTTPS. Do not use real 911 until legally cleared—use Twilio test numbers.</p>
<form method="POST" action="/save">
<label>WiFi SSID<input name="wifi_ssid" required/></label>
<label>WiFi password<input name="wifi_pass" type="password"/></label>
<hr/><label>Twilio Account SID<input name="tw_sid" required/></label>
<label>Twilio Auth Token<input name="tw_token" type="password" required/></label>
<label>Twilio From (E.164)<input name="tw_from" placeholder="+15551234567" required/></label>
<label>Primary SMS (E.164 — test or monitored)<input name="sms_pri" placeholder="+15005550006" required/></label>
<label>Family SMS (E.164)<input name="sms_fam" required/></label>
<label>Medical / location template<textarea name="med_tpl" rows="4">Medical alert prototype. Verify before dispatch.</textarea></label>
<hr/><label>Heart rate below (BPM)<input name="thr_hr" type="number" step="0.1" value="40"/></label>
<label>Breath rate below (breaths/min)<input name="thr_br" type="number" step="0.1" value="6"/></label>
<label>Debounce (ms)<input name="deb_ms" type="number" value="15000"/></label>
<label>Max distance gate (m)<input name="dist_max" type="number" step="0.1" value="1.5"/></label>
<label><input type="checkbox" name="dist_gate" checked/> Use distance gate</label>
<label>NeoPixel brightness (0-255)<input name="neo_bri" type="number" value="24"/></label>
<button type="submit">Save &amp; reboot</button></form></body></html>
)HTML";

static const char kDashPage[] = R"HTML(
<!doctype html><html><head><meta charset="utf-8"/><meta name="viewport" content="width=device-width,initial-scale=1"/>
<title>MedAlert</title><style>body{font-family:system-ui,sans-serif;margin:16px}
.grid{display:grid;grid-template-columns:1fr 1fr;gap:12px;max-width:560px}.card{border:1px solid #ccc;border-radius:8px;padding:12px}
button{padding:10px 16px;margin-top:12px}</style></head><body>
<h1>MedAlert</h1><div class="grid"><div class="card"><div>Heart (BPM)</div><div id="hr">--</div></div>
<div class="card"><div>Breath (/min)</div><div id="br">--</div></div>
<div class="card"><div>Distance (m)</div><div id="dist">--</div></div>
<div class="card"><div>State</div><div id="st">--</div></div></div>
<p>Primary SMS in <span id="tp">--</span>s · Family SMS in <span id="tf">--</span>s</p>
<p>WiFi: <span id="wf">--</span></p>
<p style="color:#a00" id="err"></p>
<button id="kill">Cancel alert / clear LEDs</button>
<script>
async function poll(){
  const r=await fetch('/api/status');const j=await r.json();
  document.getElementById('hr').textContent=(j.hr_ok?j.hr.toFixed(1):'n/a');
  document.getElementById('br').textContent=(j.br_ok?j.br.toFixed(1):'n/a');
  document.getElementById('dist').textContent=(j.dist_ok?j.dist.toFixed(2):'n/a');
  document.getElementById('st').textContent=j.state;
  document.getElementById('tp').textContent=j.to_primary_s||0;
  document.getElementById('tf').textContent=j.to_family_s||0;
  document.getElementById('wf').textContent=j.wifi;
  document.getElementById('err').textContent=j.last_sms_err||'';
}
setInterval(poll,500);poll();
document.getElementById('kill').onclick=async()=>{
  await fetch('/api/alarm/cancel',{method:'POST'});poll();
};
</script></body></html>
)HTML";

static void handlePortalRoot() { g_server.send(200, "text/html", kPortalPage); }

// POST /save from captive portal: validate bounds, persist NVS, then reboot into STA.
static void handlePortalSave() {
  DeviceConfig c;
  c.wifi_ssid = g_server.arg("wifi_ssid");
  c.wifi_password = g_server.arg("wifi_pass");
  c.twilio_account_sid = g_server.arg("tw_sid");
  c.twilio_auth_token = g_server.arg("tw_token");
  c.twilio_from_e164 = g_server.arg("tw_from");
  c.sms_primary_e164 = g_server.arg("sms_pri");
  c.sms_family_e164 = g_server.arg("sms_fam");
  c.medical_template = g_server.arg("med_tpl");
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

  g_server.send(200, "text/html", "<html><body>Saved. Rebooting…</body></html>");
  // Drain client before reset (flush() deprecated on ESP32 Arduino 3.x).
  g_server.client().clear();
  delay(300);
  ESP.restart();
}

// SoftAP + DNS hijack on 53 so phones show the setup page without typing an IP.
static void startPortal() {
  g_net = NetMode::kPortal;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("MedAlert-Setup", "medalert1");
  delay(200);
  g_dns.start(53, "*", WiFi.softAPIP());

  g_server.on("/", HTTP_GET, handlePortalRoot);
  g_server.on("/generate_204", HTTP_GET, handlePortalRoot);  // Android captive probe
  g_server.on("/hotspot-detect.html", HTTP_GET, handlePortalRoot);  // Apple
  g_server.on("/canonical.html", HTTP_GET, handlePortalRoot);
  g_server.on("/save", HTTP_POST, handlePortalSave);
  g_server.onNotFound(handlePortalRoot);  // always show setup form in AP mode

  g_server.begin();
}

// Home WiFi client + LAN routes for dashboard, JSON API, and cancel.
static void startStation() {
  g_net = NetMode::kStation;
  WiFi.mode(WIFI_STA);
  WiFi.begin(g_cfg.wifi_ssid.c_str(), g_cfg.wifi_password.c_str());

  g_server.on("/", HTTP_GET, []() { g_server.send(200, "text/html", kDashPage); });
  g_server.on("/api/status", HTTP_GET, sendJsonStatus);
  g_server.on("/api/alarm/cancel", HTTP_POST, []() {
    alarmCancel();
    g_last_sms_error = "";
    g_server.send(200, "application/json", "{\"ok\":true}");
  });
  g_server.onNotFound([]() { g_server.send(404, "text/plain", "Not found"); });

  g_server.begin();
}

// At most one Twilio attempt per loop pass; on failure set g_last_sms_error and backoff 30s.
static void tryDispatchSms() {
  if (WiFi.status() != WL_CONNECTED) {
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
  delay(200);

  pinMode(PIN_SETUP, INPUT_PULLUP);

  // NVS: WiFi, Twilio, thresholds. First boot may return defaults / empty WiFi.
  configLoad(g_cfg);
  alarmInit();
  alarmSetConfig(g_cfg.thr_heart_bpm, g_cfg.thr_breath_rpm, g_cfg.debounce_ms, g_cfg.use_distance_gate,
                 g_cfg.max_distance_m);

  g_strip.begin();
  g_strip.clear();
  g_strip.show();

  // D9 low at boot: always enter setup AP even if WiFi credentials exist (recovery path).
  const bool force_portal = (digitalRead(PIN_SETUP) == LOW);

  g_mmWave.begin(&g_mmWaveSerial, 115200);

  if (!configHasWifi(g_cfg) || force_portal) {
    Serial.println(F("[cfg] Starting captive portal (SoftAP)"));
    startPortal();
  } else {
    Serial.println(F("[cfg] Connecting STA…"));
    startStation();
  }
}

void loop() {
  const uint32_t now = millis();

  // Order: DNS (portal) → HTTP → vitals → FSM → SMS (STA) → LEDs.
  if (g_net == NetMode::kPortal) {
    g_dns.processNextRequest();
  }

  g_server.handleClient();

  if (g_net == NetMode::kStation) {
    static uint32_t s_last_wifi_log = 0;
    if (WiFi.status() != WL_CONNECTED) {
      if (now - s_last_wifi_log > 5000) {
        s_last_wifi_log = now;
        Serial.println(F("[wifi] disconnected"));
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
