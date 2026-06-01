#include "config_store.h"

static const char kNs[] = "medalert";

static String readStr(Preferences& p, const char* key, const String& def = "") {
  if (!p.isKey(key)) {
    return def;
  }
  return p.getString(key, def);
}

void configFactoryDefaults(DeviceConfig& c) {
  c = DeviceConfig{};
}

bool configHasWifi(const DeviceConfig& c) { return c.wifi_ssid.length() > 0; }

bool configLoad(DeviceConfig& out) {
  Preferences p;
  if (!p.begin(kNs, true)) {
    configFactoryDefaults(out);
    return false;
  }

  out.wifi_ssid = readStr(p, "wifi_ssid");
  out.wifi_password = readStr(p, "wifi_pass");

  out.twilio_account_sid = readStr(p, "tw_sid");
  out.twilio_auth_token = readStr(p, "tw_token");
  out.twilio_from_e164 = readStr(p, "tw_from");

  out.sms_primary_e164 = readStr(p, "sms_pri", "+15005550006");  // Twilio magic test
  out.sms_family_e164 = readStr(p, "sms_fam");
  out.medical_template = readStr(p, "med_tpl", "Automated medical alert prototype.");

  out.thr_heart_bpm = p.getFloat("thr_hr", 40.0f);
  out.thr_breath_rpm = p.getFloat("thr_br", 6.0f);
  out.debounce_ms = p.getUInt("deb_ms", 15000);

  out.use_distance_gate = p.getBool("dist_gate", true);
  out.max_distance_m = p.getFloat("dist_max", 1.5f);

  out.neo_brightness = (uint8_t)p.getUChar("neo_bri", 24);

  p.end();
  return true;
}

bool configSave(const DeviceConfig& in) {
  Preferences p;
  if (!p.begin(kNs, false)) {
    return false;
  }

  p.putString("wifi_ssid", in.wifi_ssid);
  p.putString("wifi_pass", in.wifi_password);

  p.putString("tw_sid", in.twilio_account_sid);
  p.putString("tw_token", in.twilio_auth_token);
  p.putString("tw_from", in.twilio_from_e164);

  p.putString("sms_pri", in.sms_primary_e164);
  p.putString("sms_fam", in.sms_family_e164);
  p.putString("med_tpl", in.medical_template);

  p.putFloat("thr_hr", in.thr_heart_bpm);
  p.putFloat("thr_br", in.thr_breath_rpm);
  p.putUInt("deb_ms", in.debounce_ms);

  p.putBool("dist_gate", in.use_distance_gate);
  p.putFloat("dist_max", in.max_distance_m);

  p.putUChar("neo_bri", in.neo_brightness);

  p.end();
  return true;
}
