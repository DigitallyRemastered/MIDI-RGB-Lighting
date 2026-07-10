/* lights_ota_bootstrap - minimal ArduinoOTA "provisioning" firmware.
 *
 * Purpose: flash this tiny sketch to a fresh ESP32-C3 over USB (compiles and
 * uploads in seconds), then push the real lights_esp32 firmware to the whole
 * fleet wirelessly with Release/ota_upload.py.  Once a board runs this it is
 * reachable over the network and never needs the USB cable again.
 *
 * IMPORTANT - partition table:
 *   This sketch ships the SAME min_spiffs partitions.csv as lights_esp32
 *   (app0/app1 = 1.875 MB each, dual-OTA).  The partition table is written
 *   during THIS USB flash and is NOT changed by later OTA updates, so it must
 *   already be big enough to hold the real ~1.3 MB firmware.  If you flash this
 *   with the default 1.25 MB partition instead, the OTA push of lights_esp32
 *   will fail with "Not Enough Space".  The bundled partitions.csv makes the
 *   Arduino IDE (and arduino-cli) use the right table automatically.
 *
 * Build (IDE):  select the ESP32-C3 board, then Upload. The sketch-local
 *               partitions.csv overrides the Partition Scheme menu.
 * Build (CLI):  arduino-cli compile --fqbn esp32:esp32:esp32c3 \
 *                 --libraries "C:\Users\<you>\Documents\Arduino\libraries" \
 *                 Source/lights_ota_bootstrap
 */

#include <WiFi.h>
#include <ArduinoOTA.h>
#include "secrets.h"   // const char* ssid; const char* password;  (do not commit)

char hostname[32];

void setup() {
  Serial.begin(115200);

  // Unique per-board name from the last two eFuse-MAC octets, matching the
  // Lights-XXXX default in lights_esp32 so the board is addressable (over mDNS
  // as Lights-XXXX.local) both before and after the real firmware is pushed.
  snprintf(hostname, sizeof(hostname), "Lights-%04X",
           (uint16_t)(ESP.getEfuseMac() >> 32));

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print('.'); }
  WiFi.setSleep(false);   // steady OTA throughput; no modem-sleep stalls

  ArduinoOTA.setHostname(hostname);
  ArduinoOTA.setPort(3232);
  ArduinoOTA.begin();

  Serial.printf("\n%s ready for OTA at %s\n",
                hostname, WiFi.localIP().toString().c_str());
}

void loop() {
  ArduinoOTA.handle();
}
