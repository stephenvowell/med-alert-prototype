#pragma once

#include <Arduino.h>

enum class AlarmUiState {
  kIdle,
  kDebouncingLowVitals,
  kAlarmPending,
  kSendingPrimarySms,
  kWaitingFamilySms,
  kSendingFamilySms,
  kCooldown,
};

struct VitalsSnapshot {
  float heart_bpm{0};
  float breath_rpm{0};
  float distance_m{0};
  bool heart_valid{false};
  bool breath_valid{false};
  bool distance_valid{false};
  bool human_present{false};
};

void alarmInit();
void alarmSetConfig(float thr_hr, float thr_br, uint32_t debounce_ms, bool use_dist_gate,
                    float max_dist_m);
void alarmTick(const VitalsSnapshot& v, uint32_t now_ms);
void alarmCancel();
AlarmUiState alarmUiState();
const char* alarmUiStateName();

/// Seconds until primary SMS (911/test) if in alarm; 0 if N/A
uint32_t alarmSecondsToPrimary(uint32_t now_ms);
/// Seconds until family SMS after primary was sent
uint32_t alarmSecondsToFamily(uint32_t now_ms);

bool alarmNeedsPrimarySms();
bool alarmNeedsFamilySms();

void alarmMarkPrimarySmsDone(uint32_t now_ms);
void alarmMarkFamilySmsDone();

/// Last SMS bodies built by FSM for Twilio layer (NVS template merged elsewhere)
String alarmBuildPrimaryBody(const String& medical_template);
String alarmBuildFamilyBody(const String& medical_template);
