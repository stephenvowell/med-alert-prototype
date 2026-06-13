/**
 * Matter bench spike — exposes MR60BHA2 person-present as a standard Matter
 * occupancy sensor (see docs/ALEXA-FEATURES.md §5, step 1).
 *
 * Build/flash:  pio run -e matter_spike -t upload
 * Pairing:      open serial monitor; on first boot it prints a manual pairing
 *               code + QR URL. In the Alexa app: Devices > + > Add Device >
 *               Matter, then enter the code. Then create a Routine:
 *               "When occupancy detected/cleared -> Announce".
 * Re-pair:      hold BOOT ~5 s to decommission and start fresh.
 *
 * Wiring is identical to the main firmware (radar on UART0, USB CDC serial).
 * WiFi comes from include/credentials.h (MEDALERT_WIFI_*).
 */

#include <Arduino.h>
#include <Matter.h>
#include <WiFi.h>

#include "Seeed_Arduino_mmWave.h"
#include "credentials.h"

#if !defined(MEDALERT_WIFI_SSID) || !defined(MEDALERT_WIFI_PASSWORD)
#error "matter_spike needs MEDALERT_WIFI_SSID / MEDALERT_WIFI_PASSWORD in include/credentials.h"
#endif

static MatterOccupancySensor g_occupancy;
static SEEED_MR60BHA2 g_radar;
// UART0, same as main firmware (Serial is native USB CDC on the C6).
static HardwareSerial g_radarSerial(0);

// XIAO ESP32-C6 BOOT button (GPIO9): hold ~5 s to wipe Matter commissioning.
static const uint8_t kPinBoot = 9;

void setup() {
  Serial.begin(115200);
  delay(2000);  // give USB CDC a moment so early prints are visible

  pinMode(kPinBoot, INPUT_PULLUP);

  Serial.printf("matter_spike: connecting to WiFi '%s'", MEDALERT_WIFI_SSID);
  WiFi.begin(MEDALERT_WIFI_SSID, MEDALERT_WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(250);
    Serial.print('.');
  }
  Serial.printf("\nWiFi up, IP %s\n", WiFi.localIP().toString().c_str());

  g_radar.begin(&g_radarSerial, 115200);

  g_occupancy.begin();
  Matter.begin();

  if (Matter.isDeviceCommissioned()) {
    Serial.println("Matter: already commissioned (paired).");
  } else {
    Serial.println("Matter: NOT commissioned. Pair via Alexa app > Add Device > Matter:");
    Serial.printf("  Manual pairing code: %s\n", Matter.getManualPairingCode().c_str());
    Serial.printf("  QR code URL:         %s\n", Matter.getOnboardingQRCodeUrl().c_str());
  }
}

void loop() {
  (void)g_radar.update(50);
  const bool present = g_radar.isHumanDetected();

  // Push on change; re-log every 5 s so the serial monitor shows liveness.
  static bool s_last_present = false;
  static bool s_first = true;
  static uint32_t s_last_log_ms = 0;
  if (present != s_last_present || s_first) {
    g_occupancy.setOccupancy(present);
    s_last_present = present;
    s_first = false;
    Serial.printf("occupancy -> %s\n", present ? "OCCUPIED" : "clear");
  }
  if (millis() - s_last_log_ms > 5000) {
    s_last_log_ms = millis();
    Serial.printf("status: occupancy=%s commissioned=%s\n", present ? "yes" : "no",
                  Matter.isDeviceCommissioned() ? "yes" : "no");
  }

  // Hold BOOT ~5 s to decommission (forget pairing) and re-advertise.
  static uint32_t s_boot_low_since = 0;
  if (digitalRead(kPinBoot) == LOW) {
    if (s_boot_low_since == 0) s_boot_low_since = millis();
    if (millis() - s_boot_low_since > 5000) {
      Serial.println("BOOT held 5 s — decommissioning Matter (device will reboot)...");
      Matter.decommission();
      s_boot_low_since = 0;
    }
  } else {
    s_boot_low_since = 0;
  }
}
