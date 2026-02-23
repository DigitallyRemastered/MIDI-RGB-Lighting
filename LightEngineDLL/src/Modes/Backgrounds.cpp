/*
  Background Modes
  
  Three background rendering modes for layering.
  Extracted from lights.ino updateBG function.
*/

#include "../LightEngineAPI.h"

// Defined in Constants.cpp
extern "C" {
    extern const int COLOR_PHASE[64];
}

// ===== BEGIN: Flat Background (sync with lights.ino case 0) =====

void renderFlatBackground(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[10] * 2;        // bgHue
    int sat = ctx->params[11] * 2;        // bgSat
    int bright = ctx->params[12] * 2;     // bgBright
    
    for (int i = 0; i < numLeds; i++) {
        leds[i].h = static_cast<uint8_t>(hue);
        leds[i].s = static_cast<uint8_t>(sat);
        leds[i].v = static_cast<uint8_t>(bright);
    }
}

// ===== END: Flat Background =====


// ===== BEGIN: Rainbow Background (sync with lights.ino case 1) =====

void renderRainbowBackground(HSV* leds, int numLeds, const RenderContext* ctx) {
    int sat = ctx->params[11] * 2;        // bgSat
    int bright = ctx->params[12] * 2;     // bgBright
    
    int rainbowInc = 255 / numLeds;
    
    for (int i = 0; i < numLeds; i++) {
        leds[i].h = static_cast<uint8_t>(i * rainbowInc);
        leds[i].s = static_cast<uint8_t>(sat);
        leds[i].v = static_cast<uint8_t>(bright);
    }
}

// ===== END: Rainbow Background =====


// ===== BEGIN: Sinusoid Background (sync with lights.ino case 2) =====

void renderSinusoidBackground(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[10] * 2;        // bgHue
    int sat = ctx->params[11] * 2;        // bgSat
    int bright = ctx->params[12] * 2;     // bgBright
    int phaseShift = ctx->params[13];     // phaseShift
    int brightness = ctx->params[14];     // brightness
    int sinePeriod = ctx->params[7];      // sinePeriod
    
    if (sinePeriod == 0) sinePeriod = 1;  // Prevent division by zero
    
    for (int i = 0; i < numLeds; i++) {
        int phaseIndex = ((i * 64 / sinePeriod) + phaseShift) % 64;
        int sinVal = COLOR_PHASE[phaseIndex];  // -100 to 100
        int val = (sinVal * brightness) / 100 + bright;
        
        if (val < 0) val = 0;
        if (val > 254) val = 254;
        
        leds[i].h = static_cast<uint8_t>(hue);
        leds[i].s = static_cast<uint8_t>(sat);
        leds[i].v = static_cast<uint8_t>(val);
    }
}

// ===== END: Sinusoid Background =====
