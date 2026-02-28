# LightEngine Library

Shared RGB lighting engine for MIDI-controlled LED strips.

## Description

This library provides a unified rendering engine that handles MIDI note/CC processing and generates HSV color values for FastLED-compatible LED strips. It's designed to be used by both Arduino projects and Windows DLL plugins.

## Features

- **10 Foreground Rendering Modes**: Notes to Drives, Rainbow Wheel, Moving Dots, Comets, and more
- **3 Background Modes**: Flat color, Rainbow, Sinusoid
- **15 MIDI CC Parameters**: Real-time control of all visual parameters
- **Platform Independent**: Works on Arduino (Teensy, ESP32) and Windows (DLL)
- **30Hz Rendering**: Consistent frame rate for smooth animations
- **HSV Color Space**: Natural color transitions and effects

## Installation

This library is designed to be symlinked from your project repo to Arduino's libraries folder.

**Run the setup script from the repo root:**

```powershell
# Windows (as Administrator)
.\setup_library.ps1
```

```bash
# Linux/Mac
ln -s "$(pwd)/libraries/LightEngine" ~/Documents/Arduino/libraries/LightEngine
```

## Usage

```cpp
#include <FastLED.h>
#include <LightEngine.h>

#define NUM_LEDS 108
#define DATA_PIN 2

CRGB leds[NUM_LEDS];
LightEngine engine(NUM_LEDS);

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);
}

void loop() {
  // Handle MIDI events
  engine.handleNoteOn(channel, note, velocity);
  engine.handleNoteOff(channel, note, velocity);
  engine.handleControlChange(channel, control, value);
  
  // Render at 30Hz
  engine.render();
  
  // Copy to FastLED
  const HSVColor* engineLEDs = engine.getLEDs();
  for (int i = 0; i < NUM_LEDS; i++) {
    leds[i] = CHSV(engineLEDs[i].h, engineLEDs[i].s, engineLEDs[i].v);
  }
  FastLED.show();
}
```

## MIDI CC Mapping

| CC# | Parameter | Description |
|-----|-----------|-------------|
| 1 | Foreground Hue | Color (0-127) |
| 2 | Foreground Saturation | Saturation (0-127) |
| 3 | Foreground Brightness | Brightness (0-127) |
| 4 | Start Position | LED start position |
| 5 | Length | LED strip length |
| 6 | Foreground Mode | Mode selector (0-9) |
| 7 | Lines | Number of lines |
| 8 | Color Amplitude | Sinusoid amplitude |
| 9 | Background Mode | Background mode (0-2) |
| 10 | Pan | Wave pan position |
| 11 | Background Hue | Background color |
| 12 | Background Saturation | Background saturation |
| 13 | Background Brightness | Background brightness |
| 14 | Background Start | Background start position |
| 15 | Background Length | Background length |

## API Reference

### Core Methods

```cpp
// MIDI Event Handlers
void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
void handleControlChange(uint8_t channel, uint8_t control, uint8_t value);

// Rendering
void render();                    // Call at ~30Hz
const HSVColor* getLEDs() const; // Get LED buffer
int getNumLEDs() const;          // Get LED count

// State Access
int getCC(int ccNumber) const;   // Get CC value (0-127)
void setCC(int ccNumber, int value); // Set CC value
```

### Foreground Modes (CC6)

- **0**: Notes to Drives - Maps MIDI channels to LED groups
- **1**: Rainbow Wheel - Rotating rainbow pattern
- **2**: Moving Dots - Animated moving dots
- **3**: Comets - Comet tail effects
- **4**: Back and Forth - Oscillating blocks
- **5**: Move Start LED - Shifts start position per note
- **6**: Color Sinusoid - Sine wave color pattern
- **7**: Flash Lights - Random LED flashes
- **8**: Ocean Waves - Wave effect with mirroring
- **9**: Opposing Waves - Dual wave effect

### Background Modes (CC9)

- **0**: Flat Color - Solid background color
- **1**: Rainbow Wheel - Rotating rainbow background
- **2**: Sinusoid - Sine wave color pattern

## Hardware Compatibility

- **Teensy** (3.2, 4.0, 4.1) - USB-MIDI
- **ESP32** (all variants) - WiFi-MIDI, BLE-MIDI
- **Arduino** (Uno, Mega, etc.) - Serial MIDI

## Dependencies

- **FastLED** (3.0+) - LED control library

## License

MIT License
