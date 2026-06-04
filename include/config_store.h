#pragma once

/**
 * All user-configurable fields from the captive portal, persisted under NVS namespace
 * "medalert" (see config_store.cpp keys).
 */

#include <Arduino.h>
#include <Preferences.h>

struct DeviceConfig {
  String wifi_ssid;
  String wifi_password;

  String twilio_account_sid;
  String twilio_auth_token;
  String twilio_from_e164;  // Twilio-owned number, E.164

  String sms_primary_e164;  // Configurable; use test numbers for demos
  String sms_family_e164;
  String medical_template;

  float thr_heart_bpm{40.0f};
  float thr_breath_rpm{6.0f};
  uint32_t debounce_ms{15000};

  bool use_distance_gate{true};
  float max_distance_m{1.5f};

  uint8_t neo_brightness{24};  // 0–255, capped for current
};

bool configLoad(DeviceConfig& out);
bool configSave(const DeviceConfig& in);
void configFactoryDefaults(DeviceConfig& c);
bool configHasWifi(const DeviceConfig& c);
