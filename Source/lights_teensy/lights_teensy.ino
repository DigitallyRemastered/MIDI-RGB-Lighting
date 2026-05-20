/* lights - Arduino Firmware for RGB Lighting (Teensy USB-MIDI Version)
 * 
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 */

#include <FastLED.h>
#include <LightEngine.h>  // Library include
#include <EEPROM.h>

#define NUM_LEDS 108
#define DATA_PIN 0

// FastLED buffer (CRGB is an RGB struct)
CRGB leds[NUM_LEDS];

// Light engine instance (manages all state and logic)
LightEngine engine(NUM_LEDS);

// Timer for 30Hz refresh rate
elapsedMicros t;

// ============================================================================
// Persistent State
// ============================================================================

// EEPROM base address for saved state
static const int EEPROM_BASE = 0;

void saveState() {
  uint8_t buf[LightEngine::STATE_SIZE];
  size_t len = engine.serializeState(buf, sizeof(buf));
  if (len == 0) return;
  // EEPROM.update() only writes bytes that have actually changed,
  // minimising wear on the Teensy 3.6's real hardware EEPROM.
  for (size_t i = 0; i < len; ++i) {
    EEPROM.update(EEPROM_BASE + i, buf[i]);
  }
  Serial.println("State saved");
}

void loadState() {
  uint8_t buf[LightEngine::STATE_SIZE];
  for (size_t i = 0; i < LightEngine::STATE_SIZE; ++i) {
    buf[i] = EEPROM.read(EEPROM_BASE + i);
  }
  if (!engine.deserializeState(buf, LightEngine::STATE_SIZE)) {
    Serial.println("No valid saved state, using defaults");
  } else {
    Serial.println("State loaded");
  }
}

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  
  usbMIDI.setHandleNoteOff(OnNoteOff);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  usbMIDI.setHandleControlChange(OnControlChange);
  usbMIDI.setHandleSysEx(OnSysEx);
  
  Serial.begin(250000);

  // Load persisted state before first render
  loadState();
  
  // Initial render
  engine.render();
  copyEngineToFastLED();
}

void loop() {
  usbMIDI.read();
  
  // Render at 30Hz (every 33,333 microseconds)
  if (t > 33333) {
    engine.render();
    copyEngineToFastLED();
    FastLED.show();
    t = 0;
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
  // Debug output
  Serial.print("CC received:");
  Serial.print(control);
  Serial.print(" | value: ");
  Serial.print(value);
  Serial.println();
}

void OnSysEx(const byte* data, uint16_t length, bool complete) {
  // Only process complete SysEx messages
  if (complete) {
    engine.handleSysEx(data, length);
  }
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
