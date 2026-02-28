/* lights - Arduino Firmware for RGB Lighting (Teensy USB-MIDI Version)
 * 
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 */

#include <FastLED.h>
#include <LightEngine.h>  // Library include

#define NUM_LEDS 108
#define DATA_PIN 0

// FastLED buffer (CRGB is an RGB struct)
CRGB leds[NUM_LEDS];

// Light engine instance (manages all state and logic)
LightEngine engine(NUM_LEDS);

// Timer for 30Hz refresh rate
elapsedMicros t;

// ============================================================================
// Setup & Loop
// ============================================================================

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
  
  usbMIDI.setHandleNoteOff(OnNoteOff);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  usbMIDI.setHandleControlChange(OnControlChange);
  
  Serial.begin(250000);
  
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

// ============================================================================
// Utility: Copy engine's HSV buffer to FastLED's CRGB buffer
// ============================================================================

void copyEngineToFastLED() {
  const HSVColor* engineLEDs = engine.getLEDs();
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(engineLEDs[i].h, engineLEDs[i].s, engineLEDs[i].v);
  }
}
