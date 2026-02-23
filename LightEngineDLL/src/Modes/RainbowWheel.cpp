/*
  Rainbow Wheel Mode
  
  Displays a rainbow gradient across all LEDs.
  Extracted from lights.ino case 1.
*/

#include "../LightEngineAPI.h"

// ===== BEGIN: Rainbow Wheel (sync with lights.ino case 1) =====

void renderRainbowWheel(HSV* leds, int numLeds, const RenderContext* ctx) {
   int hue = ctx->params[0] * 2;      // ffHue (scaled 0-254)
    int sat = ctx->params[1] * 2;      // ffSat
    int bright = ctx->params[2] * 2;   // ffBright
    
    const int RAINBOW_INC = 255 / numLeds;
    
    for (int led = 0; led < numLeds; led++) {
        leds[led].h = static_cast<uint8_t>((hue + led * RAINBOW_INC) % 256);
        leds[led].s = static_cast<uint8_t>(sat);
        leds[led].v = static_cast<uint8_t>(bright);
    }
}

// ===== END: Rainbow Wheel =====
