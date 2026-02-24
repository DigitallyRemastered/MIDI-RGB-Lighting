/*
  Light Engine - Implementation
  
  All rendering logic, constants, and metadata in one place.
*/

#include "LightEngine.h"

#ifndef ARDUINO
    #include <cstdlib>
    #include <cstring>
#endif

// ============================================================================
// Constants - Shared Lookup Tables
// ============================================================================

// Sine wave lookup table for color sinusoid mode
const int LightEngine::COLOR_PHASE[64] = {
    10, 20, 29, 38, 47, 56, 63, 71, 77, 83, 88, 92, 96, 98, 100, 100,
    100, 98, 96, 92, 88, 83, 77, 71, 63, 56, 47, 38, 29, 20, 10, 0,
    -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100, -100,
    -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10, 0
};

// Mirror map for ocean waves (top and bottom LED strips)
const uint8_t LightEngine::TOP_BOTTOM_MIRROR_MAP[48] = {
    23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72
};

// MIDI channel to LED mapping for Notes to Drives mode (108 LED floppy drive layout)
const uint8_t LightEngine::CHANNEL_TO_LED[17][6] = {
    {0, 0, 0, 0, 0, 0},           // Channel 0 (unused)
    {19, 20, 21, 22, 23, 24},     // Channel 1
    {29, 30, 31, 32, 33, 34},     // Channel 2
    {13, 14, 15, 16, 17, 18},     // Channel 3
    {35, 36, 37, 38, 39, 40},     // Channel 4
    {7, 8, 9, 10, 11, 12},        // Channel 5
    {41, 42, 43, 44, 45, 46},     // Channel 6
    {1, 2, 3, 4, 5, 6},           // Channel 7
    {47, 48, 49, 50, 51, 52},     // Channel 8
    {73, 74, 75, 76, 77, 78},     // Channel 9
    {83, 84, 85, 86, 87, 88},     // Channel 10
    {67, 68, 69, 70, 71, 72},     // Channel 11
    {89, 90, 91, 92, 93, 94},     // Channel 12
    {61, 62, 63, 64, 65, 66},     // Channel 13
    {95, 96, 97, 98, 99, 100},    // Channel 14
    {55, 56, 57, 58, 59, 60},     // Channel 15
    {101, 102, 103, 104, 105, 106} // Channel 16
};

// ============================================================================
// Parameter Metadata
// ============================================================================

static const ParameterInfo PARAMETER_TABLE[] = {
    {1, "Hue", "Foreground", "Sets color [roygbivmr]. Cyclic (min val = max val)"},
    {2, "Saturation", "Foreground", "Sets saturation [white, chosen hue]"},
    {3, "Brightness", "Foreground", "Sets intensity [LED off, LED on]"},
    {4, "Start", "Foreground", "Start position of line"},
    {5, "Length", "Foreground", "Length of line"},
    {6, "Foreground", "Foreground", "Foreground mode selector"},
    {7, "Number of Lines", "Foreground", "Number of lines"},
    {8, "Color Amplitude", "Shared", "Color amplitude for color sinusoid"},
    {9, "Background", "Background", "Background mode selector"},
    {10, "Pan", "Foreground", "Pan position for wave effects"},
    {11, "Hue", "Background", "Sets color [roygbivmr]. Cyclic (min val = max val)"},
    {12, "Saturation", "Background", "Sets saturation [white, chosen hue]"},
    {13, "Brightness", "Background", "Sets intensity [LED off, LED on]"},
    {14, "Start", "Background", "Start position of line"},
    {15, "Length", "Background", "Length of line"}
};

static const ModeInfo FOREGROUND_MODES[] = {
    {0, "Notes to Drives", "ffHue,ffSat,ffBright"},
    {1, "Rainbow Wheel", "ffHue,ffSat,ffBright"},
    {2, "Moving Dots", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {3, "Comets", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {4, "Back and Forth", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength"},
    {5, "Move startLED with each note on event", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {6, "Color Sinusoid", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,cAmp"},
    {7, "Flash Lights", "ffHue,ffSat,ffBright"},
    {8, "Ocean Waves", "ffHue,ffSat,ffBright,ffLedLength,pan"},
    {9, "Opposing Waves", "ffHue,ffSat,ffBright,ffLedLength,pan"}
};

static const ModeInfo BACKGROUND_MODES[] = {
    {0, "Flat Color background", "bgHue,bgSat,bgBright"},
    {1, "Rainbow wheel background", "bgHue,bgSat,bgBright"},
    {2, "Color Sinusoid", "bgHue,bgSat,bgBright,bgLedStart,bgLedLength,cAmp"}
};

const ParameterInfo* getParameterInfo(int ccNumber) {
    if (ccNumber < 1 || ccNumber > 15) return nullptr;
    return &PARAMETER_TABLE[ccNumber - 1];
}

int getAllParameters(const ParameterInfo** outArray) {
    *outArray = PARAMETER_TABLE;
    return 15;
}

int getForegroundModeCount() {
    return 10;
}

const ModeInfo* getForegroundModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 10) return nullptr;
    return &FOREGROUND_MODES[modeId];
}

int getBackgroundModeCount() {
    return 3;
}

const ModeInfo* getBackgroundModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 3) return nullptr;
    return &BACKGROUND_MODES[modeId];
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

LightEngine::LightEngine(int numLeds) : numLeds(numLeds) {
    // Validate LED count for hardware-specific layout
    if (numLeds != 108) {
        #ifndef ARDUINO
        // Note: Could log warning or error here
        #endif
    }
    
    // Allocate LED buffers
    leds = new HSVColor[numLeds];
    background = new HSVColor[numLeds];
    
    // Initialize parameters to defaults (matching lights.ino)
    ffHue = 0;
    ffSat = 200;        // Not 0-127 range, already scaled to 0-254
    ffBright = 200;
    ffLedStart = 0;
    ffLedLength = 0;
    ffMode = 0;
    lines = 0;
    cAmp = 0;
    bgMode = 0;
    pan = 64;
    bgHue = 0;
    bgSat = 200;
    bgBright = 0;
    bgLedStart = 0;
    bgLedLength = 0;
    
    // Initialize note state
    memset(activeNotes, 0, sizeof(activeNotes));
    memset(currentNote, 0, sizeof(currentNote));
    
    // Initialize helpers
    lineOffset = 0;
    frameCounter = 0;
    
    // Initialize LEDs to checkerboard pattern
    for (int i = 0; i < numLeds; i++) {
        if (i % 2 == 0) {
            leds[i].h = 80;
            leds[i].s = 200;
            leds[i].v = ffBright;
        } else {
            leds[i].h = 100;
            leds[i].s = 200;
            leds[i].v = ffBright;
        }
    }
    memset(background, 0, numLeds * sizeof(HSVColor));
}

LightEngine::~LightEngine() {
    delete[] leds;
    delete[] background;
}

// ============================================================================
// MIDI Event Handlers
// ============================================================================

void LightEngine::handleControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    // Update parameters based on CC number
    switch (control) {
        case 1: ffHue = value * 2; break;        // Scale to 0-254
        case 2: ffSat = value * 2; break;
        case 3: ffBright = value * 2; break;
        case 4: ffLedStart = value; break;       // Keep 0-127
        case 5: ffLedLength = value; break;
        case 6: ffMode = value; break;
        case 7: lines = value; break;
        case 8: cAmp = value; break;
        case 9: bgMode = value; break;
        case 10: pan = value; break;
        case 11: bgHue = value * 2; break;
        case 12: bgSat = value * 2; break;
        case 13: bgBright = value * 2; break;
        case 14: bgLedStart = value; break;
        case 15: bgLedLength = value; break;
    }
}

void LightEngine::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Store note state
    if (note < 128) {
        activeNotes[note] = velocity;
    }
    
    // Per-channel tracking for Notes to Drives mode
    if (channel >= 1 && channel <= 16) {
        currentNote[channel] = note;
    }
    
    // Mode-specific state changes
    if (ffMode == 5) {  // Move StartLED mode
        ffLedStart += 1;
        if (ffLedStart >= 127) ffLedStart = 0;
    }
}

void LightEngine::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Clear note state
    if (note < 128) {
        activeNotes[note] = 0;
    }
    
    // Clear per-channel tracking
    if (channel >= 1 && channel <= 16) {
        if (currentNote[channel] == note) {
            currentNote[channel] = 0;
        }
    }
}

// ============================================================================
// State Access
// ============================================================================

int LightEngine::getCC(int ccNumber) const {
    switch (ccNumber) {
        case 1: return ffHue / 2;
        case 2: return ffSat / 2;
        case 3: return ffBright / 2;
        case 4: return ffLedStart;
        case 5: return ffLedLength;
        case 6: return ffMode;
        case 7: return lines;
        case 8: return cAmp;
        case 9: return bgMode;
        case 10: return pan;
        case 11: return bgHue / 2;
        case 12: return bgSat / 2;
        case 13: return bgBright / 2;
        case 14: return bgLedStart;
        case 15: return bgLedLength;
        default: return 0;
    }
}

void LightEngine::setCC(int ccNumber, int value) {
    handleControlChange(0, ccNumber, value);
}

// ============================================================================
// Rendering Pipeline
// ============================================================================

void LightEngine::render() {
    // Increment frame counter for randomness
    frameCounter++;
    
    // Step 1: Render background
    updateBackground();
    
    // Step 2: Render foreground (composites over background)
    renderForeground();
}

void LightEngine::updateBackground() {
    switch (bgMode) {
        case 0: renderFlatBackground(); break;
        case 1: renderRainbowBackground(); break;
        case 2: renderSinusoidBackground(); break;
        default: renderFlatBackground(); break;
    }
    
    // Copy background to main buffer
    memcpy(leds, background, numLeds * sizeof(HSVColor));
}

void LightEngine::renderForeground() {
    switch (ffMode) {
        case 0: renderNotesToDrives(); break;
        case 1: renderRainbowWheel(); break;
        case 2: renderMovingDots(); break;
        case 3: renderComets(); break;
        case 4: renderBackAndForth(); break;
        case 5: renderMoveStartLED(); break;
        case 6: renderColorSinusoid(); break;
        case 7: renderFlashLights(); break;
        case 8: renderOceanWaves(); break;
        case 9: renderOpposingWaves(); break;
    }
}

// ============================================================================
// Utility Functions
// ============================================================================

inline void LightEngine::setLED(int index, uint8_t h, uint8_t s, uint8_t v) {
    if (index >= 0 && index < numLeds) {
        leds[index].h = h;
        leds[index].s = s;
        leds[index].v = v;
    }
}

inline void LightEngine::copyBackground() {
    memcpy(leds, background, numLeds * sizeof(HSVColor));
}

// ============================================================================
// Background Modes
// ============================================================================

void LightEngine::renderFlatBackground() {
    for (int i = 0; i < numLeds; i++) {
        background[i].h = bgHue;
        background[i].s = bgSat;
        background[i].v = bgBright;
    }
}

void LightEngine::renderRainbowBackground() {
    int rainbowInc = 255 / numLeds;
    for (int i = 0; i < numLeds; i++) {
        background[i].h = (bgHue + i * rainbowInc) % 256;
        background[i].s = bgSat;
        background[i].v = bgBright;
    }
}

void LightEngine::renderSinusoidBackground() {
    if (bgLedLength == 0) {
        renderFlatBackground();
        return;
    }
    
    for (int i = 0; i < numLeds; i++) {
        int cphaseIdx = ((i + bgLedStart) * 64 / bgLedLength) % 64;
        int h = (256 + bgHue + cAmp * COLOR_PHASE[cphaseIdx] / 100) % 256;
        background[i].h = h;
        background[i].s = bgSat;
        background[i].v = bgBright;
    }
}

// ============================================================================
// Foreground Modes
// ============================================================================

void LightEngine::renderNotesToDrives() {
    // Check all active notes and light up corresponding drives
    for (int ch = 1; ch <= 16; ch++) {
        if (currentNote[ch] != 0) {
            // Note is active on this channel - light it up
            for (int i = 0; i < 6; i++) {
                int ledIdx = CHANNEL_TO_LED[ch][i];
                setLED(ledIdx, ffHue, ffSat, ffBright);
            }
        } else {
            // Note off - show checkerboard pattern
            for (int i = 0; i < 6; i++) {
                int ledIdx = CHANNEL_TO_LED[ch][i];
                if (i % 2 == 0) {
                    setLED(ledIdx, 80, 200, ffBright);
                } else {
                    setLED(ledIdx, 100, 200, ffBright);
                }
            }
        }
    }
}

void LightEngine::renderRainbowWheel() {
    int rainbowInc = 255 / numLeds;
    for (int i = 0; i < numLeds; i++) {
        setLED(i, (ffHue + i * rainbowInc) % 256, ffSat, ffBright);
    }
}

void LightEngine::renderMovingDots() {
    if (lines == 0) return;
    
    lineOffset = numLeds / lines;
    
    for (int line = 0; line < lines; line++) {
        for (int led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
            int idx = (led + line * lineOffset) % numLeds;
            setLED(idx, ffHue, ffSat, ffBright);
        }
    }
}

void LightEngine::renderComets() {
    if (lines == 0 || ffLedLength == 0) return;
    
    lineOffset = numLeds / lines;
    
    for (int line = 0; line < lines; line++) {
        int count = ffLedLength - 1;
        for (int led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
            int idx = (led + line * lineOffset) % numLeds;
            int brightness = ffBright * (ffLedLength - count) / ffLedLength;
            setLED(idx, ffHue, ffSat, brightness);
            count--;
        }
    }
}

void LightEngine::renderBackAndForth() {
    if (ffLedLength == 0) return;
    
    for (int block = 0; block < numLeds; block += 2 * ffLedLength) {
        for (int led = 0; led < ffLedLength; led++) {
            int idx = (ffLedStart * ffLedLength + block + led) % numLeds;
            setLED(idx, ffHue, ffSat, ffBright);
        }
    }
}

void LightEngine::renderMoveStartLED() {
    if (lines == 0) return;
    
    lineOffset = numLeds / lines;
    int rainbowInc = 255 / numLeds;
    
    for (int line = 0; line < lines; line++) {
        for (int led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
            int idx = (led + line * lineOffset) % numLeds;
            int hue = (ffHue + led * rainbowInc) % 256;
            setLED(idx, hue, ffSat, ffBright);
        }
    }
}

void LightEngine::renderColorSinusoid() {
    if (ffLedLength == 0) return;
    
    for (int i = 0; i < numLeds; i++) {
        int cphaseIdx = ((i + ffLedStart) * 64 / ffLedLength) % 64;
        int h = (256 + ffHue + cAmp * COLOR_PHASE[cphaseIdx] / 100) % 256;
        setLED(i, h, ffSat, ffBright);
    }
}

void LightEngine::renderFlashLights() {
    // Use frame counter as random seed
    #ifdef ARDUINO
        randomSeed(frameCounter);
        int randomLed = random(numLeds);
    #else
        srand(frameCounter);
        int randomLed = rand() % numLeds;
    #endif
    
    setLED(randomLed, ffHue, ffSat, ffBright);
}

void LightEngine::renderOceanWaves() {
    int tMiddle = (numLeds / 2 - 1) * pan / 127 + numLeds / 4;
    int amp = ffLedLength / 2;
    
    // Clamp amplitude
    if (tMiddle - amp <= 24) {
        amp = tMiddle - 24;
    } else if (tMiddle + amp > 71) {
        amp = 71 - tMiddle;
    }
    
    if (amp <= 0) return;
    
    // Render wave
    for (int p = 0; p < amp; p++) {
        int brightness = ffBright * (amp - p) / amp;
        
        setLED(tMiddle + p, ffHue, ffSat, brightness);
        setLED(tMiddle - p, ffHue, ffSat, brightness);
        
        // Mirror to bottom
        if ((tMiddle + p - 24) >= 0 && (tMiddle + p - 24) < 48) {
            int mirrorIdx = TOP_BOTTOM_MIRROR_MAP[tMiddle + p - 24];
            setLED(mirrorIdx, ffHue, ffSat, brightness);
        }
        if ((tMiddle - p - 24) >= 0 && (tMiddle - p - 24) < 48) {
            int mirrorIdx = TOP_BOTTOM_MIRROR_MAP[tMiddle - p - 24];
            setLED(mirrorIdx, ffHue, ffSat, brightness);
        }
    }
}

void LightEngine::renderOpposingWaves() {
    int tMiddle = (numLeds / 2 - 1) * pan / 127 + numLeds / 4;
    int amp = ffLedLength / 2;
    
    // Clamp amplitude
    if (tMiddle - amp <= 24) {
        amp = tMiddle - 23;
    } else if (tMiddle + amp > 71) {
        amp = 71 - tMiddle;
    }
    
    if (amp <= 0) return;
    
    // Render opposing waves
    for (int p = 0; p <= amp; p++) {
        int brightness = ffBright * (amp - p) / amp;
        
        setLED(tMiddle + p, ffHue, ffSat, brightness);
        setLED(tMiddle - p, ffHue, ffSat, brightness);
        
        // Opposing wave
        int oppositeIdx1 = (numLeds - tMiddle) + p - 24;
        int oppositeIdx2 = (numLeds - tMiddle) - p - 24;
        
        if (oppositeIdx1 >= 0 && oppositeIdx1 < 48) {
            int mirrorIdx = TOP_BOTTOM_MIRROR_MAP[oppositeIdx1];
            setLED(mirrorIdx, ffHue, ffSat, brightness);
        }
        if (oppositeIdx2 >= 0 && oppositeIdx2 < 48) {
            int mirrorIdx = TOP_BOTTOM_MIRROR_MAP[oppositeIdx2];
            setLED(mirrorIdx, ffHue, ffSat, brightness);
        }
    }
}

// ============================================================================
// C API Implementation (Windows DLL)
// ============================================================================

#ifndef ARDUINO

extern "C" {

void* lightEngine_create(int numLeds) {
    return new LightEngine(numLeds);
}

void lightEngine_destroy(void* engine) {
    delete static_cast<LightEngine*>(engine);
}

void lightEngine_handleControlChange(void* engine, uint8_t channel, uint8_t control, uint8_t value) {
    static_cast<LightEngine*>(engine)->handleControlChange(channel, control, value);
}

void lightEngine_handleNoteOn(void* engine, uint8_t channel, uint8_t note, uint8_t velocity) {
    static_cast<LightEngine*>(engine)->handleNoteOn(channel, note, velocity);
}

void lightEngine_handleNoteOff(void* engine, uint8_t channel, uint8_t note, uint8_t velocity) {
    static_cast<LightEngine*>(engine)->handleNoteOff(channel, note, velocity);
}

void lightEngine_render(void* engine) {
    static_cast<LightEngine*>(engine)->render();
}

const HSVColor* lightEngine_getLEDs(void* engine) {
    return static_cast<LightEngine*>(engine)->getLEDs();
}

int lightEngine_getNumLEDs(void* engine) {
    return static_cast<LightEngine*>(engine)->getNumLEDs();
}

int lightEngine_getCC(void* engine, int ccNumber) {
    return static_cast<LightEngine*>(engine)->getCC(ccNumber);
}

void lightEngine_setCC(void* engine, int ccNumber, int value) {
    static_cast<LightEngine*>(engine)->setCC(ccNumber, value);
}

const char* lightEngine_getEngineName() {
    return "Light Engine v1.0";
}

int lightEngine_getEngineVersion() {
    return 1;
}

int lightEngine_getParameterCount() {
    return 15;
}

const char* lightEngine_getParameterName(int ccNumber) {
    const ParameterInfo* info = getParameterInfo(ccNumber);
    return info ? info->name : "";
}

const char* lightEngine_getParameterLayer(int ccNumber) {
    const ParameterInfo* info = getParameterInfo(ccNumber);
    return info ? info->layer : "";
}

const char* lightEngine_getParameterTooltip(int ccNumber) {
    const ParameterInfo* info = getParameterInfo(ccNumber);
    return info ? info->tooltip : "";
}

int lightEngine_getForegroundModeCount() {
    return getForegroundModeCount();
}

const char* lightEngine_getForegroundModeName(int modeId) {
    const ModeInfo* info = getForegroundModeInfo(modeId);
    return info ? info->name : "";
}

int lightEngine_getBackgroundModeCount() {
    return getBackgroundModeCount();
}

const char* lightEngine_getBackgroundModeName(int modeId) {
    const ModeInfo* info = getBackgroundModeInfo(modeId);
    return info ? info->name : "";
}

} // extern "C"

#endif // !ARDUINO
