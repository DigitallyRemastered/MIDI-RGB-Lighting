/* lights - Arduino Firmware for RGB Lighting (Teensy USB-MIDI Version)
 * 
 * Thin wrapper around the LightEngine class.
 * All rendering logic, constants, and state are handled by the engine.
 * This file simply forwards MIDI events and calls render() at 30Hz.
 */

#include <FastLED.h>
#include <LightEngine.h>  // Library include
#include <EEPROM.h>

#define NUM_LEDS 100       // Default LED count when no saved state exists.  The active
                           // count is runtime-changeable (SysEx 0x09 / state sync) and
                           // persists in the saved engine state.
#define DATA_PIN 0

// FastLED buffer (CRGB is an RGB struct) — allocated for maximum possible LED count
CRGB leds[LightEngine::MAX_LEDS];

// FastLED controller handle + currently registered LED count.  setLeds() on the
// controller re-registers the strip when the active count changes at runtime.
CLEDController* ledController = nullptr;
int currentLedCount = NUM_LEDS;

// Light engine instance (manages all state and logic)
LightEngine engine(NUM_LEDS);

// Timer for 30Hz refresh rate
elapsedMicros t;

// ============================================================================
// Persistent State
// ============================================================================

// EEPROM base address for saved state
static const int EEPROM_BASE = 0;

// The full engine state must fit this board's EEPROM.  Teensy 3.6 has 4 KB;
// Teensy 4.0's emulated EEPROM is only ~1080 bytes and cannot hold the state —
// fail at compile time rather than silently truncating saves.
static_assert(LightEngine::STATE_SIZE <= (size_t) E2END + 1,
              "LightEngine state does not fit this board's EEPROM");

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
  usbMIDI.setHandleNoteOff(OnNoteOff);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  usbMIDI.setHandleControlChange(OnControlChange);
  usbMIDI.setHandleSysEx(OnSysEx);

  Serial.begin(250000);

  // Load persisted state first so FastLED is registered at the saved LED count
  // (the engine clamps it to [1, MAX_LEDS]; defaults to NUM_LEDS when no state).
  loadState();
  engine.numLedsChanged();  // clear the flag — we register at the loaded count here
  currentLedCount = engine.getNumLEDs();
  ledController = &FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, currentLedCount);
  FastLED.setCorrection(TypicalLEDStrip);

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
  if (!complete) return;
  // SysEx 0x0D: global brightness [F0, 7D, 0D, value, F7]
  if (length >= 5 && data[2] == 0x0D) { FastLED.setBrightness(data[3]); return; }
  engine.handleSysEx(data, length);
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
  napplyGamma_video(leds, activeCount, 2.2);
}
