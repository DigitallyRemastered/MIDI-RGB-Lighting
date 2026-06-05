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

#define NUM_LEDS 100       // Initial LED count (can be changed at runtime via SysEx 0x09)
#define DATA_PIN 2  // GPIO 2 (safer than GPIO 0 on ESP32)
#define BLE_DEVICE_NAME "DR Perform 3"  // shown in Bluetooth scan lists

// ============================================================================
// Hardware Setup
// ============================================================================

// FastLED buffer (CRGB is an RGB struct) — allocated for maximum possible LED count
CRGB leds[LightEngine::MAX_LEDS];

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
  
  // Disable modem sleep — eliminates the 50-100ms beacon-interval latency that
  // wrecks rtpMIDI. Costs a few extra mA but is essential for real-time MIDI.
  WiFi.setSleep(false);

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
  BLEMidiServer.enableDebugging(Serial);  // Print raw packet bytes to serial for diagnostics

  BLEMidiServer.setOnConnectCallback([]() {
    bleConnected = true;
    Serial.println("[BLE-MIDI] Connected");
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

    // Update FastLED if the active LED count changed via SysEx 0x09
    if (engine.numLedsChanged()) {
      FastLED[0].setLeds(leds, engine.getNumLEDs());
      Serial.print("[LED] Count changed to ");
      Serial.println(engine.getNumLEDs());
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

  Serial.print("Note On received on channel: ");
  Serial.print(channel);
  Serial.print(" | note: ");
  Serial.print(note);
  Serial.println();
}

void OnNoteOff(byte channel, byte note, byte velocity) {
  engine.handleNoteOff(channel, note, velocity);
  Serial.print("Note Off received on channel: ");
  Serial.print(channel);
  Serial.print(" | note: ");
  Serial.print(note);
  Serial.println();
}

void OnControlChange(byte channel, byte control, byte value) {
  engine.handleControlChange(channel, control, value);

  // Debug output
  Serial.print("CC received:");
  Serial.print(control);
  Serial.print(" | value: ");
  Serial.print(value);
  Serial.println();
}

void OnSysEx(byte* data, unsigned int length) {
  // Different MIDI transports deliver different framing:
  //   rtpMIDI (Arduino MIDI Library) includes 0xF0/0xF7 in the callback data.
  //   BLE-MIDI (custom Midi.cpp parser)  strips 0xF0/0xF7 — only data bytes.
  // engine.handleSysEx() requires full [F0 ... F7] framing.
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

  // Debug output - show first few bytes of received SysEx
    Serial.print("SysEx received (");
    Serial.print(length);
    Serial.print(" bytes): ");
    for (int i = 0; i < (int)min((unsigned int)8, length); i++) {
      Serial.print(data[i], HEX);
      Serial.print(" ");
    }
    Serial.println();
}

// ============================================================================
// Utility: Copy engine's HSV buffer to FastLED's CRGB buffer
// ============================================================================

void copyEngineToFastLED() {
  const HSVColor* engineLEDs = engine.getLEDs();
  for (int i = 0; i < engine.getNumLEDs(); i++) {
    leds[i] = CHSV(engineLEDs[i].h, engineLEDs[i].s, engineLEDs[i].v);
  }
}
