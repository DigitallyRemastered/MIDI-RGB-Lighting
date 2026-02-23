/*
  Remaining Foreground Modes
  
  Implementations for Back and Forth, Color Sinusoid, Flash Lights, and Opposing Waves.
*/

#include "../../Shared/LightEngineAPI.h"
#include <string.h>
#include <stdlib.h>

// Defined in Constants.cpp
extern "C" {
    extern const int COLOR_PHASE[64];
    extern const uint8_t TOP_BOTTOM_MIRROR_MAP[48];
}

// ===== BEGIN: Back and Forth (sync with lights.ino case 4) =====

void renderBackAndForth(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    int ledStart = ctx->params[3];        // ffLedStart
    int ledLength = ctx->params[4];       // ffLedLength
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Render block pattern
    for (int i = ledStart; i < ledStart + ledLength; i++) {
        int idx = i % numLeds;
        leds[idx].h = static_cast<uint8_t>(hue);
        leds[idx].s = static_cast<uint8_t>(sat);
        leds[idx].v = static_cast<uint8_t>(bright);
    }
}

// ===== END: Back and Forth =====


// ===== BEGIN: Move startLED (sync with lights.ino OnNoteOn case 5) =====

void renderMoveStartLED(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    int ledStart = ctx->params[3];        // ffLedStart (modified by MIDI notes in plugin)
    int ledLength = ctx->params[4];       // ffLedLength
    int sat2 = ctx->params[5] * 2;        // ffSat2
    int lines = ctx->params[6];           // lines
    
    if (lines == 0) lines = 1;
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Render moving pattern (position controlled by plugin's MIDI note handler)
    int lineOffset = numLeds / lines;
    
    for (int line = 0; line < lines; line++) {
        for (int led = ledStart; led < ledStart + ledLength; led++) {
            int idx = (led + line * lineOffset) % numLeds;
            leds[idx].h = static_cast<uint8_t>(hue);
            leds[idx].s = static_cast<uint8_t>((line == 0) ? sat : sat2);
            leds[idx].v = static_cast<uint8_t>(bright);
        }
    }
}

// ===== END: Move startLED =====


// ===== BEGIN: Color Sinusoid (sync with lights.ino case 6) =====

void renderColorSinusoid(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    int phaseShift = ctx->params[13];     // phaseShift
    int brightness = ctx->params[14];     // brightness
    int sinePeriod = ctx->params[7];      // sinePeriod
    
    if (sinePeriod == 0) sinePeriod = 1;
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    for (int i = 0; i < numLeds; i++) {
        int phaseIndex = ((i * 64 / sinePeriod) + phaseShift) % 64;
        int sinVal = COLOR_PHASE[phaseIndex];
        int hueOffset = (sinVal * brightness) / 100;
        
        leds[i].h = static_cast<uint8_t>(hue + hueOffset);
        leds[i].s = static_cast<uint8_t>(sat);
        leds[i].v = static_cast<uint8_t>(bright);
    }
}

// ===== END: Color Sinusoid =====


// ===== BEGIN: Flash Lights (sync with lights.ino case 7) =====

void renderFlashLights(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    
    // Use deterministic random seed from plugin
    srand(ctx->randomSeed);
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Random flashing effect
    for (int i = 0; i < numLeds; i++) {
        int brightness = (rand() % 2) ? bright : 0;
        leds[i].h = static_cast<uint8_t>(hue);
        leds[i].s = static_cast<uint8_t>(sat);
        leds[i].v = static_cast<uint8_t>(brightness);
    }
}

// ===== END: Flash Lights =====


// ===== BEGIN: Opposing Waves (sync with lights.ino case 9) =====

void renderOpposingWaves(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[4] * 2;         // ffSat2
    int length = ctx->params[9];          // waveLength
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Render opposing wave patterns on mirrored strips
    // Forward direction
    for (int i = 0; i < length && i < 24; i++) {
        int ledIdx = i;  // Strips 0-23
        if (ledIdx >= 0 && ledIdx < numLeds) {
            leds[ledIdx].h = static_cast<uint8_t>(hue);
            leds[ledIdx].s = static_cast<uint8_t>(sat);
            leds[ledIdx].v = 254;
        }
    }
    
    // Reverse direction
    for (int i = 0; i < length && i < 24; i++) {
        int ledIdx = 95 - i;  // Strips 95-72 (reverse)
        if (ledIdx >= 0 && ledIdx < numLeds) {
            leds[ledIdx].h = static_cast<uint8_t>(hue);
            leds[ledIdx].s = static_cast<uint8_t>(sat);
            leds[ledIdx].v = 254;
        }
    }
}

// ===== END: Opposing Waves =====
