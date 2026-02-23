/*
  Light Engine API Implementation
  
  Main entry points and mode dispatchers.
*/

#include "LightEngineAPI.h"
#include <string.h>
#include <stdio.h>

// External constants
extern "C" {
    extern const int COLOR_PHASE[64];
    extern const uint8_t TOP_BOTTOM_MIRROR_MAP[48];
    extern const uint8_t CHANNEL_TO_LED[17][6];
}

// Forward declarations for mode rendering functions
extern void renderNotesToDrives(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderRainbowWheel(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderMovingDots(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderComets(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderBackAndForth(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderMoveStartLED(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderColorSinusoid(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderFlashLights(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderOceanWaves(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderOpposingWaves(HSV* leds, int numLeds, const RenderContext* ctx);

extern void renderFlatBackground(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderRainbowBackground(HSV* leds, int numLeds, const RenderContext* ctx);
extern void renderSinusoidBackground(HSV* leds, int numLeds, const RenderContext* ctx);

// ============================================================================
// Rendering Dispatchers
// ============================================================================

LIGHT_ENGINE_EXPORT void renderForeground(int modeId, HSV* leds, int numLeds, const RenderContext* ctx) {
    if (!leds || !ctx) return;
    
    switch (modeId) {
        case 0: renderNotesToDrives(leds, numLeds, ctx); break;
        case 1: renderRainbowWheel(leds, numLeds, ctx); break;
        case 2: renderMovingDots(leds, numLeds, ctx); break;
        case 3: renderComets(leds, numLeds, ctx); break;
        case 4: renderBackAndForth(leds, numLeds, ctx); break;
        case 5: renderMoveStartLED(leds, numLeds, ctx); break;
        case 6: renderColorSinusoid(leds, numLeds, ctx); break;
        case 7: renderFlashLights(leds, numLeds, ctx); break;
        case 8: renderOceanWaves(leds, numLeds, ctx); break;
        case 9: renderOpposingWaves(leds, numLeds, ctx); break;
        default: break;
    }
}

LIGHT_ENGINE_EXPORT void renderBackground(int modeId, HSV* leds, int numLeds, const RenderContext* ctx) {
    if (!leds || !ctx) return;
    
    switch (modeId) {
        case 0: renderFlatBackground(leds, numLeds, ctx); break;
        case 1: renderRainbowBackground(leds, numLeds, ctx); break;
        case 2: renderSinusoidBackground(leds, numLeds, ctx); break;
        default: renderFlatBackground(leds, numLeds, ctx); break;
    }
}

// ============================================================================
// CSV Export
// ============================================================================

extern int getParameterCount(void);
extern const char* getParameterName(int paramIndex);
extern int getParameterCC(int paramIndex);
extern const char* getParameterLayer(int paramIndex);
extern const char* getParameterTooltip(int paramIndex);
extern int getForegroundModeCount(void);
extern const char* getForegroundModeName(int modeId);
extern const char* getForegroundModeUsedParams(int modeId);
extern int getBackgroundModeCount(void);
extern const char* getBackgroundModeName(int modeId);
extern const char* getBackgroundModeUsedParams(int modeId);

LIGHT_ENGINE_EXPORT int exportTemplateCSV(const char* filepath) {
    FILE* f = fopen(filepath, "w");
    if (!f) return 0;
    
    // Write header
    fprintf(f, "Parameter,CC,Minimum Value,Maximum Value,Layer,Tooltip,Choices\n");
    
    // Write each parameter
    int paramCount = getParameterCount();
    for (int i = 0; i < paramCount; i++) {
        const char* name = getParameterName(i);
        int cc = getParameterCC(i);
        const char* layer = getParameterLayer(i);
        const char* tooltip = getParameterTooltip(i);
        
        // Determine which modes use this parameter
        char choices[2048] = "";
        
        // Check if this is a mode selector (CC 6 or 9)
        if (cc == 6) {
            // Foreground mode selector - list all foreground modes
            int modeCount = getForegroundModeCount();
            for (int m = 0; m < modeCount; m++) {
                strcat(choices, getForegroundModeName(m));
                if (m < modeCount - 1) strcat(choices, "\n");
            }
        } else if (cc == 9) {
            // Background mode selector - list all background modes
            int modeCount = getBackgroundModeCount();
            for (int m = 0; m < modeCount; m++) {
                strcat(choices, getBackgroundModeName(m));
                if (m < modeCount - 1) strcat(choices, "\n");
            }
        } else {
            // Regular parameter - find modes that use it
            char paramIndexStr[8];
            sprintf(paramIndexStr, "%d", i);
            
            int first = 1;
            
            // Check foreground modes
            int fgModeCount = getForegroundModeCount();
            for (int m = 0; m < fgModeCount; m++) {
                const char* usedParams = getForegroundModeUsedParams(m);
                if (strstr(usedParams, paramIndexStr)) {
                    if (!first) strcat(choices, "\n");
                    strcat(choices, getForegroundModeName(m));
                    first = 0;
                }
            }
            
            // Check background modes
            int bgModeCount = getBackgroundModeCount();
            for (int m = 0; m < bgModeCount; m++) {
                const char* usedParams = getBackgroundModeUsedParams(m);
                if (strstr(usedParams, paramIndexStr)) {
                    if (!first) strcat(choices, "\n");
                    strcat(choices, getBackgroundModeName(m));
                    first = 0;
                }
            }
        }
        
        // Write CSV row (with quoted choices to handle newlines)
        fprintf(f, "%s,%d,0,127,%s,\"%s\",\"%s\"\n", 
                name, cc, layer, tooltip, choices);
    }
    
    fclose(f);
    return 1;
}
