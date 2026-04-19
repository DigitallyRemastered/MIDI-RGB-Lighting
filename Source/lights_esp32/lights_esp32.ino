/* lights_esp32 - Arduino Firmware for RGB Lighting (ESP32 WiFi-MIDI Version)
 * 
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 */

#include <WiFi.h>
#include <AppleMIDI.h>
#include <FastLED.h>
#include <LightEngine.h>  // Library include
#include <Preferences.h>

// ============================================================================
// Configuration - UPDATE THESE!
// ============================================================================

const char* ssid = "YourWiFiName";        // Change to your WiFi network name
const char* password = "YourWiFiPassword"; // Change to your WiFi password

#define NUM_LEDS 108
#define DATA_PIN 2  // GPIO 2 (safer than GPIO 0 on ESP32)

// ============================================================================
// Hardware Setup
// ============================================================================

// FastLED buffer (CRGB is an RGB struct)
CRGB leds[NUM_LEDS];

// Light engine instance (manages all state and logic)
LightEngine engine(NUM_LEDS);

// AppleMIDI instance
APPLEMIDI_CREATE_DEFAULTSESSION_INSTANCE();

// Timer for 30Hz refresh rate
unsigned long lastRender = 0;
const unsigned long renderInterval = 33333; // microseconds (30Hz)

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
  
  // Start AppleMIDI
  MIDI.begin(MIDI_CHANNEL_OMNI);
  
  // Connection callbacks
  AppleMIDI.setHandleConnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc, const char* name) {
    Serial.print("MIDI Connected to: ");
    Serial.println(name);
  });
  
  AppleMIDI.setHandleDisconnected([](const APPLEMIDI_NAMESPACE::ssrc_t & ssrc) {
    Serial.println("MIDI Disconnected");
  });
  
  // MIDI callbacks
  MIDI.setHandleNoteOn(OnNoteOn);
  MIDI.setHandleNoteOff(OnNoteOff);
  MIDI.setHandleControlChange(OnControlChange);
  MIDI.setHandleSystemExclusive(OnSysEx);
  
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
    engine.render();
    copyEngineToFastLED();
    FastLED.show();
    lastRender = now;
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
