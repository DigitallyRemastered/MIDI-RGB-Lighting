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

#define NUM_LEDS 108
#define DATA_PIN 2  // GPIO 2 (safer than GPIO 0 on ESP32)
#define BLE_DEVICE_NAME "DR LightStudio"  // shown in Bluetooth scan lists

// ============================================================================
// Hardware Setup
// ============================================================================

// FastLED buffer (CRGB is an RGB struct)
CRGB leds[NUM_LEDS];

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

// ============================================================================
// Persistent State
// ============================================================================

Preferences prefs;

bool stateDirty = false;
unsigned long lastChangeMillis = 0;
const unsigned long SAVE_DEBOUNCE_MS = 2000;

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
  
  // Initialize FastLED
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  
  // Connect to WiFi
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid, password);
  
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  
  Serial.println("\nWiFi Connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());
  Serial.println("Configure rtpMIDI (Windows) or Audio MIDI Setup (Mac) to connect");
  
  // --- rtpMIDI (AppleMIDI over WiFi) ---
  MIDI.begin(MIDI_CHANNEL_OMNI);

  AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc, const char* name) {
    Serial.print("[rtpMIDI] Connected: ");
    Serial.println(name);
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

  BLEMidiServer.setOnConnectCallback([]() {
    bleConnected = true;
    Serial.println("[BLE-MIDI] Connected");
  });
  BLEMidiServer.setOnDisconnectCallback([]() {
    bleConnected = false;
    Serial.println("[BLE-MIDI] Disconnected");
  });

  // Reuse the same handlers — both transports feed the same engine
  BLEMidiServer.setHandleNoteOn(OnNoteOn);
  BLEMidiServer.setHandleNoteOff(OnNoteOff);
  BLEMidiServer.setHandleControlChange(OnControlChange);
  // Note: SysEx (waveform config) is not forwarded over BLE-MIDI in this version;
  // waveform automation is a DAW/PC feature. BLE-MIDI carries CC1-15 only.
  
  // Load persisted state before first render
  loadState();

  Serial.println("Ready!");
  
  // Initial render
  engine.render();
  copyEngineToFastLED();
  FastLED.show();
}

void loop() {
  MIDI.read();  // Process incoming MIDI
  
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

  // Debounced auto-save: write 2 seconds after the last state change
  if (stateDirty && (millis() - lastChangeMillis >= SAVE_DEBOUNCE_MS)) {
    saveState();
    stateDirty = false;
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
  // Only mark dirty if this CC actually changes the engine state
  if (engine.getCC(control) != value) {
    stateDirty = true;
    lastChangeMillis = millis();
  }
  engine.handleControlChange(channel, control, value);

  // Debug output
  Serial.print("CC received:");
  Serial.print(control);
  Serial.print(" | value: ");
  Serial.print(value);
  Serial.println();
}

void OnSysEx(byte* data, unsigned int length) {
  // Only process complete SysEx messages
  stateDirty = true;
  lastChangeMillis = millis();
  engine.handleSysEx(data, length);
     // Debug output - show first few bytes of received SysEx
    // Serial.print("SysEx received (");
    // Serial.print(length);
    // Serial.print(" bytes): ");
    // for (int i = 0; i < min(8, length); i++) {
    //   Serial.print(data[i], HEX);
    //   Serial.print(" ");
    // }
    // Serial.println();
}

// ============================================================================
// Utility: Copy engine's HSV buffer to FastLED's CRGB buffer
// ============================================================================

void copyEngineToFastLED() {
  const HSVColor* engineLEDs = engine.getLEDs();
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(engineLEDs[i].h, engineLEDs[i].s, engineLEDs[i].v);
  }
}
