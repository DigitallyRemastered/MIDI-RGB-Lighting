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
// Waveshape Profile — shared by spatial ColorPanel and TemporalConfig
// ============================================================================

enum WaveshapeProfile {
    WAVESHAPE_SAWTOOTH = 0,
    WAVESHAPE_TRIANGLE = 1,
    WAVESHAPE_SQUARE   = 2,
    WAVESHAPE_SINE     = 3
};

// ============================================================================
// TemporalConfig — time-varying modulation of a single scalar parameter.
// Controls how a parameter oscillates in sync with DAW tempo.
// Distinct from the spatial waveshape that ColorPanel uses to distribute
// color across the LED strip.
// ============================================================================

struct TemporalConfig {
    uint8_t  profile;      // WaveshapeProfile (0-3)
    uint8_t  amplitude;    // 0-127: oscillation magnitude
    uint8_t  offset;       // 0-127: static base value
    uint16_t phaseShift;   // 0-16383: frame offset for phase alignment
    uint8_t  period;       // 1-127: number of beats per cycle
    bool     direction;    // true = forward, false = reverse
    bool     enable;       // true = modulate, false = pass CC value through
};

// ============================================================================
// ColorPanel — defines one color dimension (Hue, Saturation, or Value)
// as a spatial waveform across the LED strip.  Each of the four spatial
// parameters can be independently time-modulated by a TemporalConfig.
// ============================================================================

struct ColorPanel {
    // Spatial waveform (how the value varies across LEDs)
    uint8_t waveshape;   // WaveshapeProfile: shape of spatial distribution
    uint8_t amplitude;   // 0-127: spatial oscillation depth (×2 → 0-254)
    uint8_t offset;      // 0-127: base value (×2 → 0-254 for H/S/V space)
    uint8_t wavelength;  // 0-127: LEDs per spatial cycle (0 = flat / no wave)
    uint8_t phaseShift;  // 0-127: spatial start offset in LEDs

    // Independent temporal modulation for each spatial parameter
    TemporalConfig ampTemporal;         // modulates amplitude
    TemporalConfig offsetTemporal;      // modulates offset
    TemporalConfig wavelengthTemporal;  // modulates wavelength
    TemporalConfig phaseShiftTemporal;  // modulates phaseShift
};

// ============================================================================
// CometState — one gravity-physics comet instance (one per held note)
// ============================================================================

struct CometState {
    bool    active;    // true while this slot is in use
    float   position;  // distance from LED 0 in inches (LED spacing = 0.625")
    float   velocity;  // in/s — positive toward higher LED index
    uint8_t note;      // MIDI note that spawned this comet
};

// ============================================================================
// Layer Modes
// ============================================================================

enum LayerMode {
    MODE_OFF           = 0,  // Layer produces no output (distinct from enabled=false)
    MODE_SOLID         = 1,  // Pure panel evaluation — no procedural motion
    MODE_MOVING_DOTS   = 2,  // Moving line segments
    MODE_COMETS        = 3,  // Comet trails with brightness fade
    MODE_BACK_FORTH    = 4,  // Oscillating blocks back and forth
    MODE_FLASH         = 5,  // Random single-LED flash each frame
    MODE_GRAVITY_COMET = 6   // Note-triggered comet under gravity physics
};

// ============================================================================
// Layer — one compositable rendering layer
// ============================================================================

static const int MAX_COMET_POLY = 16;  // Maximum simultaneous comets per layer

struct Layer {
    bool       enabled;                    // false = zero effect; skipped entirely
    uint8_t    mode;                       // LayerMode
    ColorPanel hue;                        // Hue panel
    ColorPanel sat;                        // Saturation panel
    ColorPanel val;                        // Value (brightness) panel
    uint8_t    lines;                      // Parallel line count for repeating patterns
    uint8_t    opacity;                    // 0-127: blend weight (0=transparent, 127=opaque)
    CometState comets[MAX_COMET_POLY];     // Polyphonic comet slots (GravityComet mode)
};

// ============================================================================
// Light Engine Class
// ============================================================================

class LightEngine {
public:
    LightEngine(int numLeds = 108);
    ~LightEngine();

    // ========================================================================
    // MIDI Event Interface
    // ========================================================================

    /**
     * Handle MIDI Control Change.
     * @param channel MIDI channel (1-16); selects layers[channel-1]
     * @param control CC number (1-19 supported, see getLayerCC for mapping)
     * @param value   CC value (0-127)
     */
    void handleControlChange(uint8_t channel, uint8_t control, uint8_t value);

    /**
     * Handle MIDI Note On.
     * Stores note state; triggers GravityComet physics for matching-channel layers.
     */
    void handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity);

    /**
     * Handle MIDI Note Off.
     * Clears note state.
     */
    void handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity);

    /**
     * Handle MIDI System Exclusive.
     * Supported types:
     *   0x01 - Tempo update
     *   0x03 - Reset frame counter
     *   0x06 - Request save to persistent memory
     *   0x07 - Full panel TemporalConfig (atomic)
     *   0x08 - Single panel TemporalConfig field
     */
    void handleSysEx(const uint8_t* data, uint16_t length);

    // ========================================================================
    // Rendering
    // ========================================================================

    /** Render all enabled layers and composite to LED buffer.  Call at ~30 Hz. */
    void render();

    const HSVColor* getLEDs()    const { return leds; }
    int             getNumLEDs() const { return numLeds; }

    // ========================================================================
    // State Access
    // ========================================================================

    /**
     * Get a CC parameter value (0-127) for a specific layer (0-15).
     * CC  1        = Mode (LayerMode 0-6)
     * CC  2- 6     = Hue panel:  Waveshape, Amplitude, Offset, Wavelength, PhaseShift
     * CC  7-11     = Sat panel:  same order
     * CC 12-16     = Val panel:  same order
     * CC 17        = Lines
     * CC 18        = Opacity (0-127)
     * CC 19        = Enabled (0 = disabled, 1 = enabled)
     */
    int  getLayerCC(int layer, int cc) const;

    /**
     * Set a CC parameter value (0-127) for a specific layer (0-15).
     */
    void setLayerCC(int layer, int cc, int value);

    // Serialized state layout:
    //   [0]    magic   = 0x4C
    //   [1]    version = 0x02
    //   [2-3]  tempoBPM*10 as uint16, big-endian
    //   [4..]  16 layers × 115 bytes each:
    //            enabled(1), mode(1), lines(1), opacity(1)  =   4 bytes
    //            hue ColorPanel: waveshape+amp+offset+wl+ps  =   5 bytes
    //              + 4 TemporalConfigs × 8 bytes             =  32 bytes
    //                                                           37 bytes/panel
    //            sat ColorPanel: 37 bytes
    //            val ColorPanel: 37 bytes
    //          = 4 + 3×37 = 115 bytes per layer
    //   Total: 4 + 16×115 = 1844 bytes
    static const size_t STATE_SIZE = 1844;

    size_t serializeState  (uint8_t* buf, size_t bufLen) const;
    bool   deserializeState(const uint8_t* buf, size_t len);

    /** Returns true (and clears the flag) if a save-state SysEx (0x06) was received. */
    bool saveStateRequested();

private:
    // ========================================================================
    // LED Buffers
    // ========================================================================
    static const int MAX_LAYERS = 16;

    int        numLeds;
    HSVColor*  leds;                      // Final composited output buffer
    HSVColor*  layerBuffers[MAX_LAYERS];  // Per-layer render buffers [numLeds] each

    // ========================================================================
    // Layer State
    // ========================================================================
    Layer layers[MAX_LAYERS];

    // ========================================================================
    // Note State
    // ========================================================================
    uint8_t activeNotes[128];  // activeNotes[note] = velocity (0 = note off)

    // ========================================================================
    // Temporal Modulation State
    // ========================================================================
    static const uint8_t FRAME_RATE = 30;  // Fixed 30 fps rendering

    float    tempoBPM;      // Current DAW tempo (60.0-240.0 BPM)
    uint32_t frameCounter;  // Auto-incremented each render()

    // ========================================================================
    // Physics Constants
    // ========================================================================
    static constexpr float LED_SPACING_INCHES = 0.625f;  // 5/8" LED pitch
    static constexpr float GRAVITY_IN_PER_S2  = 386.1f;  // 9.81 m/s² in in/s²

    // ========================================================================
    // Constants
    // ========================================================================
    static const int COLOR_PHASE[64];  // 64-point sine lookup, values -100..+100

    bool _saveStateRequested = false;

    // ========================================================================
    // Per-frame effective parameters
    // Computed by applyTimeModulationForLayer(), passed to renderLayer() so
    // temporal modulation is evaluated once per frame rather than per LED.
    // ========================================================================
    struct LayerEffectiveParams {
        float hueAmp, hueOffset, hueWavelength, huePhase;
        float satAmp, satOffset, satWavelength, satPhase;
        float valAmp, valOffset, valWavelength, valPhase;
    };

    // ========================================================================
    // Internal Rendering
    // ========================================================================
    void applyTimeModulationForLayer(int layerIdx, LayerEffectiveParams& out) const;
    void renderLayer       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void compositeLayersToOutput();

    // Mode renderers
    void renderSolid       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderMovingDots  (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderComets      (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderBackAndForth(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderFlash       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderGravityComet(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);

    // Utility
    void setLED(HSVColor* buf, int index, uint8_t h, uint8_t s, uint8_t v);

    // Evaluate a ColorPanel's spatial waveform at a single LED index.
    // Returns a raw float (may exceed 0-254 before clamping/wrapping by caller).
    float evalPanel(const ColorPanel& panel, int ledIndex,
                    float effAmp, float effOffset,
                    float effWavelength, float effPhase) const;

    // Temporal modulation helpers
    float evaluateTemporalWaveform(int profile, float phase, bool direction) const;
    float applyTemporalConfig(const TemporalConfig& tc, float baseValue) const;
};

// ============================================================================
// Parameter Metadata
// ============================================================================

struct ParameterInfo {
    int         ccNumber;
    const char* name;
    const char* tooltip;
};

struct ModeInfo {
    int         id;
    const char* name;
};

// Get parameter metadata for CC 1-19 (returns nullptr if out of range)
const ParameterInfo* getParameterInfo(int ccNumber);

// Get all parameters (fills *outArray, returns count = 19)
int getAllParameters(const ParameterInfo** outArray);

// Unified mode metadata (no foreground/background split)
int             getModeCount();
const ModeInfo* getModeInfo(int modeId);

// ============================================================================
// C API for DLL Export (Windows / shared library)
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
LIGHT_ENGINE_EXPORT void* lightEngine_create (int numLeds);
LIGHT_ENGINE_EXPORT void  lightEngine_destroy(void* engine);

// MIDI handlers
LIGHT_ENGINE_EXPORT void lightEngine_handleControlChange(void* engine, uint8_t channel, uint8_t control, uint8_t value);
LIGHT_ENGINE_EXPORT void lightEngine_handleNoteOn       (void* engine, uint8_t channel, uint8_t note,    uint8_t velocity);
LIGHT_ENGINE_EXPORT void lightEngine_handleNoteOff      (void* engine, uint8_t channel, uint8_t note,    uint8_t velocity);
LIGHT_ENGINE_EXPORT void lightEngine_handleSysEx        (void* engine, const uint8_t* data, uint16_t length);

// Rendering
LIGHT_ENGINE_EXPORT void            lightEngine_render    (void* engine);
LIGHT_ENGINE_EXPORT const HSVColor* lightEngine_getLEDs   (void* engine);
LIGHT_ENGINE_EXPORT int             lightEngine_getNumLEDs(void* engine);

// Per-layer state access (layer: 0-15, cc: 1-19)
LIGHT_ENGINE_EXPORT int  lightEngine_getLayerCC(void* engine, int layer, int cc);
LIGHT_ENGINE_EXPORT void lightEngine_setLayerCC(void* engine, int layer, int cc, int value);

// Metadata
LIGHT_ENGINE_EXPORT const char* lightEngine_getEngineName   ();
LIGHT_ENGINE_EXPORT int         lightEngine_getEngineVersion();

LIGHT_ENGINE_EXPORT int         lightEngine_getParameterCount  ();
LIGHT_ENGINE_EXPORT const char* lightEngine_getParameterName   (int ccNumber);
LIGHT_ENGINE_EXPORT const char* lightEngine_getParameterTooltip(int ccNumber);

LIGHT_ENGINE_EXPORT int         lightEngine_getModeCount();
LIGHT_ENGINE_EXPORT const char* lightEngine_getModeName (int modeId);

#ifdef __cplusplus
}
#endif

#endif // !ARDUINO

#endif // LIGHT_ENGINE_H
