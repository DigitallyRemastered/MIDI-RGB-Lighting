/*
  Ocean Waves Mode
  
  Creates wave effect on mirrored LED strips.
  Extracted from lights.ino case 8.
*/

#include "../LightEngineAPI.h"
#include <string.h>

// Defined in Constants.cpp
extern "C" {
    extern const uint8_t TOP_BOTTOM_MIRROR_MAP[48];
}

// ===== BEGIN: Ocean Waves (sync with lights.ino case 8) =====

void renderOceanWaves(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[4] * 2;         // ffSat2
    int length = ctx->params[9];          // waveLength
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Render ocean waves on mirrored strips
    for (int i = 0; i < length && i < 48; i++) {
        int ledIdx = TOP_BOTTOM_MIRROR_MAP[i];
        if (ledIdx >= 0 && ledIdx < numLeds) {
            leds[ledIdx].h = static_cast<uint8_t>(hue);
            leds[ledIdx].s = static_cast<uint8_t>(sat);
            leds[ledIdx].v = 254;
        }
    }
}

// ===== END: Ocean Waves =====
