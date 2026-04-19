/*
  Light Engine - Implementation
  
  All rendering logic, constants, and metadata in one place.
*/

#include "LightEngine.h"

#ifndef ARDUINO
    #include <cstdlib>
    #include <cstring>
    #include <cmath>
#else
    #include <math.h>
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
    {0, "Rainbow Wheel", "ffHue,ffSat,ffBright"},
    {1, "Moving Dots", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {2, "Comets", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {3, "Back and Forth", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength"},
    {4, "Move startLED with each note on event", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines"},
    {5, "Color Sinusoid", "ffHue,ffSat,ffBright,ffLedStart,ffLedLength,cAmp"},
    {6, "Flash Lights", "ffHue,ffSat,ffBright"}
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
    return 7;
}

const ModeInfo* getForegroundModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 7) return nullptr;
    return &FOREGROUND_MODES[modeId];
}

int getBackgroundModeCount() {
    return 3;
}

const ModeInfo* getBackgroundModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 3) return nullptr;
    return &BACKGROUND_MODES[modeId];
}

// Helper: Map parameter variable name to CC number
static int varNameToCC(const char* varName) {
    if (!varName || *varName == '\0') return -1;
    
    if (strcmp(varName, "ffHue") == 0) return 1;
    if (strcmp(varName, "ffSat") == 0) return 2;
    if (strcmp(varName, "ffBright") == 0) return 3;
    if (strcmp(varName, "ffLedStart") == 0) return 4;
    if (strcmp(varName, "ffLedLength") == 0) return 5;
    if (strcmp(varName, "lines") == 0) return 7;
    if (strcmp(varName, "cAmp") == 0) return 8;
    if (strcmp(varName, "pan") == 0) return 10;
    if (strcmp(varName, "bgHue") == 0) return 11;
    if (strcmp(varName, "bgSat") == 0) return 12;
    if (strcmp(varName, "bgBright") == 0) return 13;
    if (strcmp(varName, "bgLedStart") == 0) return 14;
    if (strcmp(varName, "bgLedLength") == 0) return 15;
    
    return -1;
}

// Convert mode's "uses" field to array of CC numbers
static int getModeParameterCCs(const char* uses, int* ccArray) {
    if (!uses || !ccArray) return 0;
    
    int count = 0;
    char buffer[256];
    strncpy(buffer, uses, sizeof(buffer) - 1);
    buffer[sizeof(buffer) - 1] = '\0';
    
    char* token = strtok(buffer, ",");
    while (token && count < 16) {
        // Trim whitespace
        while (*token == ' ' || *token == '\t') token++;
        
        int cc = varNameToCC(token);
        if (cc > 0) {
            ccArray[count++] = cc;
        }
        token = strtok(nullptr, ",");
    }
    
    return count;
}

int getForegroundModeParameterCount(int modeId) {
    const ModeInfo* info = getForegroundModeInfo(modeId);
    if (!info) return 0;
    
    int ccArray[16];
    return getModeParameterCCs(info->uses, ccArray);
}

int getForegroundModeParameter(int modeId, int index) {
    const ModeInfo* info = getForegroundModeInfo(modeId);
    if (!info) return -1;
    
    int ccArray[16];
    int count = getModeParameterCCs(info->uses, ccArray);
    
    if (index < 0 || index >= count) return -1;
    return ccArray[index];
}

int getBackgroundModeParameterCount(int modeId) {
    const ModeInfo* info = getBackgroundModeInfo(modeId);
    if (!info) return 0;
    
    int ccArray[16];
    return getModeParameterCCs(info->uses, ccArray);
}

int getBackgroundModeParameter(int modeId, int index) {
    const ModeInfo* info = getBackgroundModeInfo(modeId);
    if (!info) return -1;
    
    int ccArray[16];
    int count = getModeParameterCCs(info->uses, ccArray);
    
    if (index < 0 || index >= count) return -1;
    return ccArray[index];
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
    
    // Initialize helpers
    lineOffset = 0;
    frameCounter = 0;
    
    // Initialize tempo-sync state
    tempoBPM = 120.0f;          // Default 120 BPM
    
    // Initialize all waveforms to disabled
    for (int i = 0; i < 15; ++i) {
        waveforms[i].profile = WAVEFORM_SAWTOOTH;
        waveforms[i].amplitude = 0;
        waveforms[i].offset = 64;
        waveforms[i].phaseShift = 0;
        waveforms[i].period = 4;   // Default 4-beat period
        waveforms[i].direction = true;
        waveforms[i].enable = false;  // Disabled by default
    }
    
    // Initialize LEDs to black (off)
    memset(leds, 0, numLeds * sizeof(HSVColor));
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
    
    // Mode-specific state changes
    if (ffMode == 4) {  // Move StartLED mode
        ffLedStart += 1;
        if (ffLedStart >= 127) ffLedStart = 0;
    }
}

void LightEngine::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    // Clear note state
    if (note < 128) {
        activeNotes[note] = 0;
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
// State Serialization
// ============================================================================

size_t LightEngine::serializeState(uint8_t* buf, size_t bufLen) const {
    if (!buf || bufLen < STATE_SIZE) return 0;

    size_t i = 0;

    // Header
    buf[i++] = 0x4C;  // magic
    buf[i++] = 0x01;  // version

    // CC parameters 1-15 (0-127 each)
    for (int cc = 1; cc <= 15; ++cc) {
        buf[i++] = (uint8_t)getCC(cc);
    }

    // tempoBPM stored as uint16 = (int)(bpm * 10), big-endian
    uint16_t bpmFixed = (uint16_t)(tempoBPM * 10.0f + 0.5f);
    buf[i++] = (uint8_t)(bpmFixed >> 8);
    buf[i++] = (uint8_t)(bpmFixed & 0xFF);

    // Waveform configs: 15 × 8 bytes
    for (int w = 0; w < 15; ++w) {
        buf[i++] = waveforms[w].profile;
        buf[i++] = waveforms[w].amplitude;
        buf[i++] = waveforms[w].offset;
        buf[i++] = (uint8_t)(waveforms[w].phaseShift & 0xFF);
        buf[i++] = (uint8_t)(waveforms[w].phaseShift >> 8);
        buf[i++] = waveforms[w].period;
        buf[i++] = waveforms[w].direction ? 1 : 0;
        buf[i++] = waveforms[w].enable    ? 1 : 0;
    }

    return i;  // == STATE_SIZE
}

bool LightEngine::deserializeState(const uint8_t* buf, size_t len) {
    if (!buf || len < STATE_SIZE) return false;
    if (buf[0] != 0x4C || buf[1] != 0x01) return false;

    size_t i = 2;

    // CC parameters 1-15
    for (int cc = 1; cc <= 15; ++cc) {
        setCC(cc, buf[i++]);
    }

    // tempoBPM
    uint16_t bpmFixed = ((uint16_t)buf[i] << 8) | buf[i + 1];
    i += 2;
    tempoBPM = bpmFixed / 10.0f;

    // Waveform configs
    for (int w = 0; w < 15; ++w) {
        waveforms[w].profile   = buf[i++];
        waveforms[w].amplitude = buf[i++];
        waveforms[w].offset    = buf[i++];
        waveforms[w].phaseShift = (uint16_t)buf[i] | ((uint16_t)buf[i + 1] << 8);
        i += 2;
        waveforms[w].period    = buf[i++];
        waveforms[w].direction = buf[i++] != 0;
        waveforms[w].enable    = buf[i++] != 0;
    }

    return true;
}

// ============================================================================
// Rendering Pipeline
// ============================================================================

void LightEngine::render() {
    // Increment frame counter
    frameCounter++;
    
    // Apply waveform modulation to parameters (if enabled)
    applyWaveformModulation();
    
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
        case 0: renderRainbowWheel(); break;
        case 1: renderMovingDots(); break;
        case 2: renderComets(); break;
        case 3: renderBackAndForth(); break;
        case 4: renderMoveStartLED(); break;
        case 5: renderColorSinusoid(); break;
        case 6: renderFlashLights(); break;
        default: renderRainbowWheel(); break;
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

// ============================================================================
// Waveform Modulation Implementation
// ============================================================================

float LightEngine::evaluateWaveform(int profile, float phase, bool direction) const {
    // phase is 0.0-1.0 representing position in cycle
    // Returns 0.0-1.0 waveform value
    
    // Flip phase if direction is reversed
    if (!direction) {
        phase = 1.0f - phase;
    }
    
    switch (profile) {
        case WAVEFORM_SAWTOOTH:
            // Linear ramp 0→1
            return phase;
        
        case WAVEFORM_TRIANGLE:
            // Triangle wave: ramps 0→1→0
            if (phase < 0.5f) {
                return 2.0f * phase;  // Rising edge
            } else {
                return 2.0f * (1.0f - phase);  // Falling edge
            }
        
        case WAVEFORM_SQUARE:
            // Square wave: 0 for first half, 1 for second half
            return (phase < 0.5f) ? 0.0f : 1.0f;
        
        case WAVEFORM_SINE:
            // Use COLOR_PHASE lookup table (sine wave approximation)
            // COLOR_PHASE is a 64-entry table representing one sine cycle
            {
                int idx = (int)(phase * 64.0f) % 64;
                // COLOR_PHASE ranges from -100 to +100, normalize to 0.0-1.0
                float sineValue = (COLOR_PHASE[idx] + 100.0f) / 200.0f;
                return sineValue;
            }
        
        default:
            return phase;  // Fallback to sawtooth
    }
}

void LightEngine::applyWaveformModulation() {
    // For each CC parameter (1-15), check if waveform is enabled
    for (int cc = 1; cc <= 15; ++cc) {
        const WaveformConfig& wf = waveforms[cc - 1];  // CC1 is index 0
        
        if (!wf.enable)
            continue;  // Skip disabled waveforms
        
        // Calculate frames per period for this waveform
        // period is in beats, convert to frames: (beats / BPM) * 60 * fps
        float framesPerPeriod = (wf.period * 60.0f * FRAME_RATE) / tempoBPM;
        
        if (framesPerPeriod < 1.0f)
            framesPerPeriod = 1.0f;  // Avoid divide by zero
        
        // Calculate phase (0.0-1.0) including phase shift
        float shiftedFrame = fmod((float)frameCounter + (float)wf.phaseShift, framesPerPeriod);
        float phase = shiftedFrame / framesPerPeriod;
        
        // Evaluate waveform
        float waveValue = evaluateWaveform(wf.profile, phase, wf.direction);
        
        // Calculate modulated value: offset + (waveValue * amplitude)
        // waveValue is 0.0-1.0, amplitude is 0-127
        int modulatedValue = wf.offset + (int)(waveValue * wf.amplitude);
        
        // Clamp to 0-127 MIDI range
        if (modulatedValue < 0) modulatedValue = 0;
        if (modulatedValue > 127) modulatedValue = 127;
        
        // Apply to the appropriate parameter
        // Note: CC1-3, 11-13 are scaled 0-254, others are 0-127
        switch (cc) {
            case 1: ffHue = modulatedValue * 2; break;        // Scale to 0-254
            case 2: ffSat = modulatedValue * 2; break;
            case 3: ffBright = modulatedValue * 2; break;
            case 4: ffLedStart = modulatedValue; break;       // Keep 0-127
            case 5: ffLedLength = modulatedValue; break;
            case 6: ffMode = modulatedValue; break;
            case 7: lines = modulatedValue; break;
            case 8: cAmp = modulatedValue; break;
            case 9: bgMode = modulatedValue; break;
            case 10: pan = modulatedValue; break;
            case 11: bgHue = modulatedValue * 2; break;
            case 12: bgSat = modulatedValue * 2; break;
            case 13: bgBright = modulatedValue * 2; break;
            case 14: bgLedStart = modulatedValue; break;
            case 15: bgLedLength = modulatedValue; break;
        }
    }
}

// ============================================================================
// SysEx Handler
// ============================================================================

void LightEngine::handleSysEx(const uint8_t* data, uint16_t length) {
    // Validate minimum message length and manufacturer ID
    // Format: [F0, 7D, msgType, ...data..., F7] (standard MIDI)
    //     or: [F0, cable/channel, 7D, msgType, ...data..., F7] (USB MIDI with extra byte)
    if (length < 4)  // Minimum: F0, 7D, type, F7
        return;
    
    if (data[0] != 0xF0)  // Check for SysEx start
        return;
    
    if (data[length - 1] != 0xF7)  // Check for SysEx end
        return;
    
    // Detect format: check if byte 1 or byte 2 contains our manufacturer ID (0x7D)
    int offset = 0;  // Byte offset to manufacturer ID
    if (data[1] == 0x7D) {
        offset = 1;  // Standard MIDI format
    } else if (length >= 5 && data[2] == 0x7D) {
        offset = 2;  // USB MIDI format with extra byte
    } else {
        return;  // Unknown format - not our message
    }
    
    uint8_t msgType = data[offset + 1];
    
    switch (msgType) {
        case 0x01:  // Tempo update
            // Format: [F0, 7D, 01, bpm_msb, bpm_lsb, F7]
            if (length >= offset + 4) {  // Need manufacturer ID, msgType, and 2 data bytes
                uint16_t bpmScaled = (data[offset + 2] << 7) | data[offset + 3];  // 14-bit BPM * 10
                tempoBPM = bpmScaled / 10.0f;
            }
            break;
        
        case 0x02:  // Waveform parameter config
            // Format: [F0, 7D, 02, ccNumber, paramEnum, value, F7]
            if (length >= offset + 5) {  // Need manufacturer ID, msgType, and 3 data bytes
                uint8_t ccNumber = data[offset + 2];   // 1-15
                uint8_t paramEnum = data[offset + 3];  // 0-7
                uint8_t value = data[offset + 4];      // 0-127
                
                if (ccNumber >= 1 && ccNumber <= 15) {
                    WaveformConfig& wf = waveforms[ccNumber - 1];
                    
                    switch (paramEnum) {
                        case 0: wf.profile = value & 0x03; break;  // 0-3
                        case 1: wf.amplitude = value; break;       // 0-127
                        case 2: wf.offset = value; break;          // 0-127
                        case 3: // phaseShift low 7 bits
                            wf.phaseShift = (wf.phaseShift & 0x3F80) | value;
                            break;
                        case 4: wf.period = (value > 0) ? value : 1; break;  // 1-127 beats
                        case 5: wf.direction = (value > 0); break;           // 0=reverse, 1=forward
                        case 6: wf.enable = (value > 0); break;              // 0=disabled, 1=enabled
                        case 7: // phaseShift high 7 bits
                            wf.phaseShift = (value << 7) | (wf.phaseShift & 0x7F);
                            break;
                    }
                }
            }
            break;
        
        case 0x03:  // Playback start (reset frameCounter)
            // Format: [F0, 7D, 03, F7]
            frameCounter = 0;
            break;

        case 0x05:  // Enable waveform with full config (atomic)
            // Format: [F0, 7D, 05, cc, profile, amplitude, offset, phaseShiftL, phaseShiftH, period, direction, F7]
            // Sends all waveform parameters + enable=true in a single message to avoid UDP split.
            if (length >= offset + 11) {
                uint8_t ccNum     = data[offset + 2];
                uint8_t profile   = data[offset + 3];
                uint8_t amplitude = data[offset + 4];
                uint8_t wfOffset  = data[offset + 5];
                uint8_t phaseL    = data[offset + 6];
                uint8_t phaseH    = data[offset + 7];
                uint8_t period    = data[offset + 8];
                uint8_t direction = data[offset + 9];  // 0=reverse, 1=forward

                if (ccNum >= 1 && ccNum <= 15) {
                    WaveformConfig& wf = waveforms[ccNum - 1];
                    wf.profile    = profile & 0x03;
                    wf.amplitude  = amplitude;
                    wf.offset     = wfOffset;
                    wf.phaseShift = ((uint16_t)phaseH << 7) | phaseL;
                    wf.period     = (period > 0) ? period : 1;
                    wf.direction  = (direction > 0);
                    wf.enable     = true;
                }
            }
            break;

        default:
            // Unknown message type, ignore
            break;
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

void lightEngine_handleSysEx(void* engine, const uint8_t* data, uint16_t length) {
    static_cast<LightEngine*>(engine)->handleSysEx(data, length);
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

int lightEngine_getForegroundModeParameterCount(int modeId) {
    return getForegroundModeParameterCount(modeId);
}

int lightEngine_getForegroundModeParameter(int modeId, int index) {
    return getForegroundModeParameter(modeId, index);
}

int lightEngine_getBackgroundModeParameterCount(int modeId) {
    return getBackgroundModeParameterCount(modeId);
}

int lightEngine_getBackgroundModeParameter(int modeId, int index) {
    return getBackgroundModeParameter(modeId, index);
}

} // extern "C"

#endif // !ARDUINO
