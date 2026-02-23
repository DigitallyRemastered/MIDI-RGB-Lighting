/*
  Light Engine Metadata
  
  Parameter and mode definitions extracted from lights.ino structured comments.
  This is the single source of truth for all parameter/mode information.
*/

#include "LightEngineAPI.h"
#include <string.h>

// ============================================================================
// Parameter Metadata (extracted from @param blocks in lights.ino)
// ============================================================================

typedef struct {
    const char* name;
    int cc;
    const char* layer;
    const char* tooltip;
} ParamMeta;

static const ParamMeta PARAMS[] = {
    // CC 1-5: Foreground color and position
    {"Hue", 1, "Foreground", "Sets color [roygbivmr]. Cyclic (min val = max val)"},
    {"Saturation", 2, "Foreground", "Sets saturation [white, chosen hue]"},
    {"Brightness", 3, "Foreground", "Sets intensity [LED off, LED on]"},
    {"Start", 4, "Foreground", "start position of line"},
    {"Length", 5, "Foreground", "length of line"},
    
    // CC 6: Foreground mode selector
    {"Foreground", 6, "", "Layering of effects"},
    
    // CC 7-8: Foreground parameters
    {"Number of Lines", 7, "Foreground", "Number of lines"},
    {"Color Amplitude", 8, "Shared", "color Amplitude for color sinusoid"},
    
    // CC 9: Background mode selector
    {"Background", 9, "", "Layering of effects"},
    
    // CC 10: Wave positioning
    {"Pan", 10, "Foreground", "Pan position for wave effects"},
    
    // CC 11-15: Background color and position
    {"Hue", 11, "Background", "Sets color [roygbivmr]. Cyclic (min val = max val)"},
    {"Saturation", 12, "Background", "Sets saturation [white, chosen hue]"},
    {"Brightness", 13, "Background", "Sets intensity [LED off, LED on]"},
    {"Start", 14, "Background", "start position of line"},
    {"Length", 15, "Background", "length of line"},
};

static const int PARAM_COUNT = sizeof(PARAMS) / sizeof(PARAMS[0]);

// ============================================================================
// Mode Metadata (extracted from @mode annotations in lights.ino)
// ============================================================================

typedef struct {
    int id;
    const char* name;
    const char* usedParamIndices;  // Comma-separated indices into PARAMS[]
} ModeMeta;

// Foreground modes (from OnNoteOn and OnControlChange switch statements)
static const ModeMeta FG_MODES[] = {
    {0, "Notes to Drives", "0,1,2"},                                    // ffHue, ffSat, ffBright
    {1, "Rainbow Wheel", "0,1,2"},                                      // ffHue, ffSat, ffBright
    {2, "Moving Dots", "0,1,2,3,4,6"},                                 // ffHue, ffSat, ffBright, ffLedStart, ffLedLength, lines
    {3, "Comets", "0,1,2,3,4,6"},                                      // ffHue, ffSat, ffBright, ffLedStart, ffLedLength, lines
    {4, "Back and Forth", "0,1,2,3,4"},                                // ffHue, ffSat, ffBright, ffLedStart, ffLedLength
    {5, "Move startLED with each note on event", "0,1,2,3,4,6"},       // ffHue, ffSat, ffBright, ffLedStart, ffLedLength, lines
    {6, "Color Sinusoid", "0,1,2,3,4,7"},                              // ffHue, ffSat, ffBright, ffLedStart, ffLedLength, cAmp
    {7, "Flash Lights", "0,1,2"},                                       // ffHue, ffSat, ffBright
    {8, "Ocean Waves", "0,1,2,4,9"},                                   // ffHue, ffSat, ffBright, ffLedLength, pan
    {9, "Opposing Waves", "0,1,2,4,9"},                                // ffHue, ffSat, ffBright, ffLedLength, pan
};

static const int FG_MODE_COUNT = sizeof(FG_MODES) / sizeof(FG_MODES[0]);

// Background modes (from updateBG switch statement)
static const ModeMeta BG_MODES[] = {
    {0, "Flat Color background", "10,11,12"},           // bgHue, bgSat, bgBright
    {1, "rainbow wheel background", "10,11,12"},        // bgHue, bgSat, bgBright
    {2, "Color Sinusoid", "10,11,12,13,14,7"},         // bgHue, bgSat, bgBright, bgLedStart, bgLedLength, cAmp
};

static const int BG_MODE_COUNT = sizeof(BG_MODES) / sizeof(BG_MODES[0]);

// ============================================================================
// API Implementations - Engine Info
// ============================================================================

LIGHT_ENGINE_EXPORT int getEngineVersion(void) {
    return 1;
}

LIGHT_ENGINE_EXPORT const char* getEngineName(void) {
    return "Default Light Engine v1.0";
}

// ============================================================================
// API Implementations - Parameter Metadata
// ============================================================================

LIGHT_ENGINE_EXPORT int getParameterCount(void) {
    return PARAM_COUNT;
}

LIGHT_ENGINE_EXPORT const char* getParameterName(int paramIndex) {
    if (paramIndex < 0 || paramIndex >= PARAM_COUNT) return "";
    return PARAMS[paramIndex].name;
}

LIGHT_ENGINE_EXPORT const char* getParameterTooltip(int paramIndex) {
    if (paramIndex < 0 || paramIndex >= PARAM_COUNT) return "";
    return PARAMS[paramIndex].tooltip;
}

LIGHT_ENGINE_EXPORT const char* getParameterLayer(int paramIndex) {
    if (paramIndex < 0 || paramIndex >= PARAM_COUNT) return "";
    return PARAMS[paramIndex].layer;
}

LIGHT_ENGINE_EXPORT int getParameterCC(int paramIndex) {
    if (paramIndex < 0 || paramIndex >= PARAM_COUNT) return 0;
    return PARAMS[paramIndex].cc;
}

// ============================================================================
// API Implementations - Mode Metadata
// ============================================================================

LIGHT_ENGINE_EXPORT int getForegroundModeCount(void) {
    return FG_MODE_COUNT;
}

LIGHT_ENGINE_EXPORT const char* getForegroundModeName(int modeId) {
    if (modeId < 0 || modeId >= FG_MODE_COUNT) return "";
    return FG_MODES[modeId].name;
}

LIGHT_ENGINE_EXPORT const char* getForegroundModeUsedParams(int modeId) {
    if (modeId < 0 || modeId >= FG_MODE_COUNT) return "";
    return FG_MODES[modeId].usedParamIndices;
}

LIGHT_ENGINE_EXPORT int getBackgroundModeCount(void) {
    return BG_MODE_COUNT;
}

LIGHT_ENGINE_EXPORT const char* getBackgroundModeName(int modeId) {
    if (modeId < 0 || modeId >= BG_MODE_COUNT) return "";
    return BG_MODES[modeId].name;
}

LIGHT_ENGINE_EXPORT const char* getBackgroundModeUsedParams(int modeId) {
    if (modeId < 0 || modeId >= BG_MODE_COUNT) return "";
    return BG_MODES[modeId].usedParamIndices;
}

// ============================================================================
// LED Configuration
// ============================================================================

LIGHT_ENGINE_EXPORT int getNumLEDs(void) {
    return 108;  // Matches NUM_LEDS in lights.ino
}
