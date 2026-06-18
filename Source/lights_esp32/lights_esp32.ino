/* lights_esp32 - Arduino Firmware for RGB Lighting (ESP32 Dual-Mode MIDI Version)
 * 
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 *
 * Connections:
 *   - rtpMIDI (AppleMIDI) over WiFi  — for PC/Mac/iOS on the same network
 *   - BLE-MIDI (NimBLE)              — for Android (and iOS as fallback)
 *
 * Required libraries (Arduino Library Manager):
 *   - AppleMIDI  (lathoub/Arduino-AppleMIDI-Library)
 *   - ESP32-BLE-MIDI  (max22-/ESP32-BLE-MIDI)
 *   - NimBLE-Arduino  (h2zero/NimBLE-Arduino)   <-- USE_NIMBLE saves ~35KB RAM
 *   - FastLED
 *   - LightEngine (local)
 */

#include <WiFi.h>
#include <AppleMIDI.h>
#define USE_NIMBLE          // Use NimBLE stack (~35KB) instead of BlueDroid (~70KB)
#include <BLEMidi.h>        // ESP32-BLE-MIDI library
#include <FastLED.h>
#include <LightEngine.h>  // Library include
#include <Preferences.h>
#include "secrets.h"  // WiFi credentials — do not commit this file

#define NUM_LEDS 100       // Default LED count when no saved state exists.  The active
                           // count is runtime-changeable (SysEx 0x09 / state sync) and
                           // persists in the saved engine state.
#define DATA_PIN 2  // GPIO 2 (safer than GPIO 0 on ESP32)
#define BLE_DEVICE_NAME "DR Perform 3"  // shown in Bluetooth scan lists

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

// AppleMIDI instance (rtpMIDI over WiFi)
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

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

// NOTE: SysEx 0x0A full-state sync reassembly now lives inside LightEngine
// (shared by Teensy, ESP32, and the plugin) — this sketch just forwards SysEx.

// Flag set by BLE connect callback so loop() sends CC 119 on the next iteration
// (avoids calling BLE TX from inside the NimBLE callback thread).
static volatile bool requestSyncViaBLE = false;

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

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  Serial.begin(115200);
  Serial.println("Light Engine - ESP32 WiFi-MIDI Version");

  Serial.printf("[DIAG] Heap free: %u  min-ever: %u  max-alloc: %u  renders: %lu  worst-jitter: %luus  BLE:%s  WiFi:%s\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
      renderCount, maxRenderJitter,
      bleConnected ? "connected" : "waiting",
      WiFi.status() == WL_CONNECTED ? "connected" : "lost");
  
  // Load persisted state first so FastLED is registered at the saved LED count
  // (the engine clamps it to [1, MAX_LEDS]; defaults to NUM_LEDS when no state).
  loadState();
  engine.numLedsChanged();  // clear the flag — we register at the loaded count here
  currentLedCount = engine.getNumLEDs();
  ledController = &FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, currentLedCount);

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
    // Ask the plugin to push its current engine state over the newly established session.
    // CC 119 on channel 1 is the agreed "please send me your state" signal.
    MIDI.sendControlChange(119, 0, 1);
    Serial.println("[Sync] Sent CC 119 requesting full state from plugin");
  });
  AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc) {
    Serial.println("[rtpMIDI] Disconnected");
  });

  MIDI.setHandleNoteOn(OnNoteOn);
  MIDI.setHandleNoteOff(OnNoteOff);
  MIDI.setHandleControlChange(OnControlChange);
  MIDI.setHandleSystemExclusive(OnSysEx);

  // --- BLE-MIDI (NimBLE) ---
  BLEMidiServer.begin(BLE_DEVICE_NAME);
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
  
  Serial.println("Ready!");
  
  // Initial render
  engine.render();
  copyEngineToFastLED();
  FastLED.show();
}

void loop() {
  MIDI.read();  // Process incoming MIDI

  // Send BLE sync request outside the NimBLE callback (safe TX context).
  if (requestSyncViaBLE) {
    requestSyncViaBLE = false;
    // CC 119, value 0, channel 0 (0-indexed = MIDI ch 1)
    BLEMidiServer.controlChange(0, 119, 0);
    Serial.println("[Sync] Sent CC 119 requesting full state from plugin (BLE)");
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

    // Re-register FastLED if the active LED count changed (SysEx 0x09 or state sync)
    if (engine.numLedsChanged()) {
      const int newCount = engine.getNumLEDs();
      if (newCount < currentLedCount) {
        // Push one final frame at the old count to physically blank the tail —
        // after shrinking, those LEDs would otherwise keep their last colour.
        for (int i = newCount; i < currentLedCount; i++)
          leds[i] = CRGB::Black;
        FastLED.show();
      }
      ledController->setLeds(leds, newCount);
      currentLedCount = newCount;
      Serial.print("[LED] Count changed to ");
      Serial.println(newCount);
    }
  }

  // Stability diagnostics every 5 seconds
  unsigned long nowMs = millis();
  if (nowMs - lastDiagMillis >= DIAG_INTERVAL_MS) {
    lastDiagMillis = nowMs;
    Serial.printf("[DIAG] Heap free: %u  min-ever: %u  max-alloc: %u  renders: %lu  worst-jitter: %luus  BLE:%s  WiFi:%s\n",
      ESP.getFreeHeap(), ESP.getMinFreeHeap(), ESP.getMaxAllocHeap(),
      renderCount, maxRenderJitter,
      bleConnected ? "connected" : "waiting",
      WiFi.status() == WL_CONNECTED ? "connected" : "lost");
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
  engine.handleControlChange(channel, control, value);
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
    engine.handleSysEx(data, length);
  } else if (length <= 254) {
    // Unframed — reconstruct framing (BLE-MIDI path)
    byte framedData[256];
    framedData[0] = 0xF0;
    memcpy(framedData + 1, data, length);
    framedData[length + 1] = 0xF7;
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
  const RGBColor* engineRGB = engine.getRGB();
  const int activeCount = engine.getNumLEDs();
  for (int i = 0; i < activeCount && i < LightEngine::MAX_LEDS; i++) {
    leds[i].setRGB(engineRGB[i].r, engineRGB[i].g, engineRGB[i].b);
  }
}
