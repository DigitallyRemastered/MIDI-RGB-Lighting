/* lights_esp32 - Arduino Firmware for RGB Lighting (ESP32 Dual-Mode MIDI Version)
 *
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 *
 * Connections (both compiled in; choose at runtime):
 *   - rtpMIDI (AppleMIDI) over WiFi  — for PC/Mac/iOS on the same network
 *   - BLE-MIDI (NimBLE)              — for Android (and iOS as fallback)
 * BLE advertising is paused while an rtpMIDI session is active so the C3's single
 * 2.4GHz radio isn't time-sliced between BLE and WiFi (which drops rtpMIDI packets).
 *
 * Required libraries (Arduino Library Manager):
 *   - AppleMIDI  (lathoub/Arduino-AppleMIDI-Library)
 *   - ESP32-BLE-MIDI  (max22-/ESP32-BLE-MIDI)
 *   - NimBLE-Arduino  (h2zero/NimBLE-Arduino)   <-- USE_NIMBLE saves ~35KB RAM
 *   - FastLED
 *   - LightEngine (local)
 */

#include <WiFi.h>
#define USE_EXT_CALLBACKS   // enables AppleMIDI exception + RTP callbacks
#include <AppleMIDI.h>
#define USE_NIMBLE          // Use NimBLE stack (~35KB) instead of BlueDroid (~70KB)
#include <BLEMidi.h>        // ESP32-BLE-MIDI library
#include <FastLED.h>
#include <LightEngine.h>  // Library include
#include <Preferences.h>
#include "secrets.h"  // WiFi credentials — do not commit this file
#include <ArduinoOTA.h>

#define NUM_LEDS 100       // Default LED count when no saved state exists.  The active
                           // count is runtime-changeable (SysEx 0x09 / state sync) and
                           // persists in the saved engine state.
#define DATA_PIN 2  // GPIO 2 (safer than GPIO 0 on ESP32)

// Device name shown in BLE scan lists and used as the ArduinoOTA/mDNS hostname.
// This is NO LONGER a compile-time constant: it is loaded from NVS at boot by
// loadDeviceName() so one firmware binary can be flashed to every board.  Boards
// with no stored name fall back to "Lights-XXXX" (last two MAC octets) so each
// unprovisioned board is still uniquely addressable.  Set it at runtime via
// SysEx 0x0F from the plugin (which then reboots the board into the new name).
#define DEFAULT_NAME_PREFIX "Lights"
char deviceName[32] = DEFAULT_NAME_PREFIX;  // populated by loadDeviceName() in setup()

// ============================================================================
// Hardware Setup
// ============================================================================

// FastLED buffer (CRGB is an RGB struct) — allocated for maximum possible LED count
CRGB leds[LightEngine::MAX_LEDS];

// FastLED controller handle + currently registered LED count.  setLeds() on the
// controller re-registers the strip when the active count changes at runtime.
CLEDController* ledController = nullptr;
int currentLedCount = NUM_LEDS;

// Light engine instance (manages all state and logic)
LightEngine engine(NUM_LEDS);

// AppleMIDI instance (rtpMIDI over WiFi).
// Custom settings: enlarge the receive buffer (DefaultSettings::MaxBufferSize is
// 256) so a burst of preset-load SysEx can't overrun the parser while the CPU is
// stalled inside FastLED.show().  This grows the AppleMIDI byte deques to 1024
// each (a few KB of RAM — negligible against the C3's ~320 KB, and RAM is only
// ~18% used).  This is exactly what APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE()
// expands to, but with our Settings type substituted for DefaultSettings.
struct LightsAppleMIDISettings : public APPLEMIDI_NAMESPACE::DefaultSettings {
  static const size_t MaxBufferSize = 1024;
};
APPLEMIDI_NAMESPACE::AppleMIDISession<WiFiUDP, LightsAppleMIDISettings>
    AppleMIDI("AppleMIDI-ESP32", DEFAULT_CONTROL_PORT);
MIDI_NAMESPACE::MidiInterface<APPLEMIDI_NAMESPACE::AppleMIDISession<WiFiUDP, LightsAppleMIDISettings>,
                              APPLEMIDI_NAMESPACE::AppleMIDISettings>
    MIDI((APPLEMIDI_NAMESPACE::AppleMIDISession<WiFiUDP, LightsAppleMIDISettings> &) AppleMIDI);

// BLE-MIDI connection state (updated from BLE callbacks)
volatile bool bleConnected = false;

// Timer for 30Hz refresh rate
unsigned long lastRender = 0;
const unsigned long renderInterval = 33333; // microseconds (30Hz)

// Stability monitoring
unsigned long lastDiagMillis = 0;
const unsigned long DIAG_INTERVAL_MS = 5000;
unsigned long maxRenderJitter = 0;   // worst-case deviation from 33333µs
unsigned long renderCount = 0;

// Drop / exception tracking
volatile uint32_t rtpDropsSend = 0;
volatile uint32_t rtpDropsRecv = 0;
volatile uint32_t rtpExceptions = 0;
uint16_t lastRtpSeq = 0;
bool rtpSeqInit = false;
volatile uint32_t rtpSeqGaps = 0;

// NOTE: SysEx 0x0A full-state sync reassembly now lives inside LightEngine
// (shared by Teensy, ESP32, and the plugin) — this sketch just forwards SysEx.

// Flag set by BLE connect callback so loop() sends CC 119 on the next iteration
// (avoids calling BLE TX from inside the NimBLE callback thread).
static volatile bool requestSyncViaBLE = false;

// rtpMIDI session state — true while a remote rtpMIDI peer (e.g. FL Studio) is
// connected.  Used to pause BLE advertising: the ESP32-C3 shares ONE 2.4GHz radio
// between WiFi and BLE, and BLE advertising preempts WiFi RX (= dropped rtpMIDI
// packets).  Pausing advertising while a session is live frees the radio; it
// resumes when the session drops so a phone can still connect.  Distinct from WiFi
// being connected — you can be on WiFi with no rtpMIDI session.
static volatile bool rtpSessionActive = false;

// Self-healing state sync: after a preset push the plugin sends a SysEx 0x0E
// checksum of its serialized layer region.  We hash our own layer region the same
// way and, on mismatch (= a dropped bundle/fragment over lossy WiFi/BLE), ask the
// plugin to resend the full state via CC 119.  Capped + rate-limited so a genuine
// disagreement the resend can't fix (e.g. a plugin/firmware version skew) can't
// turn into an endless request storm.
static const uint32_t RESYNC_COOLDOWN_MS  = 500;  // min spacing between requests
static const int      MAX_RESYNC_ATTEMPTS = 4;    // per run of consecutive mismatches
static uint32_t lastResyncMs   = 0;
static int      resyncAttempts = 0;

// FNV-1a (32-bit) — must match the plugin's checksum of serializeState() bytes 6..end.
static uint32_t fnv1a32(const uint8_t* p, size_t n) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= 16777619u; }
  return h;
}

// ============================================================================
// Persistent State
// ============================================================================

Preferences prefs;

void saveState() {
  uint8_t buf[LightEngine::STATE_SIZE];
  size_t len = engine.serializeState(buf, sizeof(buf));
  if (len == 0) return;
  prefs.begin("lights", false);
  prefs.putBytes("state", buf, len);
  prefs.end();
  Serial.println("State saved");
}

void loadState() {
  prefs.begin("lights", true);
  size_t len = prefs.getBytesLength("state");
  if (len == LightEngine::STATE_SIZE) {
    uint8_t buf[LightEngine::STATE_SIZE];
    prefs.getBytes("state", buf, len);
    if (!engine.deserializeState(buf, len)) {
      Serial.println("State load failed: invalid data, using defaults");
    } else {
      Serial.println("State loaded");
    }
  } else {
    Serial.println("No saved state found, using defaults");
  }
  prefs.end();
}

// Load the persistent device name into the global `deviceName`.  If none is
// stored, derive a unique default from the last two octets of the factory MAC
// so identical firmware still yields a distinct name per board.
void loadDeviceName() {
  prefs.begin("lights", true);
  String stored = prefs.getString("name", "");
  prefs.end();
  if (stored.length() > 0) {
    stored.toCharArray(deviceName, sizeof(deviceName));
  } else {
    // High 16 bits of the 48-bit eFuse MAC = the two most device-unique octets.
    uint16_t suffix = (uint16_t)(ESP.getEfuseMac() >> 32);
    snprintf(deviceName, sizeof(deviceName), DEFAULT_NAME_PREFIX "-%04X", suffix);
  }
}

// Persist a new device name (takes effect on the next boot — the BLE advertising
// name and mDNS hostname are latched at begin() time).
void saveDeviceName(const char* name) {
  prefs.begin("lights", false);
  prefs.putString("name", name);
  prefs.end();
  Serial.printf("Device name saved: %s\n", name);
}

// ============================================================================
// Startup Animation
// ============================================================================

void playStartupAnimation() {
  const CRGB chartreuse = CRGB(127, 255, 0);
  const CRGB limeGreen  = CRGB(0, 255, 0);
  const int stripeWidth = 5;

  for (int i = 0; i < currentLedCount; i++)
    leds[i] = ((i / stripeWidth) % 2 == 0) ? chartreuse : limeGreen;

  // Ramp up 0→255 over 2 seconds
  unsigned long start = millis();
  while (millis() - start < 2000) {
    FastLED.setBrightness((uint8_t)(255UL * (millis() - start) / 2000));
    FastLED.show();
    delay(16);
  }

  // Ramp down 255→128 over 1 second
  start = millis();
  while (millis() - start < 1000) {
    FastLED.setBrightness((uint8_t)(255 - 127UL * (millis() - start) / 1000));
    FastLED.show();
    delay(16);
  }

  FastLED.setBrightness(128);
  FastLED.show();
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Light Engine - ESP32 WiFi-MIDI Version");

  Serial.printf("[DIAG] boot  heap: %u  min-ever: %u\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap());

  // Resolve the runtime device name (NVS override or MAC-suffixed default) before
  // BLE and OTA start, since both latch the name at begin() time.
  loadDeviceName();
  Serial.printf("[DIAG] device name: %s\n", deviceName);

  // Load persisted state first so FastLED is registered at the saved LED count
  // (the engine clamps it to [1, MAX_LEDS]; defaults to NUM_LEDS when no state).
  loadState();
  engine.numLedsChanged();  // clear the flag — we register at the loaded count here
  currentLedCount = engine.getNumLEDs();
  ledController = &FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, currentLedCount);
  FastLED.setCorrection(CRGB(0xFF, 0xD0, 0xF0));  // G at 82% instead of 69%

  // Connect to WiFi
  WiFi.persistent(false);   // skip writing creds to NVS flash every boot — shaves
                            // a small but nonzero delay off connect, and avoids
                            // flash wear from repeated power-cycling.
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  // Disable modem sleep — eliminates the 50-100ms beacon-interval latency that
  // wrecks rtpMIDI. Costs a few extra mA but is essential for real-time MIDI.
  WiFi.setSleep(false);

  // Max TX power — improves link margin/SNR, which lowers the underlying
  // packet-loss rate that UDP-based rtpMIDI has no retransmission for.
  WiFi.setTxPower(WIFI_POWER_19_5dBm);

  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Configure rtpMIDI (Windows) or Audio MIDI Setup (Mac) to connect");

  // --- rtpMIDI (AppleMIDI over WiFi) ---
  MIDI.begin(MIDI_CHANNEL_OMNI);

  AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc, const char* name) {
    Serial.print("[rtpMIDI] Connected: ");
    Serial.println(name);
    // Pause BLE advertising (applied in loop) so the shared 2.4GHz radio stops
    // being time-sliced for BLE while this WiFi/rtpMIDI session is active.
    rtpSessionActive = true;
    // Ask the plugin to push its current engine state over the newly established session.
    // CC 119 on channel 1 is the agreed "please send me your state" signal.
    MIDI.sendControlChange(119, 0, 1);
    Serial.println("[Sync] Sent CC 119 requesting full state from plugin");
  });
  AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc) {
    Serial.println("[rtpMIDI] Disconnected");
    // Session gone — let loop() resume BLE advertising so a phone can connect.
    rtpSessionActive = false;
  });

  // Log every library-detected drop/exception so they're visible on Serial.
  AppleMIDI.setHandleException([](const APPLEMIDI_NAMESPACE::ssrc_t&, const APPLEMIDI_NAMESPACE::Exception& e, const int32_t val) {
    if (e == APPLEMIDI_NAMESPACE::Exception::SendPacketsDropped) {
      rtpDropsSend++;
      Serial.printf("[DROP] Send packet dropped (total %u)\n", rtpDropsSend);
    } else if (e == APPLEMIDI_NAMESPACE::Exception::ReceivedPacketsDropped) {
      rtpDropsRecv++;
      Serial.printf("[DROP] Recv packet dropped (total %u)\n", rtpDropsRecv);
    } else {
      rtpExceptions++;
      Serial.printf("[DROP] AppleMIDI exception %d val=%d (total %u)\n", (int)e, (int)val, rtpExceptions);
    }
  });

  // Detect sequence-number gaps in received RTP packets (= lost UDP datagrams).
  AppleMIDI.setHandleReceivedRtp([](const APPLEMIDI_NAMESPACE::ssrc_t&, const APPLEMIDI_NAMESPACE::Rtp_t& rtp, const int32_t&) {
    if (rtpSeqInit) {
      uint16_t expected = lastRtpSeq + 1;
      if (rtp.sequenceNr != expected) {
        rtpSeqGaps++;
        Serial.printf("[DROP] RTP seq gap: expected %u got %u (total gaps %u)\n",
          expected, rtp.sequenceNr, rtpSeqGaps);
      }
    }
    lastRtpSeq = rtp.sequenceNr;
    rtpSeqInit = true;
  });

  MIDI.setHandleNoteOn(OnNoteOn);
  MIDI.setHandleNoteOff(OnNoteOff);
  MIDI.setHandleControlChange(OnControlChange);
  MIDI.setHandleSystemExclusive(OnSysEx);

  // --- BLE-MIDI (NimBLE) ---
  BLEMidiServer.begin(deviceName);
  BLEMidiServer.enableDebugging(Serial);  // Print raw packet bytes to serial for diagnostics

  BLEMidiServer.setOnConnectCallback([]() {
    bleConnected = true;
    Serial.println("[BLE-MIDI] Connected");
    // Ask the plugin to push its current engine state so the Arduino stays in sync.
    // Set flag; loop() will send CC 119 on the next iteration (safe BLE TX context).
    requestSyncViaBLE = true;
  });
  BLEMidiServer.setOnDisconnectCallback([]() {
    bleConnected = false;
    Serial.println("[BLE-MIDI] Disconnected");
  });

  // Reuse the same handlers — both transports feed the same engine
  // BLE-MIDI callbacks include a timestamp parameter; bridge to the shared handlers.
  // NOTE: BLE-MIDI library delivers 0-based channels (0-15); add 1 to match the
  // 1-based convention expected by the engine and the rtpMIDI path.
  BLEMidiServer.setNoteOnCallback([](uint8_t ch, uint8_t note, uint8_t vel, uint16_t) { OnNoteOn(ch + 1, note, vel); });
  BLEMidiServer.setNoteOffCallback([](uint8_t ch, uint8_t note, uint8_t vel, uint16_t) { OnNoteOff(ch + 1, note, vel); });
  BLEMidiServer.setControlChangeCallback([](uint8_t ch, uint8_t ctrl, uint8_t val, uint16_t) { OnControlChange(ch + 1, ctrl, val); });
  BLEMidiServer.setSysExCallback([](uint8_t *data, uint16_t length, uint16_t) { OnSysEx(data, length); });

  //ArduinoOTA
  ArduinoOTA.setHostname(deviceName);
  ArduinoOTA.setPort(3232);  // explicit port forces mDNS to advertise it correctly
  ArduinoOTA.begin();

  Serial.println("Ready!");
  playStartupAnimation();

  // Initial render
  engine.render();
  copyEngineToFastLED();
  FastLED.show();
}

void loop() {
  ArduinoOTA.handle();
  while (MIDI.read()) {}  // drain all pending events each tick

  // Send BLE sync request outside the NimBLE callback (safe TX context).
  if (requestSyncViaBLE) {
    requestSyncViaBLE = false;
    // CC 119, value 0, channel 0 (0-indexed = MIDI ch 1)
    BLEMidiServer.controlChange(0, 119, 0);
    Serial.println("[Sync] Sent CC 119 requesting full state from plugin (BLE)");
  }

  // Free the shared 2.4GHz radio for rtpMIDI: advertise BLE only when there's no
  // rtpMIDI session AND no BLE client connected.  Re-asserted every loop so the
  // ESP32-BLE-MIDI library's own advertising restart (it re-advertises on BLE
  // disconnect) can't override the pause mid-session.  Cheap: NimBLE start/stop
  // are no-ops when already in the target state, and we only call on a change.
  {
    static bool bleAdvertising = true;  // BLEMidiServer.begin() starts advertising
    bool wantAdvertising = !rtpSessionActive && !bleConnected;
    if (wantAdvertising != bleAdvertising) {
      if (wantAdvertising) NimBLEDevice::startAdvertising();
      else                 NimBLEDevice::stopAdvertising();
      bleAdvertising = wantAdvertising;
      Serial.printf("[BLE] advertising %s\n",
          wantAdvertising ? "resumed" : "paused (rtpMIDI session active)");
    }
  }

  // Self-healing: when the plugin's post-push checksum (SysEx 0x0E) arrives, verify
  // our applied state matches; if a packet was lost, ask for a full resend.  The
  // ~1.9KB serialize runs here on the loop task's stack (not the small MIDI/BLE
  // callback stacks).
  uint32_t peerChecksum;
  if (engine.takePeerStateChecksum(peerChecksum)) {
    uint8_t stateBuf[LightEngine::STATE_SIZE];
    size_t  n = engine.serializeState(stateBuf, sizeof(stateBuf));
    // Hash only the layer region (skip magic+version+tempo+LED-count header) so
    // loosely-synced globals can't cause a false mismatch / resync loop.
    uint32_t mine = (n > 6) ? fnv1a32(stateBuf + 6, n - 6) : 0;
    if (mine == peerChecksum) {
      resyncAttempts = 0;  // in sync — clear the attempt run
    } else {
      uint32_t nowMs = millis();
      if (resyncAttempts < MAX_RESYNC_ATTEMPTS && nowMs - lastResyncMs > RESYNC_COOLDOWN_MS) {
        lastResyncMs = nowMs;
        resyncAttempts++;
        Serial.printf("[Resync] state mismatch mine=%08x peer=%08x — requesting full state (%d/%d)\n",
            mine, peerChecksum, resyncAttempts, MAX_RESYNC_ATTEMPTS);
        MIDI.sendControlChange(119, 0, 1);            // rtpMIDI peer (no-op if none)
        if (bleConnected) BLEMidiServer.controlChange(0, 119, 0);  // BLE peer
      } else if (resyncAttempts >= MAX_RESYNC_ATTEMPTS) {
        Serial.printf("[Resync] giving up after %d attempts (mine=%08x peer=%08x) — "
                      "likely a plugin/firmware mismatch, not packet loss\n",
            resyncAttempts, mine, peerChecksum);
        resyncAttempts++;  // step past the cap so this logs only once per run
      }
    }
  }

  // Render at 30Hz (every 33,333 microseconds)
  unsigned long now = micros();
  if (now - lastRender >= renderInterval) {
    unsigned long jitter = (now - lastRender) - renderInterval;
    if (jitter > maxRenderJitter) maxRenderJitter = jitter;
    renderCount++;
    engine.render();
    copyEngineToFastLED();
    FastLED.show();
    lastRender = now;

    // Track active LED count changes (SysEx 0x09 or state sync).
    // We do NOT call ledController->setLeds() here: FastLED's ESP32 RMT backend
    // initialises its internal led_strip object at addLeds() time with a fixed
    // pixel count and asserts if show() is called with a different size.
    // copyEngineToFastLED() blanks any tail LEDs beyond the active count, so the
    // physical strip always sees the right colours even when the count shrinks.
    if (engine.numLedsChanged()) {
      Serial.print("[LED] Count changed to ");
      Serial.println(engine.getNumLEDs());
    }
  }

  // Stability diagnostics every 5 seconds
  unsigned long nowMs = millis();
  if (nowMs - lastDiagMillis >= DIAG_INTERVAL_MS) {
    lastDiagMillis = nowMs;
    Serial.printf("[DIAG] Heap free: %u  min-ever: %u  renders: %lu  jitter: %luus  RSSI: %ddBm  BLE:%s  WiFi:%s  drops(send/recv/gap): %u/%u/%u\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap(),
      renderCount, maxRenderJitter,
      WiFi.RSSI(),
      bleConnected ? "connected" : "waiting",
      WiFi.status() == WL_CONNECTED ? "connected" : "lost",
      rtpDropsSend, rtpDropsRecv, rtpSeqGaps);
    maxRenderJitter = 0; // reset per window
  }

  // Save state when the plugin sends a save-state SysEx (0x06)
  if (engine.saveStateRequested()) {
    saveState();
  }
}

// ============================================================================
// MIDI Event Handlers (forward to engine)
// ============================================================================

void OnNoteOn(byte channel, byte note, byte velocity) {
  engine.handleNoteOn(channel, note, velocity);
}

void OnNoteOff(byte channel, byte note, byte velocity) {
  engine.handleNoteOff(channel, note, velocity);
}

void OnControlChange(byte channel, byte control, byte value) {
  // Intentionally IGNORE inbound raw MIDI CC.  Mode/waveshape control now arrives
  // as manufacturer SysEx 0x10 (see OnSysEx), because raw CC 1/3/4/5/6 collide
  // with the standard controllers a DAW blasts to every MIDI output on transport
  // start (e.g. FL Studio), which would otherwise corrupt the engine's per-layer
  // mode/waveshape/transient state.  Notes and SysEx are handled elsewhere.
  (void) channel; (void) control; (void) value;
}

// SysEx 0x0F: set persistent device name.  `nameBytes` points at the ASCII name
// payload (the bytes between the 0x0F command byte and the trailing 0xF7).
// Persists to NVS and reboots so the new name takes effect on BLE and mDNS.
void applyDeviceNameSysEx(const byte* nameBytes, int n) {
  if (n <= 0) return;
  char name[32] = {0};
  if (n > (int)sizeof(name) - 1) n = sizeof(name) - 1;
  memcpy(name, nameBytes, n);
  saveDeviceName(name);
  Serial.printf("Rebooting into new device name '%s'...\n", name);
  delay(50);          // let the serial line and any BLE ACK flush
  ESP.restart();
}

void OnSysEx(byte* data, unsigned int length) {
  // Different MIDI transports deliver different framing:
  //   rtpMIDI (Arduino MIDI Library) includes 0xF0/0xF7 in the callback data.
  //   BLE-MIDI (custom Midi.cpp parser)  strips 0xF0/0xF7 — only data bytes.
  // engine.handleSysEx() requires full [F0 ... F7] framing.  All message types
  // — including 0x0A state-sync fragments — are handled inside the engine.

  // -------------------------------------------------------------------------
  // Re-frame if needed and forward everything to the engine.
  // -------------------------------------------------------------------------
  if (length > 0 && data[0] == 0xF0) {
    // Already framed — pass directly (rtpMIDI path)
    // SysEx 0x0D: global brightness [F0, 7D, 0D, value, F7]
    if (length >= 5 && data[2] == 0x0D) { FastLED.setBrightness(data[3]); return; }
    // SysEx 0x0F: set device name [F0, 7D, 0F, <ascii>, F7] — payload is data[3..len-2]
    if (length >= 5 && data[2] == 0x0F) { applyDeviceNameSysEx(data + 3, (int)length - 4); return; }
    engine.handleSysEx(data, length);
  } else if (length <= 254) {
    // Unframed — reconstruct framing (BLE-MIDI path)
    byte framedData[256];
    framedData[0] = 0xF0;
    memcpy(framedData + 1, data, length);
    framedData[length + 1] = 0xF7;
    // SysEx 0x0D: global brightness [F0, 7D, 0D, value, F7]
    if ((length + 2) >= 5 && framedData[2] == 0x0D) { FastLED.setBrightness(framedData[3]); return; }
    // SysEx 0x0F: set device name — payload is framedData[3..(length+2)-2] == data[2..length-1]
    if ((length + 2) >= 5 && framedData[2] == 0x0F) { applyDeviceNameSysEx(framedData + 3, (int)length - 2); return; }
    engine.handleSysEx(framedData, length + 2);
  }

  // Per-message SysEx debug logging removed — it blocked the NimBLE receive task
  // during bulk preset sync (300+ messages), causing BLE packet drops.
  // Use the 5-second DIAG heartbeat for health monitoring instead.
}

// ============================================================================
// Utility: Copy engine's HSV buffer to FastLED's CRGB buffer
// ============================================================================

void copyEngineToFastLED() {
  // Use the engine's authoritative RGB output verbatim.  Assigning CHSV here
  // would apply FastLED's hsv2rgb_rainbow remapping and the strip would no
  // longer match the plugin preview (which draws the same RGB bytes).
  // The controller is always driven at currentLedCount (registered at boot).
  // LEDs beyond the engine's active count are blanked so tail pixels go dark
  // when the count shrinks without needing to resize the FastLED controller.
  const RGBColor* engineRGB = engine.getRGB();
  const int activeCount = engine.getNumLEDs();
  for (int i = 0; i < currentLedCount; i++) {
    if (i < activeCount)
      leds[i].setRGB(engineRGB[i].r, engineRGB[i].g, engineRGB[i].b);
    else
      leds[i] = CRGB::Black;
  }
  napplyGamma_video(leds, currentLedCount, 2.2);
}
