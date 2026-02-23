/*
  Notes to Drives Mode
  
  Maps MIDI notes (36-52) to specific LED drives using CHANNEL_TO_LED lookup.
  Extracted from lights.ino OnNoteOn lines 152-157.
*/

#include "../LightEngineAPI.h"
#include <string.h>

// Defined in Constants.cpp
extern "C" {
    extern const uint8_t CHANNEL_TO_LED[17][6];
}

// ===== BEGIN: Notes to Drives (sync with lights.ino OnNoteOn case 0) =====

void renderNotesToDrives(HSV* leds, int numLeds, const RenderContext* ctx) {
    int hue = ctx->params[0] * 2;         // ffHue
    int sat = ctx->params[1] * 2;         // ffSat
    int bright = ctx->params[2] * 2;      // ffBright
    
    // Copy background if provided
    if (ctx->background) {
        memcpy(leds, ctx->background, numLeds * sizeof(HSV));
    }
    
    // Scan MIDI notes 36-52 (17 channels)
    for (int noteNum = 36; noteNum <= 52; noteNum++) {
        uint8_t velocity = ctx->midiNotes[noteNum];
        
        if (velocity > 0) {
            int channel = noteNum - 36;  // 0-16
            
            // Light up the 6 LEDs for this channel
            for (int i = 0; i < 6; i++) {
                int ledIdx = CHANNEL_TO_LED[channel][i];
                if (ledIdx >= 0 && ledIdx < numLeds) {
                    leds[ledIdx].h = static_cast<uint8_t>(hue);
                    leds[ledIdx].s = static_cast<uint8_t>(sat);
                    leds[ledIdx].v = static_cast<uint8_t>(bright);
                }
            }
        }
    }
}

// ===== END: Notes to Drives =====
