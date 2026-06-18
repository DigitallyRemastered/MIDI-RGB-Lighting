/*
  Light Engine - Implementation

  All rendering logic, constants, and metadata in one place.
*/

#include "LightEngine.h"

#ifndef ARDUINO
    #include <cstdlib>
    #include <cstring>
    #include <cmath>
#else
    #include <math.h>
#endif

// ============================================================================
// Constants
// ============================================================================

// 64-point sine lookup (-100..+100), used for both spatial and temporal SINE waveshape
const int LightEngine::COLOR_PHASE[64] = {
    10, 20, 29, 38, 47, 56, 63, 71, 77, 83, 88, 92, 96, 98, 100, 100,
    100, 98, 96, 92, 88, 83, 77, 71, 63, 56, 47, 38, 29, 20, 10, 0,
    -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100, -100,
    -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10, 0
};

// ============================================================================
// Parameter Metadata
// ============================================================================

static const ParameterInfo PARAMETER_TABLE[] = {
    { 1, "Mode",           "Layer mode (0=Off,1=Solid,2=MovingDots,3=Comets,4=Flash,5=GravityComet)"},
    { 3, "Hue Waveshape",  "Spatial waveshape for hue (0=Sawtooth,1=Triangle,2=Square,3=Sine)"},
    { 4, "Sat Waveshape",  "Spatial waveshape for saturation"},
    { 5, "Val Waveshape",  "Spatial waveshape for brightness"}
};

static const ModeInfo MODE_TABLE[] = {
    {0, "Off"},
    {1, "Solid"},
    {2, "Moving Dots"},
    {3, "Comets"},
    {4, "Flash"},
    {5, "Gravity Comet"}
};

const ParameterInfo* getParameterInfo(int ccNumber) {
    for (const auto& info : PARAMETER_TABLE)
        if (info.ccNumber == ccNumber) return &info;
    return nullptr;
}

int getAllParameters(const ParameterInfo** outArray) {
    *outArray = PARAMETER_TABLE;
    return 4;
}

int getModeCount() {
    return 6;
}

const ModeInfo* getModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 6) return nullptr;
    return &MODE_TABLE[modeId];
}

// Local helpers used across compositing/render passes.
static inline uint8_t clampSV(float v);
static inline uint8_t wrapHue(float h);

struct RGBf { float r, g, b; };
static RGBf     hsvToRgbF(uint8_t h, uint8_t s, uint8_t v);
static HSVColor rgbFToHsv(float r, float g, float b);

// ============================================================================
// Constructor / Destructor
// ============================================================================

static void initTemporalConfig(TemporalConfig& tc) {
    tc.profile    = WAVESHAPE_SAWTOOTH;
    tc.amplitude  = 0;
    tc.offset     = 0;     // caller sets meaningful default after this
    tc.phaseShift = 0;
    tc.period     = 4;
    tc.direction  = true;
}

static void initColorComponent(ColorComponent& p, uint8_t defOffset, uint8_t defWavelength) {
    p.waveshape = WAVESHAPE_SAWTOOTH;
    initTemporalConfig(p.ampTemporal);         p.ampTemporal.offset         = 0;             // no amplitude variation
    initTemporalConfig(p.offsetTemporal);      p.offsetTemporal.offset      = defOffset;     // base color value
    initTemporalConfig(p.wavelengthTemporal);  p.wavelengthTemporal.offset  = defWavelength; // LEDs per cycle
    initTemporalConfig(p.phaseShiftTemporal);  p.phaseShiftTemporal.offset  = 0;             // no phase offset
}

LightEngine::LightEngine(int numLeds) : numLeds(numLeds) {
    // Allocate full MAX_LEDS capacity once; numLeds controls how many are active.
    leds   = new HSVColor[MAX_LEDS];
    rgbOut = new RGBColor[MAX_LEDS];
    for (int li = 0; li < MAX_LAYERS; li++) {
        layerBuffers[li] = new HSVColor[MAX_LEDS];
    }

    // All layers start MODE_OFF with opacity temporal offset=0 (fully transparent)
    for (int li = 0; li < MAX_LAYERS; li++) {
        Layer& layer = layers[li];
        layer.mode = MODE_OFF;
        initColorComponent(layer.hue, 0,   127);  // hue:  offset=0,   wl=127
        initColorComponent(layer.sat, 127, 127);  // sat:  offset=127 (full), wl=127
        initColorComponent(layer.val, 64,  127);  // val:  offset=64  (half), wl=127
        initTemporalConfig(layer.linesTemporal);  layer.linesTemporal.offset = 1;  // 1 line
        initTemporalConfig(layer.opacityTemporal); layer.opacityTemporal.offset = 0;
        for (int ci = 0; ci < MAX_COMET_POLY; ci++) {
            layer.comets[ci].active   = false;
            layer.comets[ci].position = 0.0f;
            layer.comets[ci].velocity = 0.0f;
            layer.comets[ci].note     = 0;
        }
    }

    memset(activeNotes, 0, sizeof(activeNotes));
    frameCounter        = 0;
    tempoBPM            = 120.0f;
    _saveStateRequested = false;
    _numLedsChanged     = false;
    memset(effectiveOpacity, 0, sizeof(effectiveOpacity));

    memset(leds, 0, MAX_LEDS * sizeof(HSVColor));
    memset(rgbOut, 0, MAX_LEDS * sizeof(RGBColor));
    for (int li = 0; li < MAX_LAYERS; li++) {
        memset(layerBuffers[li], 0, MAX_LEDS * sizeof(HSVColor));
    }
}

LightEngine::~LightEngine() {
    delete[] leds;
    delete[] rgbOut;
    for (int li = 0; li < MAX_LAYERS; li++) {
        delete[] layerBuffers[li];
    }
    delete[] syncBuf_;
}

// ============================================================================
// LED Count
// ============================================================================

void LightEngine::setNumLeds(int n) {
    if (n < 1)        n = 1;
    if (n > MAX_LEDS) n = MAX_LEDS;
    if (n != numLeds) {
        numLeds = n;
        _numLedsChanged = true;
    }
}

bool LightEngine::numLedsChanged() {
    if (_numLedsChanged) {
        _numLedsChanged = false;
        return true;
    }
    return false;
}

// ============================================================================
// MIDI Event Handlers
// ============================================================================

void LightEngine::handleControlChange(uint8_t channel, uint8_t control, uint8_t value) {
    // MIDI channel 1-16 selects layers[channel-1]
    if (channel < 1 || channel > 16) return;
    int li = channel - 1;
    Layer& layer = layers[li];

    // CC 1 = Mode
    // CC 3 = Hue Waveshape  (0-3)
    // CC 4 = Sat Waveshape  (0-3)
    // CC 5 = Val Waveshape  (0-3)
    // Opacity is set via SysEx 0x07/0x08 (opacityTemporal, panelId=6).
    switch (control) {
        case 1:  layer.mode          = value % 6;    break;
        case 3:  layer.hue.waveshape = value & 0x03; break;
        case 4:  layer.sat.waveshape = value & 0x03; break;
        case 5:  layer.val.waveshape = value & 0x03; break;
        default: break;
    }
}

void LightEngine::handleNoteOn(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (note < 128) {
        activeNotes[note] = velocity;
    }

    if (channel < 1 || channel > 16) return;
    int li = channel - 1;
    Layer& layer = layers[li];

    if (layer.mode == MODE_GRAVITY_COMET) {
        // Find a free comet slot and launch it toward the note's target LED
        for (int ci = 0; ci < MAX_COMET_POLY; ci++) {
            if (!layer.comets[ci].active) {
                int targetLed = (note < (uint8_t)numLeds) ? (int)note : (numLeds - 1);
                float targetPos = (float)targetLed * LED_SPACING_INCHES;
                float v0 = (targetPos > 0.0f) ? sqrtf(2.0f * GRAVITY_IN_PER_S2 * targetPos) : 0.0f;
                layer.comets[ci].active   = true;
                layer.comets[ci].position = 0.0f;
                layer.comets[ci].velocity = v0;
                layer.comets[ci].note     = note;
                break;
            }
        }
    }
}

void LightEngine::handleNoteOff(uint8_t channel, uint8_t note, uint8_t velocity) {
    if (note < 128) {
        activeNotes[note] = 0;
    }
    // GravityComet comets continue under physics; they self-deactivate when
    // they fall back to position <= 0 with negative velocity.
    (void)channel; (void)velocity;
}

// ============================================================================
// State Access
// ============================================================================

int LightEngine::getLayerCC(int layer, int cc) const {
    if (layer < 0 || layer >= MAX_LAYERS) return 0;
    const Layer& l = layers[layer];

    switch (cc) {
        case 1: return l.mode;
        case 3: return l.hue.waveshape;
        case 4: return l.sat.waveshape;
        case 5: return l.val.waveshape;
        default: return 0;
    }
}

void LightEngine::setLayerCC(int layer, int cc, int value) {
    if (layer < 0 || layer >= MAX_LAYERS) return;
    uint8_t v = (uint8_t)(value < 0 ? 0 : value > 127 ? 127 : value);
    handleControlChange((uint8_t)(layer + 1), (uint8_t)cc, v);
}

// ============================================================================
// State Serialization
// ============================================================================

static size_t serializeTC(uint8_t* buf, const TemporalConfig& tc) {
    buf[0] = tc.profile;
    buf[1] = tc.amplitude;
    buf[2] = tc.offset;
    buf[3] = (uint8_t)(tc.phaseShift & 0xFF);
    buf[4] = (uint8_t)(tc.phaseShift >> 8);
    buf[5] = tc.period;
    buf[6] = tc.direction ? 1 : 0;
    return 7;  // enable byte removed
}

static size_t deserializeTC(const uint8_t* buf, TemporalConfig& tc) {
    tc.profile    = buf[0] & 0x03;
    tc.amplitude  = buf[1];
    tc.offset     = buf[2];
    tc.phaseShift = (uint16_t)buf[3] | ((uint16_t)buf[4] << 8);
    tc.period     = (buf[5] > 0) ? buf[5] : 1;
    tc.direction  = buf[6] != 0;
    return 7;
}

static size_t serializeComponent(uint8_t* buf, const ColorComponent& p) {
    buf[0] = p.waveshape;
    size_t off = 1;
    off += serializeTC(buf + off, p.ampTemporal);
    off += serializeTC(buf + off, p.offsetTemporal);
    off += serializeTC(buf + off, p.wavelengthTemporal);
    off += serializeTC(buf + off, p.phaseShiftTemporal);
    return off;  // 1 + 4*7 = 29
}

static size_t deserializeComponent(const uint8_t* buf, ColorComponent& p) {
    p.waveshape = buf[0] & 0x03;
    size_t off = 1;
    off += deserializeTC(buf + off, p.ampTemporal);
    off += deserializeTC(buf + off, p.offsetTemporal);
    off += deserializeTC(buf + off, p.wavelengthTemporal);
    off += deserializeTC(buf + off, p.phaseShiftTemporal);
    return off;  // 29
}

size_t LightEngine::serializeState(uint8_t* buf, size_t bufLen) const {
    if (!buf || bufLen < STATE_SIZE) return 0;
    size_t i = 0;

    buf[i++] = 0x4C;  // magic
    buf[i++] = 0x06;  // version (v6: adds active LED count)

    // tempoBPM as uint16 (BPM*10), big-endian
    uint16_t bpmFixed = (uint16_t)(tempoBPM * 10.0f + 0.5f);
    buf[i++] = (uint8_t)(bpmFixed >> 8);
    buf[i++] = (uint8_t)(bpmFixed & 0xFF);

    // active LED count as uint16, big-endian (so it survives power cycles)
    buf[i++] = (uint8_t)((numLeds >> 8) & 0xFF);
    buf[i++] = (uint8_t)(numLeds & 0xFF);

    // 16 layers x 116 bytes each
    for (int li = 0; li < MAX_LAYERS; li++) {
        const Layer& layer = layers[li];
        buf[i++] = layer.mode;
        i += serializeTC(buf + i, layer.opacityTemporal);
        i += serializeComponent(buf + i, layer.hue);
        i += serializeComponent(buf + i, layer.sat);
        i += serializeComponent(buf + i, layer.val);
        i += serializeTC(buf + i, layer.linesTemporal);
        i += serializeTC(buf + i, layer.motionOffsetTemporal);
        i += serializeTC(buf + i, layer.motionLengthTemporal);
    }

    return i;  // == STATE_SIZE
}

bool LightEngine::deserializeState(const uint8_t* buf, size_t len) {
    if (!buf || len < STATE_SIZE) return false;  // length first: never read past caller's buffer
    if (buf[0] != 0x4C) return false;
    if (buf[1] != 0x06) return false;

    size_t i = 2;

    uint16_t bpmFixed = ((uint16_t)buf[i] << 8) | buf[i + 1];
    i += 2;
    tempoBPM = bpmFixed / 10.0f;
    if (tempoBPM < 20.0f || tempoBPM > 999.0f) tempoBPM = 120.0f;  // corrupt tempo would break period math

    int count = ((int)buf[i] << 8) | buf[i + 1];
    i += 2;
    setNumLeds(count);  // clamps to [1, MAX_LEDS]; sets numLedsChanged for FastLED re-registration

    for (int li = 0; li < MAX_LAYERS; li++) {
        Layer& layer = layers[li];
        uint8_t m  = buf[i++];
        layer.mode = (m < 6) ? m : (uint8_t)MODE_OFF;  // reject corrupt mode bytes
        i += deserializeTC(buf + i, layer.opacityTemporal);
        i += deserializeComponent(buf + i, layer.hue);
        i += deserializeComponent(buf + i, layer.sat);
        i += deserializeComponent(buf + i, layer.val);
        i += deserializeTC(buf + i, layer.linesTemporal);
        i += deserializeTC(buf + i, layer.motionOffsetTemporal);
        i += deserializeTC(buf + i, layer.motionLengthTemporal);
    }

    return true;
}

// ============================================================================
// Rendering Pipeline
// ============================================================================

void LightEngine::render() {
    frameCounter++;

    if (!lightsEnabled) {
        memset(leds,   0, numLeds * sizeof(HSVColor));
        memset(rgbOut, 0, numLeds * sizeof(RGBColor));
        return;
    }

    for (int li = 0; li < MAX_LAYERS; li++) {
        if (layers[li].mode == MODE_OFF) continue;

        LayerEffectiveParams ep;
        applyTimeModulationForLayer(li, ep);
        effectiveOpacity[li] = ep.opacity;

        memset(layerBuffers[li], 0, numLeds * sizeof(HSVColor));
        renderLayer(li, layerBuffers[li], ep);
    }

    compositeLayersToOutput();
}

void LightEngine::applyTimeModulationForLayer(int layerIdx, LayerEffectiveParams& out) const {
    const Layer& layer = layers[layerIdx];

    out.hueAmp        = applyTemporalConfig(layer.hue.ampTemporal);
    out.hueOffset     = applyTemporalConfig(layer.hue.offsetTemporal);
    out.hueWavelength = applyTemporalConfig(layer.hue.wavelengthTemporal);
    out.huePhase      = applyTemporalConfig(layer.hue.phaseShiftTemporal);

    out.satAmp        = applyTemporalConfig(layer.sat.ampTemporal);
    out.satOffset     = applyTemporalConfig(layer.sat.offsetTemporal);
    out.satWavelength = applyTemporalConfig(layer.sat.wavelengthTemporal);
    out.satPhase      = applyTemporalConfig(layer.sat.phaseShiftTemporal);

    out.valAmp        = applyTemporalConfig(layer.val.ampTemporal);
    out.valOffset     = applyTemporalConfig(layer.val.offsetTemporal);
    out.valWavelength = applyTemporalConfig(layer.val.wavelengthTemporal);
    out.valPhase      = applyTemporalConfig(layer.val.phaseShiftTemporal);

    out.opacity       = applyTemporalConfig(layer.opacityTemporal);
    out.lines         = applyTemporalConfig(layer.linesTemporal);
    out.motionOffset  = applyTemporalConfig(layer.motionOffsetTemporal);
    out.motionLength  = applyTemporalConfig(layer.motionLengthTemporal);
}

void LightEngine::renderLayer(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    switch (layers[layerIdx].mode) {
        case MODE_SOLID:         renderSolid       (layerIdx, buf, ep); break;
        case MODE_MOVING_DOTS:   renderMovingDots  (layerIdx, buf, ep); break;
        case MODE_COMETS:        renderComets      (layerIdx, buf, ep); break;
        case MODE_FLASH:         renderFlash       (layerIdx, buf, ep); break;
        case MODE_GRAVITY_COMET: renderGravityComet(layerIdx, buf, ep); break;
        default: break;
    }
}

void LightEngine::compositeLayersToOutput() {
    // Hoist per-layer blend weights so the per-LED loop only visits live layers.
    float alphas[MAX_LAYERS];
    bool  live[MAX_LAYERS];
    for (int li = 0; li < MAX_LAYERS; li++) {
        float a = effectiveOpacity[li] / 127.0f;
        if (a > 1.0f) a = 1.0f;
        alphas[li] = a;
        live[li]   = (layers[li].mode != MODE_OFF) && (a > 0.0f);
    }

    // Blend each LED in float RGB across all layers, quantizing only once at
    // the end.  The previous layer-major pass round-tripped through 8-bit HSV
    // after every layer, costing precision and two extra conversions per
    // layer per LED.  rgbOut is the authoritative output (strip + preview);
    // leds keeps the HSV view for diagnostics.
    for (int i = 0; i < numLeds; i++) {
        float r = 0.0f, g = 0.0f, b = 0.0f;
        for (int li = 0; li < MAX_LAYERS; li++) {
            if (!live[li]) continue;
            const HSVColor& c = layerBuffers[li][i];
            if (c.v == 0) continue;  // LED is off in this layer (transparent)
            RGBf src = hsvToRgbF(c.h, c.s, c.v);
            const float a = alphas[li];
            r = src.r * a + r * (1.0f - a);
            g = src.g * a + g * (1.0f - a);
            b = src.b * a + b * (1.0f - a);
        }
        rgbOut[i].r = (uint8_t)(r * 255.0f + 0.5f);
        rgbOut[i].g = (uint8_t)(g * 255.0f + 0.5f);
        rgbOut[i].b = (uint8_t)(b * 255.0f + 0.5f);
        leds[i] = rgbFToHsv(r, g, b);
    }
}

// ============================================================================
// Utility
// ============================================================================

void LightEngine::setLED(HSVColor* buf, int index, uint8_t h, uint8_t s, uint8_t v) {
    if (index >= 0 && index < numLeds) {
        buf[index].h = h;
        buf[index].s = s;
        buf[index].v = v;
    }
}

// Evaluate a ColorPanel's spatial waveform at a single LED index.
// Returns a raw float (effOffset*2 + effAmp*2*waveVal).
// Callers wrap hue (& 0xFF) or clamp sat/val (0-254) as appropriate.
float LightEngine::evalPanel(const ColorComponent& panel, int ledIndex,
                              float effAmp, float effOffset,
                              float effWavelength, float effPhase) const {
    if (effWavelength <= 0.0f) {
        return effOffset * 2.0f;  // Flat: no spatial variation
    }
    float spatialPhase = fmod((float)(ledIndex - effPhase) / effWavelength, 1.0f);
    if (spatialPhase < 0.0f) spatialPhase += 1.0f;
    float waveVal = evaluateTemporalWaveform((int)panel.waveshape, spatialPhase, true);
    return effOffset * 2.0f + effAmp * 2.0f * waveVal;
}

// Shared waveform evaluator for both spatial ColorPanel and temporal TemporalConfig.
float LightEngine::evaluateTemporalWaveform(int profile, float phase, bool direction) const {
    if (!direction) phase = 1.0f - phase;

    switch (profile) {
        case WAVESHAPE_SAWTOOTH:
            return phase;
        case WAVESHAPE_TRIANGLE:
            return (phase < 0.5f) ? 2.0f * phase : 2.0f * (1.0f - phase);
        case WAVESHAPE_SQUARE:
            return (phase < 0.5f) ? 0.0f : 1.0f;
        case WAVESHAPE_SINE: {
            int idx = (int)(phase * 64.0f) % 64;
            return (COLOR_PHASE[idx] + 100.0f) / 200.0f;
        }
        default:
            return phase;
    }
}

// Apply a TemporalConfig to produce a time-varying float in 0-127 range.
// TC.offset is the static base; TC.amplitude adds oscillation per DAW tempo.
float LightEngine::applyTemporalConfig(const TemporalConfig& tc) const {
    if (tc.amplitude == 0) return (float)tc.offset;  // fast path: flat

    float framesPerPeriod = (tc.period * 60.0f * FRAME_RATE) / tempoBPM;
    if (framesPerPeriod < 1.0f) framesPerPeriod = 1.0f;

    float shiftedFrame = fmod((float)frameCounter + (float)tc.phaseShift, framesPerPeriod);
    float phase = shiftedFrame / framesPerPeriod;

    float waveValue = evaluateTemporalWaveform(tc.profile, phase, tc.direction);
    float modulated = (float)tc.offset + waveValue * (float)tc.amplitude;
    if (modulated < 0.0f)   modulated = 0.0f;
    if (modulated > 127.0f) modulated = 127.0f;
    return modulated;
}

// ============================================================================
// Mode Renderers
// ============================================================================

static inline uint8_t clampSV(float v) {
    if (v < 0.0f)   return 0;
    if (v > 254.0f) return 254;
    return (uint8_t)v;
}

static inline uint8_t wrapHue(float h) {
    return (uint8_t)((int)h & 0xFF);
}

// Convert HSV (0-255 each) to normalised RGB floats (0.0-1.0).
static RGBf hsvToRgbF(uint8_t h, uint8_t s, uint8_t v) {
    float vf = v / 255.0f;
    float sf = s / 255.0f;
    if (sf == 0.0f) return { vf, vf, vf };
    float hf = (h / 255.0f) * 6.0f;
    int   i  = (int)hf;
    float f  = hf - i;
    float p  = vf * (1.0f - sf);
    float q  = vf * (1.0f - sf * f);
    float t  = vf * (1.0f - sf * (1.0f - f));
    switch (i % 6) {
        case 0:  return { vf, t,  p  };
        case 1:  return { q,  vf, p  };
        case 2:  return { p,  vf, t  };
        case 3:  return { p,  q,  vf };
        case 4:  return { t,  p,  vf };
        default: return { vf, p,  q  };
    }
}

// Convert normalised RGB floats (0.0-1.0) back to HSV (0-255 each).
static HSVColor rgbFToHsv(float r, float g, float b) {
    float maxC  = r > g ? (r > b ? r : b) : (g > b ? g : b);
    float minC  = r < g ? (r < b ? r : b) : (g < b ? g : b);
    float delta = maxC - minC;
    float hf = 0.0f;
    if (delta > 0.0f) {
        if      (maxC == r) hf = (g - b) / delta;
        else if (maxC == g) hf = 2.0f + (b - r) / delta;
        else                hf = 4.0f + (r - g) / delta;
        hf /= 6.0f;
        if (hf < 0.0f) hf += 1.0f;
    }
    HSVColor out;
    out.h = (uint8_t)(hf * 255.0f + 0.5f);
    out.s = (maxC > 0.0f) ? (uint8_t)(delta / maxC * 255.0f + 0.5f) : 0;
    out.v = (uint8_t)(maxC * 255.0f + 0.5f);
    return out;
}

void LightEngine::renderSolid(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    for (int i = 0; i < numLeds; i++) {
        uint8_t h = wrapHue (evalPanel(layer.hue, i, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
        uint8_t s = clampSV (evalPanel(layer.sat, i, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
        uint8_t v = clampSV (evalPanel(layer.val, i, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase));
        setLED(buf, i, h, s, v);
    }
}

void LightEngine::renderMovingDots(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    int lines = (int)ep.lines;
    if (lines <= 0) return;

    // hue.phaseShift (effective) = segment start LED  --> now motionOffsetTemporal
    // hue.wavelength (effective) = segment length in LEDs --> now motionLengthTemporal
    int startLed = (int)ep.motionOffset;
    int segLen   = (int)ep.motionLength;
    if (segLen <= 0) return;

    int lineOff = numLeds / lines;

    for (int line = 0; line < lines; line++) {
        for (int j = 0; j < segLen; j++) {
            int idx = (startLed + j + line * lineOff) % numLeds;
            uint8_t h = wrapHue (evalPanel(layer.hue, idx, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
            uint8_t s = clampSV (evalPanel(layer.sat, idx, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
            uint8_t v = clampSV (evalPanel(layer.val, idx, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase));
            setLED(buf, idx, h, s, v);
        }
    }
}

void LightEngine::renderComets(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    int lines = (int)ep.lines;
    if (lines <= 0) return;

    int startLed = (int)ep.motionOffset;
    int segLen   = (int)ep.motionLength;
    if (segLen <= 0) return;

    int lineOff = numLeds / lines;

    for (int line = 0; line < lines; line++) {
        for (int j = 0; j < segLen; j++) {
            int idx = (startLed + j + line * lineOff) % numLeds;
            // j=0 is tail (dim), j=segLen-1 is head (full brightness)
            float trailFactor = (float)(j + 1) / (float)segLen;
            uint8_t h = wrapHue (evalPanel(layer.hue, idx, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
            uint8_t s = clampSV (evalPanel(layer.sat, idx, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
            uint8_t v = clampSV (evalPanel(layer.val, idx, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase) * trailFactor);
            setLED(buf, idx, h, s, v);
        }
    }
}

void LightEngine::renderFlash(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    int randomLed;
#ifdef ARDUINO
    randomSeed(frameCounter);
    randomLed = random(numLeds);
#else
    srand(frameCounter);
    randomLed = rand() % numLeds;
#endif
    uint8_t h = wrapHue (evalPanel(layer.hue, randomLed, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
    uint8_t s = clampSV (evalPanel(layer.sat, randomLed, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
    uint8_t v = clampSV (evalPanel(layer.val, randomLed, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase));
    setLED(buf, randomLed, h, s, v);
}

void LightEngine::renderGravityComet(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    const float dt       = 1.0f / FRAME_RATE;
    const int   TAIL_LEN = 5;  // Number of LEDs in the trailing brightness fade

    for (int ci = 0; ci < MAX_COMET_POLY; ci++) {
        CometState& comet = layers[layerIdx].comets[ci];
        if (!comet.active) continue;

        // Physics step: constant downward acceleration
        comet.velocity -= GRAVITY_IN_PER_S2 * dt;
        comet.position += comet.velocity * dt;

        // Deactivate when comet returns to base after having launched
        if (comet.position <= 0.0f && comet.velocity < 0.0f) {
            comet.active   = false;
            comet.position = 0.0f;
            continue;
        }
        if (comet.position < 0.0f) comet.position = 0.0f;

        int headIdx = (int)(comet.position / LED_SPACING_INCHES);
        if (headIdx >= numLeds) headIdx = numLeds - 1;

        // Draw head + trailing tail with linearly decreasing brightness
        for (int t = 0; t < TAIL_LEN; t++) {
            int idx = headIdx - t;
            if (idx < 0) break;
            float brightness = (float)(TAIL_LEN - t) / (float)TAIL_LEN;
            uint8_t h = wrapHue (evalPanel(layer.hue, idx, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
            uint8_t s = clampSV (evalPanel(layer.sat, idx, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
            uint8_t v = clampSV (evalPanel(layer.val, idx, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase) * brightness);
            setLED(buf, idx, h, s, v);
        }
    }
}

// ============================================================================
// SysEx Handler
// ============================================================================

// Return pointer to the TemporalConfig for [layerIdx][panelId][paramId].
// panelId: 0=hue, 1=sat, 2=val, 3=lines, 4=motionOffset, 5=motionLength,
//          6=opacity (paramId ignored for panelId >= 3)
// paramId: 0=ampTemporal, 1=offsetTemporal, 2=wavelengthTemporal, 3=phaseShiftTemporal
static TemporalConfig* resolveTemporalConfig(Layer* layers, int layerIdx, int panelId, int paramId) {
    if (layerIdx < 0 || layerIdx > 15) return nullptr;
    if (panelId  < 0 || panelId  >  6) return nullptr;

    if (panelId == 3) return &layers[layerIdx].linesTemporal;
    if (panelId == 4) return &layers[layerIdx].motionOffsetTemporal;
    if (panelId == 5) return &layers[layerIdx].motionLengthTemporal;
    if (panelId == 6) return &layers[layerIdx].opacityTemporal;

    ColorComponent* comp = (panelId == 0) ? &layers[layerIdx].hue
                         : (panelId == 1) ? &layers[layerIdx].sat
                         :                  &layers[layerIdx].val;

    if (paramId < 0 || paramId > 3) return nullptr;
    switch (paramId) {
        case 0: return &comp->ampTemporal;
        case 1: return &comp->offsetTemporal;
        case 2: return &comp->wavelengthTemporal;
        case 3: return &comp->phaseShiftTemporal;
    }
    return nullptr;
}

void LightEngine::handleSysEx(const uint8_t* data, uint16_t length) {
    if (length < 4)               return;
    if (data[0] != 0xF0)          return;
    if (data[length - 1] != 0xF7) return;

    // Detect standard (offset=1) vs USB MIDI with extra routing byte (offset=2)
    int offset = 0;
    if      (data[1] == 0x7D)                offset = 1;
    else if (length >= 5 && data[2] == 0x7D) offset = 2;
    else return;

    uint8_t msgType = data[offset + 1];

    switch (msgType) {
        case 0x01:  // Tempo update: [F0, 7D, 01, bpm_msb, bpm_lsb, F7]
            if (length >= (uint16_t)(offset + 4)) {
                uint16_t bpmScaled = ((uint16_t)data[offset + 2] << 7) | data[offset + 3];
                tempoBPM = bpmScaled / 10.0f;
            }
            break;

        case 0x02:  // Deprecated (old CC-indexed waveform param) -- no-op
            break;

        case 0x03:  // Reset frame counter
            frameCounter = 0;
            break;

        case 0x05:  // Deprecated (old CC-indexed full waveform) -- no-op
            break;

        case 0x06:  // Request save to persistent memory
            _saveStateRequested = true;
            break;

        case 0x09:  // Set active LED count: [F0, 7D, 09, countHi, countLo, F7]
            if (length >= (uint16_t)(offset + 4)) {
                int n = (((int)data[offset + 2]) << 7) | (int)data[offset + 3];
                setNumLeds(n);
            }
            break;

        case 0x0B:  // Master lights enable: [F0, 7D, 0B, on, F7]
            if (length >= (uint16_t)(offset + 4))
                lightsEnabled = data[offset + 2] != 0;
            break;

        case 0x07:  // Full panel TemporalConfig (atomic)
            // [F0, 7D, 07, layerIdx, panelId, paramId,
            //  profile, amplitude, offset, phaseL, phaseH, period, direction, F7]
            if (length >= (uint16_t)(offset + 12)) {
                TemporalConfig* tc = resolveTemporalConfig(
                    layers,
                    (int)data[offset + 2],
                    (int)data[offset + 3],
                    (int)data[offset + 4]);
                if (tc) {
                    tc->profile    = data[offset + 5] & 0x03;
                    tc->amplitude  = data[offset + 6];
                    tc->offset     = data[offset + 7];
                    tc->phaseShift = (uint16_t)data[offset + 8] | ((uint16_t)data[offset + 9] << 7);
                    tc->period     = (data[offset + 10] > 0) ? data[offset + 10] : 1;
                    tc->direction  = data[offset + 11] != 0;
                }
            }
            break;

        case 0x0C: {  // Full layer bundle: mode + waveshapes + all 16 TC slots in one packet.
            // [F0, 7D, 0C, layerIdx, mode, hueWave, satWave, valWave,
            //  <16 slots x 7 bytes: profile, amplitude, offset, phaseL, phaseH,
            //  period, direction>, F7].  Slot order matches pushAllLayersToHardware().
            static const struct { int8_t panelId, paramId; } kBundleSlots[16] = {
                { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
                { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 },
                { 2, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 },
                { 3, 0 }, { 4, 0 }, { 5, 0 }, { 6, 0 },
            };
            if (length >= (uint16_t)(offset + 120)) {
                const int layerIdx = (int)data[offset + 2];
                if (layerIdx >= 0 && layerIdx <= 15) {
                    const uint8_t ch = (uint8_t)(layerIdx + 1);
                    handleControlChange(ch, 1, data[offset + 3]);
                    handleControlChange(ch, 3, data[offset + 4]);
                    handleControlChange(ch, 4, data[offset + 5]);
                    handleControlChange(ch, 5, data[offset + 6]);

                    const uint8_t* p = data + offset + 7;
                    for (int i = 0; i < 16; i++, p += 7) {
                        TemporalConfig* tc = resolveTemporalConfig(
                            layers, layerIdx, kBundleSlots[i].panelId, kBundleSlots[i].paramId);
                        if (!tc) continue;
                        tc->profile    = p[0] & 0x03;
                        tc->amplitude  = p[1];
                        tc->offset     = p[2];
                        tc->phaseShift = (uint16_t)p[3] | ((uint16_t)p[4] << 7);
                        tc->period     = (p[5] > 0) ? p[5] : 1;
                        tc->direction  = p[6] != 0;
                    }
                }
            }
            break;
        }

        case 0x0A: {  // Full-state sync fragment: [F0, 7D, 0A, seq, total, payload..., F7]
            const int fragBytes = (int)length - offset - 3;  // seq + total + payload (excludes F7)
            if (fragBytes >= 3)
                handleStateSyncFragment(data + offset + 2, (uint16_t)fragBytes);
            break;
        }

        case 0x08:  // Single panel TemporalConfig field
            // [F0, 7D, 08, layerIdx, panelId, paramId, wfField, value, F7]
            // wfField: 0=profile, 1=amplitude, 2=offset, 3=phaseL, 4=phaseH,
            //          5=period, 6=direction
            if (length >= (uint16_t)(offset + 7)) {
                TemporalConfig* tc = resolveTemporalConfig(
                    layers,
                    (int)data[offset + 2],
                    (int)data[offset + 3],
                    (int)data[offset + 4]);
                uint8_t wfField = data[offset + 5];
                uint8_t value   = data[offset + 6];
                if (tc) {
                    switch (wfField) {
                        case 0: tc->profile    = value & 0x03; break;
                        case 1: tc->amplitude  = value; break;
                        case 2: tc->offset     = value; break;
                        case 3: tc->phaseShift = (tc->phaseShift & 0x3F80) | value; break;
                        case 4: tc->phaseShift = (tc->phaseShift & 0x007F) | ((uint16_t)value << 7); break;
                        case 5: tc->period     = (value > 0) ? value : 1; break;
                        case 6: tc->direction  = (value != 0); break;
                    }
                }
            }
            break;

        default:
            break;
    }
}

// ============================================================================
// SysEx 0x0A — Full-State Sync Reassembly
// ============================================================================

// Decode 7-bit MIDI bulk data (1 packed-MSBs byte + up to 7 data bytes per
// group) into raw bytes.  Writes at most rawCap bytes into rawOut; returns the
// number of raw bytes produced.  rawOut may alias encoded (in-place decode):
// each 8-byte group shrinks to 7, so writes always trail reads.
static size_t decode7bit(const uint8_t* encoded, size_t encodedLen, uint8_t* rawOut, size_t rawCap) {
    size_t rawIdx = 0;
    size_t i = 0;
    while (i < encodedLen) {
        uint8_t msbs = encoded[i++];
        for (int k = 0; k < 7 && i < encodedLen; k++, i++) {
            if (rawIdx >= rawCap) return rawIdx;
            rawOut[rawIdx++] = (encoded[i] & 0x7F) | ((msbs >> k) & 1) << 7;
        }
    }
    return rawIdx;
}

void LightEngine::handleStateSyncFragment(const uint8_t* frag, uint16_t len) {
    if (len < 3) return;  // need seqNum, totalFrags, and at least one payload byte
    const uint8_t  seqNum     = frag[0];
    const uint8_t  totalFrags = frag[1];
    const uint8_t* fragData   = frag + 2;
    const uint16_t fragLen    = (uint16_t)(len - 2);
    const bool     isLast     = (seqNum + 1 == totalFrags);

    // Validate the header before touching any buffer.
    if (totalFrags == 0 || totalFrags > SYNC_MAX_FRAGS || seqNum >= totalFrags) return;
    // All fragments carry exactly SYNC_PAYLOAD_PER_FRAG encoded bytes except
    // the last, which may be shorter (but never longer or empty).
    if ((!isLast && fragLen != SYNC_PAYLOAD_PER_FRAG) ||
        ( isLast && (fragLen == 0 || fragLen > SYNC_PAYLOAD_PER_FRAG))) return;

    if (!syncBuf_) {
        syncBuf_ = new uint8_t[(size_t)SYNC_MAX_FRAGS * SYNC_PAYLOAD_PER_FRAG];
        if (!syncBuf_) return;  // out of memory — drop the fragment
    }

    // Start a fresh reassembly when the expected count changes or the previous
    // partial sync has gone stale.  Staleness is measured in render frames
    // (~30 fps) so it behaves identically on firmware and in the plugin.  A
    // frame-counter reset (SysEx 0x03) mid-push at worst restarts reassembly,
    // which self-heals on the next push.
    if (totalFrags != syncTotalFrags_ ||
        (uint32_t)(frameCounter - syncLastFragFrame_) > SYNC_STALE_FRAMES) {
        syncTotalFrags_  = totalFrags;
        syncFragsMask_   = 0;
        syncLastFragLen_ = 0;
    }
    syncLastFragFrame_ = frameCounter;

    // Fixed-offset store: duplicated or reordered fragments rewrite the same
    // region.  seqNum <= SYNC_MAX_FRAGS-1 and fragLen <= SYNC_PAYLOAD_PER_FRAG,
    // so this cannot exceed the buffer.
    memcpy(syncBuf_ + (size_t)seqNum * SYNC_PAYLOAD_PER_FRAG, fragData, fragLen);
    syncFragsMask_ |= (uint16_t)(1u << seqNum);
    if (isLast) syncLastFragLen_ = fragLen;

    const uint16_t allMask = (uint16_t)((1u << totalFrags) - 1);
    if (syncFragsMask_ != allMask) return;

    const size_t encodedLen =
        (size_t)(totalFrags - 1) * SYNC_PAYLOAD_PER_FRAG + syncLastFragLen_;

    // A legitimate sync decodes to exactly STATE_SIZE raw bytes: each full
    // 8-byte group yields 7, a trailing partial group of m yields m-1.
    const size_t impliedRaw = (encodedLen / 8) * 7 +
                              ((encodedLen % 8) > 0 ? (encodedLen % 8) - 1 : 0);
    if (impliedRaw == STATE_SIZE) {
        // Decode in place (see decode7bit) to avoid a ~1.9 KB stack buffer —
        // BLE-MIDI callbacks can run on small stacks.
        const size_t rawLen = decode7bit(syncBuf_, encodedLen, syncBuf_, encodedLen);
        if (rawLen == STATE_SIZE)
            deserializeState(syncBuf_, rawLen);
    }
    syncTotalFrags_  = 0;  // back to idle for the next sync
    syncFragsMask_   = 0;
    syncLastFragLen_ = 0;
}

// ============================================================================
// Save-State Flag
// ============================================================================

bool LightEngine::saveStateRequested() {
    if (_saveStateRequested) {
        _saveStateRequested = false;
        return true;
    }
    return false;
}

// ============================================================================
// C API Implementation (Windows DLL / shared library)
// ============================================================================

#ifndef ARDUINO

extern "C" {

void* lightEngine_create(int numLeds) {
    return new LightEngine(numLeds);
}

void lightEngine_destroy(void* engine) {
    delete static_cast<LightEngine*>(engine);
}

void lightEngine_handleControlChange(void* engine, uint8_t channel, uint8_t control, uint8_t value) {
    static_cast<LightEngine*>(engine)->handleControlChange(channel, control, value);
}

void lightEngine_handleNoteOn(void* engine, uint8_t channel, uint8_t note, uint8_t velocity) {
    static_cast<LightEngine*>(engine)->handleNoteOn(channel, note, velocity);
}

void lightEngine_handleNoteOff(void* engine, uint8_t channel, uint8_t note, uint8_t velocity) {
    static_cast<LightEngine*>(engine)->handleNoteOff(channel, note, velocity);
}

void lightEngine_handleSysEx(void* engine, const uint8_t* data, uint16_t length) {
    static_cast<LightEngine*>(engine)->handleSysEx(data, length);
}

void lightEngine_render(void* engine) {
    static_cast<LightEngine*>(engine)->render();
}

const HSVColor* lightEngine_getLEDs(void* engine) {
    return static_cast<LightEngine*>(engine)->getLEDs();
}

const RGBColor* lightEngine_getRGB(void* engine) {
    return static_cast<LightEngine*>(engine)->getRGB();
}

size_t lightEngine_serializeState(void* engine, uint8_t* buf, size_t bufLen) {
    return static_cast<LightEngine*>(engine)->serializeState(buf, bufLen);
}

int lightEngine_getNumLEDs(void* engine) {
    return static_cast<LightEngine*>(engine)->getNumLEDs();
}

int lightEngine_getLayerCC(void* engine, int layer, int cc) {
    return static_cast<LightEngine*>(engine)->getLayerCC(layer, cc);
}

void lightEngine_setLayerCC(void* engine, int layer, int cc, int value) {
    static_cast<LightEngine*>(engine)->setLayerCC(layer, cc, value);
}

void lightEngine_setNumLeds(void* engine, int n) {
    static_cast<LightEngine*>(engine)->setNumLeds(n);
}

void lightEngine_setLightsEnabled(void* engine, int enabled) {
    static_cast<LightEngine*>(engine)->setLightsEnabled(enabled != 0);
}

const char* lightEngine_getEngineName() {
    return "Light Engine v2.0";
}

int lightEngine_getEngineVersion() {
    return 2;
}

int lightEngine_getParameterCount() {
    return 5;
}

const char* lightEngine_getParameterName(int ccNumber) {
    const ParameterInfo* info = getParameterInfo(ccNumber);
    return info ? info->name : "";
}

const char* lightEngine_getParameterTooltip(int ccNumber) {
    const ParameterInfo* info = getParameterInfo(ccNumber);
    return info ? info->tooltip : "";
}

int lightEngine_getModeCount() {
    return getModeCount();
}

const char* lightEngine_getModeName(int modeId) {
    const ModeInfo* info = getModeInfo(modeId);
    return info ? info->name : "";
}

} // extern "C"

#endif // !ARDUINO
