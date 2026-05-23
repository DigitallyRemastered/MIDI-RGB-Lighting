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
    { 1, "Mode",           "Layer mode (0=Off,1=Solid,2=MovingDots,3=Comets,4=BackForth,5=Flash,6=GravityComet)"},
    { 2, "Opacity",        "Layer blend weight (0=transparent/skip, 127=opaque)"},
    { 3, "Hue Waveshape",  "Spatial waveshape for hue (0=Sawtooth,1=Triangle,2=Square,3=Sine)"},
    { 4, "Sat Waveshape",  "Spatial waveshape for saturation"},
    { 5, "Val Waveshape",  "Spatial waveshape for brightness"}
};

static const ModeInfo MODE_TABLE[] = {
    {0, "Off"},
    {1, "Solid"},
    {2, "Moving Dots"},
    {3, "Comets"},
    {4, "Back and Forth"},
    {5, "Flash"},
    {6, "Gravity Comet"}
};

const ParameterInfo* getParameterInfo(int ccNumber) {
    if (ccNumber < 1 || ccNumber > 5) return nullptr;
    return &PARAMETER_TABLE[ccNumber - 1];
}

int getAllParameters(const ParameterInfo** outArray) {
    *outArray = PARAMETER_TABLE;
    return 5;
}

int getModeCount() {
    return 7;
}

const ModeInfo* getModeInfo(int modeId) {
    if (modeId < 0 || modeId >= 7) return nullptr;
    return &MODE_TABLE[modeId];
}

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
    leds = new HSVColor[numLeds];
    for (int li = 0; li < MAX_LAYERS; li++) {
        layerBuffers[li] = new HSVColor[numLeds];
    }

    // All layers start with opacity=0 (invisible) and MODE_OFF
    for (int li = 0; li < MAX_LAYERS; li++) {
        Layer& layer = layers[li];
        layer.mode    = MODE_OFF;
        layer.opacity = 0;
        initColorComponent(layer.hue, 0,   127);  // hue:  offset=0,   wl=127
        initColorComponent(layer.sat, 127, 127);  // sat:  offset=127 (full), wl=127
        initColorComponent(layer.val, 64,  127);  // val:  offset=64  (half), wl=127
        initTemporalConfig(layer.linesTemporal);  layer.linesTemporal.offset = 1;  // 1 line
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

    memset(leds, 0, numLeds * sizeof(HSVColor));
    for (int li = 0; li < MAX_LAYERS; li++) {
        memset(layerBuffers[li], 0, numLeds * sizeof(HSVColor));
    }
}

LightEngine::~LightEngine() {
    delete[] leds;
    for (int li = 0; li < MAX_LAYERS; li++) {
        delete[] layerBuffers[li];
    }
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
    // CC 2 = Opacity (0=transparent/skip, 127=opaque)
    // CC 3 = Hue Waveshape  (0-3)
    // CC 4 = Sat Waveshape  (0-3)
    // CC 5 = Val Waveshape  (0-3)
    // All spatial amp/offset/wl/phase are set via SysEx TemporalConfig messages.
    switch (control) {
        case 1:  layer.mode              = value % 7;    break;
        case 2:  layer.opacity           = value;        break;
        case 3:  layer.hue.waveshape     = value & 0x03; break;
        case 4:  layer.sat.waveshape     = value & 0x03; break;
        case 5:  layer.val.waveshape     = value & 0x03; break;
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
        case 2: return l.opacity;
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
    buf[i++] = 0x03;  // version

    // tempoBPM as uint16 (BPM*10), big-endian
    uint16_t bpmFixed = (uint16_t)(tempoBPM * 10.0f + 0.5f);
    buf[i++] = (uint8_t)(bpmFixed >> 8);
    buf[i++] = (uint8_t)(bpmFixed & 0xFF);

    // 16 layers x 96 bytes each
    for (int li = 0; li < MAX_LAYERS; li++) {
        const Layer& layer = layers[li];
        buf[i++] = layer.mode;
        buf[i++] = layer.opacity;
        i += serializeComponent(buf + i, layer.hue);
        i += serializeComponent(buf + i, layer.sat);
        i += serializeComponent(buf + i, layer.val);
        i += serializeTC(buf + i, layer.linesTemporal);
    }

    return i;  // == STATE_SIZE
}

bool LightEngine::deserializeState(const uint8_t* buf, size_t len) {
    if (!buf || len < STATE_SIZE) return false;
    if (buf[0] != 0x4C || buf[1] != 0x03) return false;

    size_t i = 2;

    uint16_t bpmFixed = ((uint16_t)buf[i] << 8) | buf[i + 1];
    i += 2;
    tempoBPM = bpmFixed / 10.0f;

    for (int li = 0; li < MAX_LAYERS; li++) {
        Layer& layer = layers[li];
        layer.mode    = buf[i++];
        layer.opacity = buf[i++];
        i += deserializeComponent(buf + i, layer.hue);
        i += deserializeComponent(buf + i, layer.sat);
        i += deserializeComponent(buf + i, layer.val);
        i += deserializeTC(buf + i, layer.linesTemporal);
    }

    return true;
}

// ============================================================================
// Rendering Pipeline
// ============================================================================

void LightEngine::render() {
    frameCounter++;

    for (int li = 0; li < MAX_LAYERS; li++) {
        if (layers[li].opacity == 0 || layers[li].mode == MODE_OFF) continue;

        LayerEffectiveParams ep;
        applyTimeModulationForLayer(li, ep);

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

    out.lines         = applyTemporalConfig(layer.linesTemporal);
}

void LightEngine::renderLayer(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    switch (layers[layerIdx].mode) {
        case MODE_SOLID:         renderSolid       (layerIdx, buf, ep); break;
        case MODE_MOVING_DOTS:   renderMovingDots  (layerIdx, buf, ep); break;
        case MODE_COMETS:        renderComets      (layerIdx, buf, ep); break;
        case MODE_BACK_FORTH:    renderBackAndForth(layerIdx, buf, ep); break;
        case MODE_FLASH:         renderFlash       (layerIdx, buf, ep); break;
        case MODE_GRAVITY_COMET: renderGravityComet(layerIdx, buf, ep); break;
        default: break;
    }
}

void LightEngine::compositeLayersToOutput() {
    memset(leds, 0, numLeds * sizeof(HSVColor));

    for (int li = 0; li < MAX_LAYERS; li++) {
        if (layers[li].opacity == 0 || layers[li].mode == MODE_OFF) continue;

        float alpha = layers[li].opacity / 127.0f;
        if (alpha <= 0.0f) continue;
        if (alpha > 1.0f)  alpha = 1.0f;

        const HSVColor* layerBuf = layerBuffers[li];

        for (int i = 0; i < numLeds; i++) {
            if (layerBuf[i].v == 0) continue;  // LED is off in this layer
            float v_scaled = layerBuf[i].v * alpha;
            // Opacity scales brightness only; hue is circular and must not be
            // interpolated toward zero (that would rotate the colour).
            leds[i].h = layerBuf[i].h;
            leds[i].s = layerBuf[i].s;
            float total_v = (float)leds[i].v + v_scaled;
            leds[i].v = (uint8_t)(total_v > 254.0f ? 254.0f : total_v);
        }
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

    // hue.phaseShift (effective) = segment start LED
    // hue.wavelength (effective) = segment length in LEDs
    int startLed = (int)ep.huePhase;
    int segLen   = (int)ep.hueWavelength;
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

    int startLed = (int)ep.huePhase;
    int segLen   = (int)ep.hueWavelength;
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

void LightEngine::renderBackAndForth(int layerIdx, HSVColor* buf, const LayerEffectiveParams& ep) {
    const Layer& layer = layers[layerIdx];
    int startLed = (int)ep.huePhase;
    int segLen   = (int)ep.hueWavelength;
    if (segLen <= 0) return;

    for (int block = 0; block < numLeds; block += 2 * segLen) {
        for (int j = 0; j < segLen; j++) {
            int idx = (startLed * segLen + block + j) % numLeds;
            uint8_t h = wrapHue (evalPanel(layer.hue, idx, ep.hueAmp, ep.hueOffset, ep.hueWavelength, ep.huePhase));
            uint8_t s = clampSV (evalPanel(layer.sat, idx, ep.satAmp, ep.satOffset, ep.satWavelength, ep.satPhase));
            uint8_t v = clampSV (evalPanel(layer.val, idx, ep.valAmp, ep.valOffset, ep.valWavelength, ep.valPhase));
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
// panelId: 0=hue, 1=sat, 2=val, 3=mask (linesTemporal, paramId ignored)
// paramId: 0=ampTemporal, 1=offsetTemporal, 2=wavelengthTemporal, 3=phaseShiftTemporal
static TemporalConfig* resolveTemporalConfig(Layer* layers, int layerIdx, int panelId, int paramId) {
    if (layerIdx < 0 || layerIdx > 15) return nullptr;
    if (panelId  < 0 || panelId  >  3) return nullptr;

    if (panelId == 3) return &layers[layerIdx].linesTemporal;  // mask panel

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

int lightEngine_getNumLEDs(void* engine) {
    return static_cast<LightEngine*>(engine)->getNumLEDs();
}

int lightEngine_getLayerCC(void* engine, int layer, int cc) {
    return static_cast<LightEngine*>(engine)->getLayerCC(layer, cc);
}

void lightEngine_setLayerCC(void* engine, int layer, int cc, int value) {
    static_cast<LightEngine*>(engine)->setLayerCC(layer, cc, value);
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
