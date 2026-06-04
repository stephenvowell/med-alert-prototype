#pragma once

/**
 * Optional compile-time Wi‑Fi + Twilio defaults for first boot / dev.
 *
 * Setup:
 *   1. Copy this file to `include/credentials.h` (same folder).
 *   2. Uncomment and fill the `#define` lines you need.
 *   3. Build and flash. `credentials.h` is listed in `.gitignore` — do not commit it.
 *
 * To **disable** `credentials.h` without deleting the file, add to `platformio.ini` under
 * `build_flags`: `-DMEDALERT_SKIP_CREDENTIALS_H=1` (this repo may ship with that set for
 * portal-first bring-up). Remove that flag to compile credentials back in.
 *
 * Behavior: after NVS `configLoad`, each field below is set **only if** that field is
 * still empty in NVS. Saving the captive portal overwrites NVS; embedded values are then
 * ignored for those keys on later boots.
 */

// Wi‑Fi (optional — skip typing SSID/password in the portal on a fresh chip)
// #define MEDALERT_WIFI_SSID "YourWiFiSSID"
// #define MEDALERT_WIFI_PASSWORD "YourWiFiPassword"

// Twilio REST (same strings as the setup form)
// #define MEDALERT_TWILIO_ACCOUNT_SID "ACxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx"
// #define MEDALERT_TWILIO_AUTH_TOKEN "your_auth_token"
// #define MEDALERT_TWILIO_FROM_E164 "+15551234567"

// SMS destinations (E.164); use numbers you control for testing
// #define MEDALERT_SMS_PRIMARY_E164 "+15559876543"
// #define MEDALERT_SMS_FAMILY_E164 "+15551112222"

// One-line body prefix is easiest in a macro; use the portal for long templates
// #define MEDALERT_MEDICAL_TEMPLATE "Medical alert — please check on resident."
