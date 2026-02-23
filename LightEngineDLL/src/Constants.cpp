/*
  Light Engine Constants
  
  Shared lookup tables and constants used by rendering modes.
  Extracted from lights.ino.
*/

#include "LightEngineAPI.h"

// ============================================================================
// Color Lookup Tables
// ============================================================================

// 64-element discretized sine wave [-100,100] for color sinusoid mode
// Represents 100*sin(theta) where theta ranges from 0 to 2*pi
extern "C" LIGHT_ENGINE_EXPORT const int COLOR_PHASE[64] = {
    10, 20,  29,  38, 47,  56,  63,  71,  77,  83,  88,  92,  96,  98,  100,  100,
    100,  98,  96,  92,  88,  83,  77,  71,  63,  56,  47,  38,  29,  20,  10,  0,
    -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100, -100,
    -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10, 0
};

// ============================================================================
// LED Position Mappings
// ============================================================================

// Mirrors top strip (LEDs 0-23) and bottom strip (LEDs 72-95) for Ocean Waves modes
extern "C" LIGHT_ENGINE_EXPORT const uint8_t TOP_BOTTOM_MIRROR_MAP[48] = {
    23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
    95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72
};

// Maps MIDI channels (1-16) to LED indices for Notes to Drives mode
// Each floppy drive has 6 LEDs controlled by one MIDI channel
extern "C" LIGHT_ENGINE_EXPORT const uint8_t CHANNEL_TO_LED[17][6] = {
    {0, 0, 0, 0, 0, 0},           // Channel 0 (unused)
    {19, 20, 21, 22, 23, 24},     // Channel 1
    {29, 30, 31, 32, 33, 34},     // Channel 2
    {13, 14, 15, 16, 17, 18},     // Channel 3
    {35, 36, 37, 38, 39, 40},     // Channel 4
    {7 , 8 , 9 , 10, 11, 12},     // Channel 5
    {41, 42, 43, 44, 45, 46},     // Channel 6
    {1 , 2 , 3 , 4 , 5, 6},       // Channel 7
    {47, 48, 49, 50, 51, 52},     // Channel 8
    {73, 74, 75, 76, 77, 78},     // Channel 9
    {83, 84, 85, 86, 87, 88},     // Channel 10
    {67, 68, 69, 70, 71, 72},     // Channel 11
    {89, 90, 91, 92, 93, 94},     // Channel 12
    {61, 62, 63, 64, 65, 66},     // Channel 13
    {95, 96, 97, 98, 99, 100},    // Channel 14
    {55, 56, 57, 58, 59, 60},     // Channel 15
    {101, 102, 103, 104, 105, 106}, // Channel 16
};
