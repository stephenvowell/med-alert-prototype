#include "alarm_fsm.h"

static float g_thr_hr{40};
static float g_thr_br{6};
static uint32_t g_debounce_ms{15000};
static bool g_use_dist{true};
static float g_max_dist{1.5f};

static AlarmUiState g_state{AlarmUiState::kIdle};
static uint32_t g_bad_since_ms{0};
static uint32_t g_alarm_enter_ms{0};
static bool g_primary_sent{false};
static bool g_family_sent{false};
static uint32_t g_primary_sent_at_ms{0};
static uint32_t g_cooldown_until_ms{0};

static VitalsSnapshot g_last{};

static constexpr uint32_t kPrimaryDelayMs = 45'000;
static constexpr uint32_t kFamilyDelayMs = 60'000;
static constexpr uint32_t kCooldownMs = 30'000;

void alarmInit() {
  g_state = AlarmUiState::kIdle;
  g_bad_since_ms = 0;
  g_alarm_enter_ms = 0;
  g_primary_sent = false;
  g_family_sent = false;
  g_primary_sent_at_ms = 0;
  g_cooldown_until_ms = 0;
}

void alarmSetConfig(float thr_hr, float thr_br, uint32_t debounce_ms, bool use_dist_gate,
                    float max_dist_m) {
  g_thr_hr = thr_hr;
  g_thr_br = thr_br;
  g_debounce_ms = debounce_ms;
  g_use_dist = use_dist_gate;
  g_max_dist = max_dist_m;
}

static bool vitalsLow(const VitalsSnapshot& v) {
  if (!v.heart_valid || !v.breath_valid) {
    return false;
  }
  if (v.heart_bpm >= g_thr_hr) {
    return false;
  }
  if (v.breath_rpm >= g_thr_br) {
    return false;
  }
  if (g_use_dist) {
    if (!v.distance_valid || v.distance_m > g_max_dist) {
      return false;
    }
  }
  return true;
}

void alarmTick(const VitalsSnapshot& v, uint32_t now_ms) {
  g_last = v;

  if (g_state == AlarmUiState::kCooldown) {
    if ((int32_t)(now_ms - g_cooldown_until_ms) >= 0) {
      alarmInit();
    }
    return;
  }

  const bool low = vitalsLow(v);

  switch (g_state) {
    case AlarmUiState::kIdle:
      if (low) {
        g_bad_since_ms = now_ms;
        g_state = AlarmUiState::kDebouncingLowVitals;
      }
      break;

    case AlarmUiState::kDebouncingLowVitals:
      if (!low) {
        g_state = AlarmUiState::kIdle;
        break;
      }
      if (now_ms - g_bad_since_ms >= g_debounce_ms) {
        g_alarm_enter_ms = now_ms;
        g_primary_sent = false;
        g_family_sent = false;
        g_state = AlarmUiState::kAlarmPending;
      }
      break;

    case AlarmUiState::kAlarmPending:
      if (!low) {
        alarmInit();
        break;
      }
      break;

    case AlarmUiState::kWaitingFamilySms:
      // Stay here until family SMS fires or user cancels (alarmCancel).
      break;

    case AlarmUiState::kSendingPrimarySms:
    case AlarmUiState::kSendingFamilySms:
      // Reserved for future async dispatch; SMS currently done in main.
      break;

    default:
      break;
  }
}

void alarmCancel() { alarmInit(); }

AlarmUiState alarmUiState() { return g_state; }

const char* alarmUiStateName() {
  switch (g_state) {
    case AlarmUiState::kIdle:
      return "idle";
    case AlarmUiState::kDebouncingLowVitals:
      return "debouncing";
    case AlarmUiState::kAlarmPending:
      return "alarm_pending";
    case AlarmUiState::kSendingPrimarySms:
      return "sending_primary";
    case AlarmUiState::kWaitingFamilySms:
      return "waiting_family";
    case AlarmUiState::kSendingFamilySms:
      return "sending_family";
    case AlarmUiState::kCooldown:
      return "cooldown";
    default:
      return "unknown";
  }
}

uint32_t alarmSecondsToPrimary(uint32_t now_ms) {
  if (g_state != AlarmUiState::kAlarmPending || g_primary_sent) {
    return 0;
  }
  uint32_t elapsed = now_ms - g_alarm_enter_ms;
  if (elapsed >= kPrimaryDelayMs) {
    return 0;
  }
  return (kPrimaryDelayMs - elapsed + 999) / 1000;
}

uint32_t alarmSecondsToFamily(uint32_t now_ms) {
  if (g_state != AlarmUiState::kWaitingFamilySms || g_family_sent) {
    return 0;
  }
  uint32_t elapsed = now_ms - g_primary_sent_at_ms;
  if (elapsed >= kFamilyDelayMs) {
    return 0;
  }
  return (kFamilyDelayMs - elapsed + 999) / 1000;
}

bool alarmNeedsPrimarySms() {
  if (g_primary_sent) {
    return false;
  }
  if (g_state != AlarmUiState::kAlarmPending) {
    return false;
  }
  return (millis() - g_alarm_enter_ms) >= kPrimaryDelayMs;
}

bool alarmNeedsFamilySms() {
  if (g_family_sent) {
    return false;
  }
  if (g_state != AlarmUiState::kWaitingFamilySms) {
    return false;
  }
  return (millis() - g_primary_sent_at_ms) >= kFamilyDelayMs;
}

void alarmMarkPrimarySmsDone(uint32_t now_ms) {
  g_primary_sent = true;
  g_primary_sent_at_ms = now_ms;
  g_state = AlarmUiState::kWaitingFamilySms;
}

void alarmMarkFamilySmsDone() {
  g_family_sent = true;
  g_cooldown_until_ms = millis() + kCooldownMs;
  g_state = AlarmUiState::kCooldown;
}

String alarmBuildPrimaryBody(const String& medical_template) {
  String b = medical_template;
  b += " | HR:";
  b += String(g_last.heart_bpm, 1);
  b += " BR:";
  b += String(g_last.breath_rpm, 1);
  b += " | Automated prototype alert—verify before dispatch.";
  return b;
}

String alarmBuildFamilyBody(const String& medical_template) {
  String b = "FAMILY NOTICE: ";
  b += medical_template;
  b += " | Primary SMS was sent. Last HR:";
  b += String(g_last.heart_bpm, 1);
  b += " BR:";
  b += String(g_last.breath_rpm, 1);
  return b;
}
