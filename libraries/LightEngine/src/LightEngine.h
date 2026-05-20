/*
  Light Engine - Unified Core
  
  Complete lighting engine shared between Arduino and Windows plugin.
  Single source of truth for all LED rendering behavior.
  
  Usage:
    Arduino:  LightEngine engine(108); engine.handleNoteOn(...); engine.render();
    Plugin:   Create via DLL C API, call same methods through function pointers
*/

#ifndef LIGHT_ENGINE_H
#define LIGHT_ENGINE_H

#include <stdint.h>

// Platform detection
#ifdef ARDUINO
    #include <Arduino.h>
    #include <FastLED.h>
    typedef CHSV HSVColor;
#else
    // Standard C++ (Windows DLL)
    #include <cstdlib>
    #include <cstring>
    #include <cmath>
    
    typedef struct {
        uint8_t h, s, v;
    } HSVColor;
#endif

// ============================================================================
// Waveform Modulation
// ============================================================================

// Waveform profile types
enum WaveformProfile {
    WAVEFORM_SAWTOOTH = 0,
    WAVEFORM_TRIANGLE = 1,
    WAVEFORM_SQUARE = 2,
    WAVEFORM_SINE = 3
};

// Waveform configuration for a single parameter
struct WaveformConfig {
    uint8_t profile;        // WaveformProfile (0-3)
    uint8_t amplitude;      // 0-127: magnitude of change through cycle
    uint8_t offset;         // 0-127: static base value
    uint16_t phaseShift;    // 0-16383: frame offset for phase alignment
    uint8_t period;         // 1-127: number of beats per cycle
    bool direction;         // true = forward, false = reverse
    bool enable;            // true = use waveform, false = use CC value only
};

// ============================================================================
// Light Engine Class
// ============================================================================

class LightEngine {
public:
    // Constructor - numLeds must be 108 for current hardware layout
    LightEngine(int numLeds = 108);
    ~LightEngine();
    
    // ========================================================================
    // MIDI Event Interface (THE THREE KEY METHODS)
    // ========================================================================
    
    /**
     * Handle MIDI Control Change
     * Updates internal parameter state
     * @param channel MIDI channel (1-16, or 0 for any)
     * @param control CC number (1-15 supported)
     * @param value CC value (0-127)
     */
    void handleControlChange(uint8_t channel, uint8_t control, uint8_t value);
    
    /**
     * Handle MIDI Note On
     * Stores note state and updates mode-specific behavior
     * @param channel MIDI channel (1-16)
     * @param note Note number (0-127)
     * @param velocity Note velocity (1-127)
     */
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);
    
    /**
     * Handle MIDI Note Off
     * Clears note state
     * @param channel MIDI channel (1-16)
     * @param note Note number (0-127)
     * @param velocity Release velocity (0-127)
     */
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);
    
    /**
     * Handle MIDI System Exclusive message
     * Used for tempo sync and waveform configuration
     * @param data SysEx message bytes (including F0/F7)
     * @param length Total message length
     */
    void handleSysEx(const uint8_t* data, uint16_t length);
    
    // ========================================================================
    // Rendering
    // ========================================================================
    
    /**
     * Render current state to LED buffer
     * Call this at ~30Hz to update the LED visualization
     * Automatically increments frame counter for randomness
     */
    void render();
    
    /**
     * Get read-only access to LED buffer
     */
    const HSVColor* getLEDs() const { return leds; }
    
    /**
     * Get number of LEDs
     */
    int getNumLEDs() const { return numLeds; }
    
    // ========================================================================
    // State Access (for debugging/serialization)
    // ========================================================================
    
    // Get CC parameter value (returns 0-127 range)
    int getCC(int ccNumber) const;
    
    // Set CC parameter value (0-127 range)
    void setCC(int ccNumber, int value);
    
    // Serialized state layout (bytes):
    //   [0]       magic = 0x4C
    //   [1]       version = 0x01
    //   [2..16]   CC1-15 values (0-127 each)          = 15 bytes
    //   [17..18]  tempoBPM*10 as uint16, big-endian   =  2 bytes
    //   [19..138] waveforms[15], 8 bytes each:         = 120 bytes
    //               profile, amplitude, offset, phaseL, phaseH, period,
    //               direction (0/1), enable (0/1)
    //   Total: 2 + 15 + 2 + 120 = 139 bytes
    static const size_t STATE_SIZE = 139;
    
    // Serialize current state into buf. Returns bytes written, or 0 on failure.
    size_t serializeState(uint8_t* buf, size_t bufLen) const;
    
    // Restore state from buf. Returns false if magic/version invalid (state unchanged).
    bool deserializeState(const uint8_t* buf, size_t len);

    // Returns true (and clears the flag) if a save-state SysEx (0x06) was received
    // since the last call.  Firmware should call this once per loop() and persist
    // state to non-volatile memory when it returns true.
    bool saveStateRequested();
    
private:
    // ========================================================================
    // LED Buffers
    // ========================================================================
    int numLeds;
    HSVColor* leds;          // Main LED buffer
    HSVColor* background;    // Pre-rendered background layer
    
    // ========================================================================
    // MIDI-Controlled Parameters (CC 1-15)
    // Stored in 0-254 or 0-127 range as needed
    // ========================================================================
    
    // Foreground Layer (CC 1-7)
    int ffHue;          // CC1: Hue (0-254, scaled from 0-127)
    int ffSat;          // CC2: Saturation (0-254)
    int ffBright;       // CC3: Brightness (0-254)
    int ffLedStart;     // CC4: Start position (0-127)
    int ffLedLength;    // CC5: Length (0-127)
    int ffMode;         // CC6: Mode selector (0-9)
    int lines;          // CC7: Number of lines (0-127)
    
    // Shared Parameters
    int cAmp;           // CC8: Color Amplitude for sinusoid (0-127)
    
    // Background Mode
    int bgMode;         // CC9: Background mode selector (0-2)
    int pan;            // CC10: Pan position for wave effects (0-127)
    
    // Background Layer (CC 11-15)
    int bgHue;          // CC11: Background Hue (0-254)
    int bgSat;          // CC12: Background Saturation (0-254)
    int bgBright;       // CC13: Background Brightness (0-254)
    int bgLedStart;     // CC14: Background Start position (0-127)
    int bgLedLength;    // CC15: Background Length (0-127)
    
    // ========================================================================
    // Note State
    // ========================================================================
    uint8_t activeNotes[128];  // activeNotes[note] = velocity (0 = off)
    
    // ========================================================================
    // Helper Variables
    // ========================================================================
    int lineOffset;            // Calculated offset between lines
    uint32_t frameCounter;     // Auto-incremented for random seed
    
    // ========================================================================
    // Tempo-Sync Waveform Modulation State
    // ========================================================================
    static const uint8_t FRAME_RATE = 30;  // Fixed 30fps rendering
    
    float tempoBPM;                // Current DAW tempo (60.0-240.0 typical)
    
    WaveformConfig waveforms[15];  // One config per CC parameter (CC1-15)
    
    // ========================================================================
    // Constants (defined in LightEngine.cpp)
    // ========================================================================
    static const int COLOR_PHASE[64];

    // Set by handleSysEx(0x06); cleared and returned by saveStateRequested().
    bool _saveStateRequested = false;
    
    // ========================================================================
    // Internal Rendering Methods
    // ========================================================================
    void updateBackground();
    void renderForeground();
    
    // Mode-specific rendering
    void renderRainbowWheel();
    void renderMovingDots();
    void renderComets();
    void renderBackAndForth();
    void renderMoveStartLED();
    void renderColorSinusoid();
    void renderFlashLights();
    
    // Background modes
    void renderFlatBackground();
    void renderRainbowBackground();
    void renderSinusoidBackground();
    
    // Utility
    inline void setLED(int index, uint8_t h, uint8_t s, uint8_t v);
    inline void copyBackground();
    
    // Waveform modulation helpers
    float evaluateWaveform(int profile, float phase, bool direction) const;
    void applyWaveformModulation();
};

// ============================================================================
// Parameter Metadata
// ============================================================================

struct ParameterInfo {
    int ccNumber;
    const char* name;
    const char* layer;      // "Foreground", "Background", or "Shared"
    const char* tooltip;
};

struct ModeInfo {
    int id;
    const char* name;
    const char* uses;       // Comma-separated parameter list
};

// Get parameter metadata (returns nullptr if ccNumber invalid)
const ParameterInfo* getParameterInfo(int ccNumber);

// Get all parameters (returns count)
int getAllParameters(const ParameterInfo** outArray);

// Get foreground mode metadata
int getForegroundModeCount();
const ModeInfo* getForegroundModeInfo(int modeId);

// Get background mode metadata
int getBackgroundModeCount();
const ModeInfo* getBackgroundModeInfo(int modeId);

// ============================================================================
// C API for DLL Export (Windows only)
// ============================================================================

#ifndef ARDUINO

#ifdef _WIN32
  #define LIGHT_ENGINE_EXPORT __declspec(dllexport)
#else
  #define LIGHT_ENGINE_EXPORT
#endif

#ifdef __cplusplus
extern "C" {
#endif

// Engine lifecycle
LIGHT_ENGINE_EXPORT void* lightEngine_create(int numLeds);
LIGHT_ENGINE_EXPORT void lightEngine_destroy(void* engine);

// MIDI handlers
LIGHT_ENGINE_EXPORT void lightEngine_handleControlChange(void* engine, uint8_t channel, uint8_t control, uint8_t value);
LIGHT_ENGINE_EXPORT void lightEngine_handleNoteOn(void* engine, uint8_t channel, uint8_t note, uint8_t velocity);
LIGHT_ENGINE_EXPORT void lightEngine_handleNoteOff(void* engine, uint8_t channel, uint8_t note, uint8_t velocity);
LIGHT_ENGINE_EXPORT void lightEngine_handleSysEx(void* engine, const uint8_t* data, uint16_t length);

// Rendering
LIGHT_ENGINE_EXPORT void lightEngine_render(void* engine);
LIGHT_ENGINE_EXPORT const HSVColor* lightEngine_getLEDs(void* engine);
LIGHT_ENGINE_EXPORT int lightEngine_getNumLEDs(void* engine);

// State access
LIGHT_ENGINE_EXPORT int lightEngine_getCC(void* engine, int ccNumber);
LIGHT_ENGINE_EXPORT void lightEngine_setCC(void* engine, int ccNumber, int value);

// Metadata
LIGHT_ENGINE_EXPORT const char* lightEngine_getEngineName();
LIGHT_ENGINE_EXPORT int lightEngine_getEngineVersion();

LIGHT_ENGINE_EXPORT int lightEngine_getParameterCount();
LIGHT_ENGINE_EXPORT const char* lightEngine_getParameterName(int ccNumber);
LIGHT_ENGINE_EXPORT const char* lightEngine_getParameterLayer(int ccNumber);
LIGHT_ENGINE_EXPORT const char* lightEngine_getParameterTooltip(int ccNumber);

LIGHT_ENGINE_EXPORT int lightEngine_getForegroundModeCount();
LIGHT_ENGINE_EXPORT const char* lightEngine_getForegroundModeName(int modeId);

LIGHT_ENGINE_EXPORT int lightEngine_getBackgroundModeCount();
LIGHT_ENGINE_EXPORT const char* lightEngine_getBackgroundModeName(int modeId);

LIGHT_ENGINE_EXPORT int lightEngine_getForegroundModeParameterCount(int modeId);
LIGHT_ENGINE_EXPORT int lightEngine_getForegroundModeParameter(int modeId, int index);
LIGHT_ENGINE_EXPORT int lightEngine_getBackgroundModeParameterCount(int modeId);
LIGHT_ENGINE_EXPORT int lightEngine_getBackgroundModeParameter(int modeId, int index);

#ifdef __cplusplus
}
#endif

#endif // !ARDUINO

#endif // LIGHT_ENGINE_H
