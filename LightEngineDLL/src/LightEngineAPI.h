/*
  Light Engine API
  
  C interface for dynamically loadable light rendering engines.
  Used by both the Arduino firmware (statically) and JUCE plugin (dynamically via DLL).
  
  Version: 1.0
*/

#ifndef LIGHT_ENGINE_API_H
#define LIGHT_ENGINE_API_H

#include <stdint.h>

#ifdef _WIN32
  #define LIGHT_ENGINE_EXPORT __declspec(dllexport)
#else
  #define LIGHT_ENGINE_EXPORT __attribute__((visibility("default")))
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Data Structures
// ============================================================================

/** HSV color representation */
typedef struct {
    uint8_t h;  // Hue (0-255)
    uint8_t s;  // Saturation (0-255)
    uint8_t v;  // Value/Brightness (0-255)
} HSV;

/** Rendering context containing all parameter values and state */
typedef struct {
    int params[32];          // Generic parameter values (0-127)
    HSV* background;         // Pre-rendered background buffer (can be NULL)
    int randomSeed;          // Seed for random/flash modes
    uint8_t midiNotes[128];  // Active MIDI notes bitmap (1=on, 0=off)
} RenderContext;

// ============================================================================
// Engine Information
// ============================================================================

/** Get engine version number (for compatibility checking) */
LIGHT_ENGINE_EXPORT int getEngineVersion(void);

/** Get engine name string (e.g., "Default Light Engine") */
LIGHT_ENGINE_EXPORT const char* getEngineName(void);

// ============================================================================
// Parameter Metadata
// ============================================================================

/** Get total number of parameters defined by this engine (max 32) */
LIGHT_ENGINE_EXPORT int getParameterCount(void);

/** Get parameter display name (e.g., "Hue", "Saturation") */
LIGHT_ENGINE_EXPORT const char* getParameterName(int paramIndex);

/** Get parameter tooltip/description */
LIGHT_ENGINE_EXPORT const char* getParameterTooltip(int paramIndex);

/** Get parameter layer ("Foreground", "Background", "Shared", or "") */
LIGHT_ENGINE_EXPORT const char* getParameterLayer(int paramIndex);

/** Get MIDI CC number for this parameter (1-127) */
LIGHT_ENGINE_EXPORT int getParameterCC(int paramIndex);

// ============================================================================
// Mode Metadata
// ============================================================================

/** Get number of foreground modes */
LIGHT_ENGINE_EXPORT int getForegroundModeCount(void);

/** Get foreground mode name by ID (e.g., "Rainbow Wheel") */
LIGHT_ENGINE_EXPORT const char* getForegroundModeName(int modeId);

/** Get comma-separated parameter indices used by this mode (e.g., "0,1,2") */
LIGHT_ENGINE_EXPORT const char* getForegroundModeUsedParams(int modeId);

/** Get number of background modes */
LIGHT_ENGINE_EXPORT int getBackgroundModeCount(void);

/** Get background mode name by ID */
LIGHT_ENGINE_EXPORT const char* getBackgroundModeName(int modeId);

/** Get comma-separated parameter indices used by this background mode */
LIGHT_ENGINE_EXPORT const char* getBackgroundModeUsedParams(int modeId);

// ============================================================================
// LED Configuration
// ============================================================================

/** Get total number of LEDs in the strip */
LIGHT_ENGINE_EXPORT int getNumLEDs(void);

// ============================================================================
// Rendering Functions
// ============================================================================

/**
 * Render a foreground mode to the LED buffer
 * 
 * @param modeId  Mode identifier (0 to getForegroundModeCount()-1)
 * @param leds    Output LED buffer (must be numLeds in size)
 * @param numLeds Number of LEDs in buffer
 * @param ctx     Rendering context with parameters and state
 */
LIGHT_ENGINE_EXPORT void renderForeground(int modeId, HSV* leds, int numLeds, const RenderContext* ctx);

/**
 * Render a background mode to the LED buffer
 * 
 * @param modeId  Mode identifier (0 to getBackgroundModeCount()-1)
 * @param leds    Output LED buffer (must be numLeds in size)
 * @param numLeds Number of LEDs in buffer
 * @param ctx     Rendering context with parameters and state
 */
LIGHT_ENGINE_EXPORT void renderBackground(int modeId, HSV* leds, int numLeds, const RenderContext* ctx);

// ============================================================================
// CSV Export (Optional)
// ============================================================================

/**
 * Export FL Studio template CSV from engine metadata
 * 
 * @param filepath  Path where CSV should be written
 * @return  1 on success, 0 on failure
 */
LIGHT_ENGINE_EXPORT int exportTemplateCSV(const char* filepath);

#ifdef __cplusplus
}
#endif

#endif // LIGHT_ENGINE_API_H
