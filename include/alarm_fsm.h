#pragma once

/**
 * Alarm FSM types and API. VitalsSnapshot is filled by main from MR60BHA2; alarmTick()
 * advances state once per loop. SMS side effects live in main (HTTP).
 */

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

/// Seconds until primary SMS if in alarm_pending; 0 if not applicable or timer elapsed.
uint32_t alarmSecondsToPrimary(uint32_t now_ms);
/// Seconds until family SMS after primary was sent; 0 if N/A.
uint32_t alarmSecondsToFamily(uint32_t now_ms);

bool alarmNeedsPrimarySms();
bool alarmNeedsFamilySms();

void alarmMarkPrimarySmsDone(uint32_t now_ms);
void alarmMarkFamilySmsDone();

/// Last vitals snapshot merged into SMS body templates.
String alarmBuildPrimaryBody(const String& medical_template);
String alarmBuildFamilyBody(const String& medical_template);
