# LightEngineDLL Implementation Guide

This guide documents the extraction pattern for porting light modes from `lights.ino` to the DLL.

## Implementation Status

### ✅ Completed Modes (7/13)

**Foreground Modes:**
- ✅ Rainbow Wheel (case 1) - `src/Modes/RainbowWheel.cpp`
- ✅ Moving Dots (case 2) - `src/Modes/MovingDots.cpp`
- ✅ Comets (case 3) - `src/Modes/Comets.cpp`
- ✅ Ocean Waves (case 8) - `src/Modes/OceanWaves.cpp`

**Background Modes:**
- ✅ Flat Background (case 0) - `src/Modes/Backgrounds.cpp`
- ✅ Rainbow Background (case 1) - `src/Modes/Backgrounds.cpp`
- ✅ Sinusoid Background (case 2) - `src/Modes/Backgrounds.cpp`

### 🔨 Remaining Modes (6/13)

**Foreground Modes:**
- ⬜ Notes to Drives (case 0) - `lights.ino` lines 152-157 (OnNoteOn)
- ⬜ Back and Forth (case 4) - `lights.ino` lines 258-266
- ⬜ Move startLED (case 5) - `lights.ino` lines 158-174 (OnNoteOn)
- ⬜ Color Sinusoid (case 6) - `lights.ino` lines 266-273
- ⬜ Flash Lights (case 7) - `lights.ino` lines 273-279
- ⬜ Opposing Waves (case 9) - `lights.ino` lines 295-311

---

## Extraction Pattern

### Step 1: Locate Source Code in lights.ino

Find the mode's rendering logic:
- **Foreground modes**: `OnControlChange` switch statement (lines 228-330)
- **Background modes**: `updateBG` function (lines 336-361)
- **Special cases**: Some modes in `OnNoteOn` (Notes to Drives, Move startLED)

### Step 2: Map MIDI Variables to RenderContext Parameters

Replace global MIDI variables with `ctx->params[]` array access:

| lights.ino Variable | Array Index | Parameter | CC# |
|---------------------|-------------|-----------|-----|
| `ffHue`             | `params[0]` | Foreground Hue | 1 |
| `ffSat`             | `params[1]` | Foreground Saturation | 2 |
| `ffBright`          | `params[2]` | Foreground Brightness | 3 |
| `ffLedStart`        | `params[3]` | LED Start Position | 4 |
| `ffLedLength`       | `params[4]` | LED Length | 5 |
| `ffSat2`            | `params[5]` | Saturation 2 | 6 |
| `lines`             | `params[6]` | Lines/Divisions | 7 |
| `sinePeriod`        | `params[7]` | Sine Wave Period | 8 |
| `moveAmount`        | `params[8]` | Movement Amount | 9 |
| `waveLength`        | `params[9]` | Wave Length | 10 |
| `bgHue`             | `params[10]` | Background Hue | 11 |
| `bgSat`             | `params[11]` | Background Saturation | 12 |
| `bgBright`          | `params[12]` | Background Brightness | 13 |
| `phaseShift`        | `params[13]` | Phase Shift | 14 |
| `brightness`        | `params[14]` | Brightness Modifier | 15 |

**Important**: MIDI values are 0-127. Scale to 0-254 for HSV by multiplying by 2:
```cpp
int hue = ctx->params[0] * 2;  // ffHue: 0-127 → 0-254
```

### Step 3: Handle Background Buffer

If the mode layers over background (most foreground modes):
```cpp
// Copy background if provided
if (ctx->background) {
    memcpy(leds, ctx->background, numLeds * sizeof(HSV));
}
```

### Step 4: Replace Global Arrays

Replace Arduino global arrays with external constants:
- `COLOR_PHASE[i]` → `extern const int COLOR_PHASE[64]`
- `topBottomMirrorMap[i]` → `extern const uint8_t TOP_BOTTOM_MIRROR_MAP[48]`
- `channelToLED[ch][i]` → `extern const uint8_t CHANNEL_TO_LED[17][6]`

### Step 5: Handle MIDI Note State

For modes using MIDI notes (Notes to Drives):
```cpp
uint8_t noteVelocity = ctx->midiNotes[noteNumber];  // 0 = off, 1-127 = velocity
```

### Step 6: Handle Random Numbers

For modes using randomness (Flash Lights):
```cpp
// Use provided random seed for deterministic rendering
srand(ctx->randomSeed);
int randomValue = rand() % maxValue;
```

---

## Example Extraction: Color Sinusoid (case 6)

### Original (lights.ino lines 266-273):
```cpp
case 6: // Color Sinusoid
  for (int i = 0; i < NUM_LEDS; i++) {
    int phaseIndex = ((i * 64 / sinePeriod) + phaseShift) % 64;
    int sinVal = COLOR_PHASE[phaseIndex];
    int hueOffset = (sinVal * brightness) / 100;
    leds[i] = CHSV(ffHue * 2 + hueOffset, ffSat * 2, ffBright * 2);
  }
  break;
```

### Extracted (src/Modes/ColorSinusoid.cpp):
```cpp
#include "../../Shared/LightEngineAPI.h"
#include <string.h>

extern const int COLOR_PHASE[64];

void renderColorSinusoid(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    int phaseShift = ctx->params[13];     // phaseShift
    int brightness = ctx->params[14];     // brightness
    int sinePeriod = ctx->params[7];      // sinePeriod
    
    if (sinePeriod == 0) sinePeriod = 1;  // Prevent division by zero
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    for (int i = 0; i < numLeds; i++) {
        int phaseIndex = ((i * 64 / sinePeriod) + phaseShift) % 64;
        int sinVal = COLOR_PHASE[phaseIndex];
        int hueOffset = (sinVal * brightness) / 100;
        
        leds[i].h = hue + hueOffset;
        leds[i].s = sat;
        leds[i].v = bright;
    }
}
```

**Key Changes:**
1. ✅ MIDI variables → `ctx->params[]` with scaling
2. ✅ `NUM_LEDS` → `numLeds` parameter
3. ✅ `CHSV(h,s,v)` → `leds[i].h/s/v = ...`
4. ✅ Background buffer handling added
5. ✅ Division-by-zero protection added
6. ✅ `COLOR_PHASE` declared as extern const

---

## Remaining Mode Extraction Details

### Notes to Drives (case 0) - SPECIAL CASE

**Source**: `lights.ino` OnNoteOn lines 152-157
**File**: Create `src/Modes/NotesToDrives.cpp`
**Complexity**: Uses `CHANNEL_TO_LED` lookup table and MIDI note state

```cpp
// Pseudo-code structure:
void renderNotesToDrives(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;
    int sat = ctx->params[1] * 2;
    int bright = ctx->params[2] * 2;
    
    // Copy background
    if (ctx->background) memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    
    // Scan MIDI notes 36-52 (17 notes)
    for (int noteNum = 36; noteNum <= 52; noteNum++) {
        uint8_t velocity = ctx->midiNotes[noteNum];
        if (velocity > 0) {
            int channel = noteNum - 36;  // 0-16
            // Light up 6 LEDs for this channel using CHANNEL_TO_LED
            for (int i = 0; i < 6; i++) {
                int ledIdx = CHANNEL_TO_LED[channel][i];
                leds[ledIdx] = {hue, sat, bright};
            }
        }
    }
}
```

### Back and Forth (case 4)

**Source**: `lights.ino` lines 258-266
**File**: Create `src/Modes/BackAndForth.cpp`
**Complexity**: Simple block pattern with position and length

```cpp
// lights.ino reference:
// case 4: // Back and Forth
//   for (int i = ffLedStart; i < ffLedStart + ffLedLength; i++) {
//     leds[i % NUM_LEDS] = CHSV(ffHue * 2, ffSat * 2, ffBright * 2);
//   }
```

### Move startLED (case 5) - SPECIAL CASE

**Source**: `lights.ino` OnNoteOn lines 158-174
**File**: Create `src/Modes/MoveStartLED.cpp`
**Complexity**: Uses MIDI notes to increment `ffLedStart` parameter

⚠️ **Challenge**: This mode modifies parameters based on MIDI notes. The DLL renders but doesn't persist state. Solution:
- In the plugin, handle MIDI notes to update `ffLedStart` parameter value
- DLL only renders the current state (use params[3] as LED start position)

### Flash Lights (case 7)

**Source**: `lights.ino` lines 273-279
**File**: Create `src/Modes/FlashLights.cpp`
**Complexity**: Uses `random()` for flashing effect

```cpp
// lights.ino reference:
// case 7: // Flash Lights
//   for (int i = 0; i < NUM_LEDS; i++) {
//     int brightness = random(2) ? ffBright * 2 : 0;
//     leds[i] = CHSV(ffHue * 2, ffSat * 2, brightness);
//   }
```

**Random Seed Handling**:
```cpp
srand(ctx->randomSeed);  // Deterministic from JUCE plugin's frame counter
```

### Opposing Waves (case 9)

**Source**: `lights.ino` lines 295-311
**File**: Create `src/Modes/OpposingWaves.cpp`
**Complexity**: Variant of Ocean Waves with bidirectional pattern

---

## Build Integration

After creating each mode .cpp file:

1. **Add to CMakeLists.txt**:
   ```cmake
   set(MODE_SOURCES
       src/Modes/RainbowWheel.cpp
       src/Modes/MovingDots.cpp
       src/Modes/Comets.cpp
       src/Modes/OceanWaves.cpp
       src/Modes/Backgrounds.cpp
       # Add new modes here:
       src/Modes/ColorSinusoid.cpp
       src/Modes/NotesToDrives.cpp
       # ...
   )
   ```

2. **Verify compilation**:
   ```bash
   cmake -B build
   cmake --build build
   ```

3. **Test with plugin**: Load DLL and verify mode renders correctly

---

## Validation Checklist

For each extracted mode:

- [ ] Function signature matches: `void render**(HSV* leds, int numLeds, const RenderContext* ctx)`
- [ ] Parameters mapped correctly (verify against Metadata.cpp `usedParams`)
- [ ] MIDI values scaled 0-127 → 0-254 where needed
- [ ] Background buffer handled (if layering mode)
- [ ] Constants declared as extern
- [ ] Division-by-zero checks added
- [ ] Algorithm logic unchanged from lights.ino
- [ ] Compiled without errors
- [ ] Visual output matches Arduino hardware (pixel-perfect)

---

## Next Steps

1. Extract remaining 6 foreground modes
2. Create CMakeLists.txt with all sources
3. Build DLL for Windows/macOS/Linux
4. Implement LightEngineLoader.h in JUCE plugin
5. Create LightPreviewComponent for visual feedback
6. Integration testing with FL Studio

## Notes

- **Sync Marker**: Each mode has `===== BEGIN/END =====` comments marking Arduino sync points
- **Version Compatibility**: Add checksum validation of lights.ino mode code to detect drift
- **Future**: Consider auto-generating .cpp files from lights.ino using structured comments
