/*
  Moving Dots Mode
  
  Displays moving dots/lines with background.
  Extracted from lights.ino case 2.
*/

#include "../../Shared/LightEngineAPI.h"
#include <string.h>

// ===== BEGIN: Moving Dots (sync with lights.ino case 2) =====

void renderMovingDots(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    int ledStart = ctx->params[3];        // ffLedStart
    int ledLength = ctx->params[4];       // ffLedLength
    int lines = ctx->params[6];           // lines
    
    if (lines == 0) lines = 1;  // Prevent division by zero
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Render foreground dots
    int lineOffset = numLeds / lines;
    
    for (int line = 0; line < lines; line++) {
        for (int led = ledStart; led < ledStart + ledLength; led++) {
            int idx = (led + line * lineOffset) % numLeds;
            leds[idx].h = static_cast<uint8_t>(hue);
            leds[idx].s = static_cast<uint8_t>(sat);
            leds[idx].v = static_cast<uint8_t>(bright);
        }
    }
}

// ===== END: Moving Dots =====
