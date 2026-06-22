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

// Final composited output color.  The engine owns the HSV→RGB conversion
// (spectrum mapping) so firmware and the plugin preview consume identical
// bytes — FastLED's CHSV→CRGB rainbow mapping must NOT be applied on top.
typedef struct {
    uint8_t r, g, b;
} RGBColor;

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
// The TC.offset field is the static/base value; amplitude > 0 adds oscillation.
// Always active — there is no enable flag.
// ============================================================================

struct TemporalConfig {
    uint8_t  profile;      // WaveshapeProfile (0-3)
    uint8_t  amplitude;    // 0-127: oscillation magnitude
    uint8_t  offset;       // 0-127: base value (static value when amplitude = 0)
    uint16_t phaseShift;   // 0-16383: frame offset for phase alignment
    uint8_t  period;       // 1-127: number of beats per cycle
    bool     direction;    // true = forward, false = reverse
};

// ============================================================================
// ColorComponent — defines one color dimension (Hue, Saturation, or Value)
// as a spatial waveform across the LED strip.  Each of the four spatial
// parameters is fully controlled by its own always-active TemporalConfig;
// TC.offset is the static/base value and TC.amplitude adds time oscillation.
// ============================================================================

struct ColorComponent {
    // Spatial waveform shape (how the value varies across LEDs)
    uint8_t waveshape;   // WaveshapeProfile: spatial distribution shape

    // Each spatial parameter is managed entirely by its TemporalConfig.
    // TC.offset = static baseline; TC.amplitude > 0 adds time-varying modulation.
    TemporalConfig ampTemporal;          // controls spatial amplitude
    TemporalConfig offsetTemporal;       // controls spatial offset (base color value)
    TemporalConfig wavelengthTemporal;   // controls spatial wavelength (LEDs per cycle)
    TemporalConfig phaseShiftTemporal;   // controls spatial phase shift (start LED offset)
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
    MODE_OFF             = 0,  // Layer produces no output (distinct from enabled=false)
    MODE_SOLID           = 1,  // Pure panel evaluation — no procedural motion
    MODE_MOVING_DOTS     = 2,  // Moving line segments
    MODE_COMETS          = 3,  // Comet trails with brightness fade
    MODE_FLASH           = 4,  // Random single-LED flash each frame
    MODE_GRAVITY_COMET   = 5,  // Note-triggered comet under gravity physics
    MODE_NOTE_GATE       = 6,  // Solid pattern, opacity gated by held notes on the layer's channel
    MODE_BRIGHTNESS_SPIKE= 7,  // Note On slams brightness up, then decays (velocity-sensitive)
    MODE_SAT_SPIKE       = 8,  // Note On pushes saturation to full, then decays
    MODE_HUE_SPIKE       = 9,  // Note On rotates hue, then eases back
    MODE_FIREBALL        = 10  // Note On launches a comet up the strip that burns out at the top
};

// ============================================================================
// Layer — one compositable rendering layer
// ============================================================================

static const int MAX_COMET_POLY = 16;  // Maximum simultaneous comets per layer

struct Layer {
    uint8_t        mode;                       // LayerMode
    ColorComponent hue;                        // Hue color component
    ColorComponent sat;                        // Saturation color component
    ColorComponent val;                        // Value (brightness) color component
    TemporalConfig linesTemporal;              // controls number of parallel lines
    TemporalConfig motionOffsetTemporal;        // start-LED offset for dots/comets modes
    TemporalConfig motionLengthTemporal;        // segment length for dots/comets modes
    TemporalConfig opacityTemporal;            // layer blend weight (offset=base opacity)
    CometState     comets[MAX_COMET_POLY];     // Polyphonic comet slots (GravityComet / Fireball)

    // Note-triggered transient state — runtime only, NOT serialized (like comets[]).
    float          spikeLevel;                 // 0..1 envelope: set on Note On, decays each frame (spike modes)
    uint8_t        heldCount;                  // notes currently held on this layer's channel (NoteGate mode)
};

// ============================================================================
// Light Engine Class
// ============================================================================

class LightEngine {
public:
    static const int MAX_LEDS = 1000;  // Maximum LED buffer size (allocated once)

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
     *   0x09 - Set active LED count: [F0, 7D, 09, countHi, countLo, F7]
     *          count = (countHi << 7) | countLo, clamped to [1, MAX_LEDS]
     *   0x0A - Full-state sync fragment: [F0, 7D, 0A, seqNum, totalFrags,
     *          ...7-bit-encoded payload..., F7].  Fragments are reassembled
     *          internally; a complete, exact-size set is decoded and applied
     *          via deserializeState().  Works on every target (Teensy, ESP32,
     *          plugin) since the logic lives here rather than in firmware.
     *   0x0B - Master lights enable: [F0, 7D, 0B, on, F7] (on: 0 = dark)
     *   0x0C - Full layer bundle (mode + waveshapes + all 16 TC slots) in one
     *          message: [F0, 7D, 0C, layerIdx, mode, hueWave, satWave, valWave,
     *          <16 slots x 7 bytes: profile, amplitude, offset, phaseL, phaseH,
     *          period, direction>, F7].  Slot order is fixed: hue amp/off/wl/phase,
     *          sat amp/off/wl/phase, val amp/off/wl/phase, lines, motionOffset,
     *          motionLength, opacity (the same order used by pushAllLayersToHardware
     *          in the plugin).  Replaces sending CC1/3/4/5 plus 16 separate 0x07
     *          messages per layer with a single packet, so a full 16-layer sync
     *          costs 16 UDP datagrams instead of up to ~320 — critical for rtpMIDI,
     *          whose recovery journal does not cover SysEx (lost packets are gone
     *          for good, so fewer packets per sync means fewer chances to lose one).
     */
    void handleSysEx(const uint8_t* data, uint16_t length);

    // ========================================================================
    // Rendering
    // ========================================================================

    /** Render all enabled layers and composite to LED buffer.  Call at ~30 Hz. */
    void render();

    /**
     * Master lights enable.  When false, render() zeroes the output buffers
     * and returns immediately, so the strip and preview go dark while all
     * layer state stays intact.  Settable remotely via SysEx 0x0B.
     */
    void setLightsEnabled(bool on) { lightsEnabled = on; }
    bool getLightsEnabled() const  { return lightsEnabled; }

    const HSVColor* getLEDs()    const { return leds; }
    int             getNumLEDs() const { return numLeds; }

    /**
     * Final composited colors as RGB — the authoritative output.  Firmware
     * writes these bytes to the strip verbatim and the plugin preview draws
     * them verbatim, so both always show the same colors.  getLEDs() remains
     * as the HSV view for diagnostics (e.g. the spatial wave plots).
     */
    const RGBColor* getRGB() const { return rgbOut; }

    /** Set the active LED count at runtime (clamped to [1, MAX_LEDS]). */
    void setNumLeds(int n);

    /**
     * Returns true (and clears the flag) if the active LED count changed
     * since the last call.  Arduino firmware uses this to re-register
     * FastLED with the new count.
     */
    bool numLedsChanged();

    // ========================================================================
    // State Access
    // ========================================================================

    /**
     * Get a CC parameter value (0-127) for a specific layer (0-15).
     * CC 1 = Mode (LayerMode 0-6)
     * CC 3 = Hue Waveshape  (0-3)
     * CC 4 = Sat Waveshape  (0-3)
     * CC 5 = Val Waveshape  (0-3)
     * Opacity and all amplitude/offset/wavelength/phaseShift are set via SysEx
     * TemporalConfig messages (0x07 full / 0x08 single field).
     */
    int  getLayerCC(int layer, int cc) const;

    /**
     * Set a CC parameter value (0-127) for a specific layer (0-15).
     */
    void setLayerCC(int layer, int cc, int value);

    // Serialized state layout (v6):
    //   [0]    magic   = 0x4C
    //   [1]    version = 0x06
    //   [2-3]  tempoBPM*10 as uint16, big-endian
    //   [4-5]  active LED count as uint16, big-endian (clamped to [1, MAX_LEDS]
    //          on load; persists the count across power cycles)
    //   [6..]  16 layers × 116 bytes each:
    //            mode(1), opacityTemporal(7)                     =   8 bytes
    //            hue ColorComponent: waveshape(1) + 4×7 TC       =  29 bytes
    //            sat ColorComponent: 29 bytes
    //            val ColorComponent: 29 bytes
    //            linesTemporal: 7 bytes
    //            motionOffsetTemporal: 7 bytes
    //            motionLengthTemporal: 7 bytes
    //          = 8 + 3×29 + 7 + 7 + 7 = 116 bytes per layer
    //   Total: 6 + 16×116 = 1862 bytes
    static const size_t STATE_SIZE = 1862;

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
    HSVColor*  leds;                      // Composited output, HSV view (MAX_LEDS allocated)
    RGBColor*  rgbOut;                    // Composited output, authoritative RGB (MAX_LEDS allocated)
    HSVColor*  layerBuffers[MAX_LAYERS];  // Per-layer render buffers (MAX_LEDS allocated each)

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
    float    effectiveOpacity[MAX_LAYERS];  // per-layer opacity from temporal modulation

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
    bool _numLedsChanged     = false;
    bool lightsEnabled       = true;   // master enable — render() outputs black when false

    // ========================================================================
    // SysEx 0x0A full-state sync reassembly
    // The plugin pushes the whole serialized state as 7-bit-encoded fragments.
    // Every fragment except the last carries exactly SYNC_PAYLOAD_PER_FRAG
    // encoded bytes, so each is stored at seqNum * SYNC_PAYLOAD_PER_FRAG —
    // duplicated or reordered delivery (rtpMIDI runs over UDP) is harmless.
    // The buffer is heap-allocated on first use (3 KB) so instances that never
    // receive a state push (e.g. the plugin's own preview engine) pay nothing.
    // ========================================================================
    static const uint16_t SYNC_PAYLOAD_PER_FRAG = 192;  // must match the plugin sender
    static const uint8_t  SYNC_MAX_FRAGS        = 16;   // 12 needed at STATE_SIZE=1862
    static const uint32_t SYNC_STALE_FRAMES     = 90;   // ~3 s at 30 fps

    uint8_t*  syncBuf_           = nullptr;
    uint8_t   syncTotalFrags_    = 0;  // expected fragment count (0 = idle)
    uint16_t  syncFragsMask_     = 0;  // bit n set = fragment n received
    uint16_t  syncLastFragLen_   = 0;  // payload length of the final fragment
    uint32_t  syncLastFragFrame_ = 0;  // frameCounter when last fragment arrived

    // frag points at seqNum; len counts seqNum + totalFrags + payload bytes.
    void handleStateSyncFragment(const uint8_t* frag, uint16_t len);

    // ========================================================================
    // Per-frame effective parameters
    // Computed by applyTimeModulationForLayer(), passed to renderLayer() so
    // temporal modulation is evaluated once per frame rather than per LED.
    // ========================================================================
    struct LayerEffectiveParams {
        float hueAmp, hueOffset, hueWavelength, huePhase;
        float satAmp, satOffset, satWavelength, satPhase;
        float valAmp, valOffset, valWavelength, valPhase;
        float opacity;
        float lines;
        float motionOffset;  // start-LED for dots/comets modes
        float motionLength;  // segment length for dots/comets modes
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
    void renderFlash       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderGravityComet(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderBrightnessSpike(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderSatSpike       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderHueSpike       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);
    void renderFireball       (int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep);

    // Utility
    void setLED(HSVColor* buf, int index, uint8_t h, uint8_t s, uint8_t v);

    // Evaluate a ColorComponent's spatial waveform at a single LED index.
    // Returns a raw float (may exceed 0-254 before clamping/wrapping by caller).
    float evalPanel(const ColorComponent& panel, int ledIndex,
                    float effAmp, float effOffset,
                    float effWavelength, float effPhase) const;

    // Temporal modulation helpers
    float evaluateTemporalWaveform(int profile, float phase, bool direction) const;
    float applyTemporalConfig(const TemporalConfig& tc) const;
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

// Only the dedicated DLL build (LightEngineDLL/CMakeLists defines
// LIGHT_ENGINE_BUILD_DLL) exports these symbols.  When the engine is compiled
// statically into the plugin/app, the C API stays internal so it does not
// pollute the host binary's export table.
#if defined(_WIN32) && defined(LIGHT_ENGINE_BUILD_DLL)
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
LIGHT_ENGINE_EXPORT const RGBColor* lightEngine_getRGB    (void* engine);
LIGHT_ENGINE_EXPORT int             lightEngine_getNumLEDs(void* engine);

// State serialization (buf must hold LightEngine::STATE_SIZE bytes; returns
// bytes written, 0 on failure).  Used by the plugin's full-state push.
LIGHT_ENGINE_EXPORT size_t lightEngine_serializeState(void* engine, uint8_t* buf, size_t bufLen);

// Per-layer state access (layer: 0-15, cc: 1-19)
LIGHT_ENGINE_EXPORT int  lightEngine_getLayerCC(void* engine, int layer, int cc);
LIGHT_ENGINE_EXPORT void lightEngine_setLayerCC(void* engine, int layer, int cc, int value);

// Active LED count (clamped to [1, MAX_LEDS])
LIGHT_ENGINE_EXPORT void lightEngine_setNumLeds(void* engine, int n);

// Master lights enable (0 = dark; render() zeroes output and short-circuits)
LIGHT_ENGINE_EXPORT void lightEngine_setLightsEnabled(void* engine, int enabled);

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
