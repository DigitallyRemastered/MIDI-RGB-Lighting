/*
 * LightEngine Unit Tests — Catch2 v3
 *
 * Build:
 *   cmake -B build -DLIGHT_ENGINE_BUILD_TESTS=ON
 *   cmake --build build
 *   ctest --test-dir build --output-on-failure
 *
 * These tests compile LightEngine.cpp directly (no DLL export macro) so the
 * full C++ class API is available without going through function pointers.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cstring>
#include <vector>

#include "LightEngine.h"

// ============================================================================
// Helpers
// ============================================================================

// Build a standard [F0, 7D, msgType, ...payload..., F7] SysEx message.
static std::vector<uint8_t> sysex(uint8_t msgType,
                                  std::initializer_list<uint8_t> payload = {})
{
    std::vector<uint8_t> msg = {0xF0, 0x7D, msgType};
    msg.insert(msg.end(), payload.begin(), payload.end());
    msg.push_back(0xF7);
    return msg;
}

// Set a layer's opacity via SysEx 0x07 (panelId=6, paramId ignored).
// offset is 0-127; amplitude=0 means static (no time modulation).
static void setOpacity(LightEngine& engine, uint8_t layerIdx, uint8_t offset)
{
    auto msg = sysex(0x07, {
        layerIdx,
        6,      // panelId = opacity
        0,      // paramId (ignored for opacity)
        0,      // profile = SAWTOOTH
        0,      // amplitude = 0 (static)
        offset, // base opacity value
        0, 0,   // phaseShift = 0
        4,      // period index (unused here: amplitude = 0 -> static)
        1       // direction = forward
    });
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));
}

// ============================================================================
// CC Routing
// ============================================================================

TEST_CASE("CC1 sets layer mode via MIDI channel", "[cc]")
{
    LightEngine engine(10);

    // MIDI channel 1 → layer 0, CC1 value 1 = MODE_SOLID
    engine.handleControlChange(1, 1, 1);
    REQUIRE(engine.getLayerCC(0, 1) == MODE_SOLID);

    // MIDI channel 16 → layer 15, CC1 value 3 = MODE_COMETS
    engine.handleControlChange(16, 1, 3);
    REQUIRE(engine.getLayerCC(15, 1) == MODE_COMETS);
}

TEST_CASE("CC1 mode wraps via value % getModeCount()", "[cc]")
{
    LightEngine engine(10);
    const int modeCount = getModeCount();  // 11

    // value == modeCount wraps to 0 = MODE_OFF
    engine.handleControlChange(1, 1, (uint8_t)modeCount);
    REQUIRE(engine.getLayerCC(0, 1) == MODE_OFF);

    // value == modeCount+1 wraps to 1 = MODE_SOLID
    engine.handleControlChange(1, 1, (uint8_t)(modeCount + 1));
    REQUIRE(engine.getLayerCC(0, 1) == MODE_SOLID);

    // The top mode is reachable without wrapping.
    engine.handleControlChange(1, 1, MODE_FIREBALL);
    REQUIRE(engine.getLayerCC(0, 1) == MODE_FIREBALL);
}

TEST_CASE("CC3/4/5 set waveshapes (masked to 0-3)", "[cc]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 3, 3);  // hue waveshape = SINE
    engine.handleControlChange(1, 4, 2);  // sat waveshape = SQUARE
    engine.handleControlChange(1, 5, 1);  // val waveshape = TRIANGLE

    REQUIRE(engine.getLayerCC(0, 3) == WAVESHAPE_SINE);
    REQUIRE(engine.getLayerCC(0, 4) == WAVESHAPE_SQUARE);
    REQUIRE(engine.getLayerCC(0, 5) == WAVESHAPE_TRIANGLE);
}

TEST_CASE("CC waveshape masks high bits (value & 0x03)", "[cc]")
{
    LightEngine engine(10);
    // 0b10000001 & 0x03 = 0x01 = TRIANGLE
    engine.handleControlChange(1, 3, 0b10000001);
    REQUIRE(engine.getLayerCC(0, 3) == WAVESHAPE_TRIANGLE);
}

TEST_CASE("Invalid MIDI channels (0 and 17+) are ignored", "[cc]")
{
    LightEngine engine(10);
    // Default mode is MODE_OFF
    engine.handleControlChange(0,  1, MODE_SOLID);
    engine.handleControlChange(17, 1, MODE_SOLID);

    for (int layer = 0; layer < 16; layer++) {
        REQUIRE(engine.getLayerCC(layer, 1) == MODE_OFF);
    }
}

TEST_CASE("CC on different channels routes to correct layers", "[cc]")
{
    LightEngine engine(10);
    const int modeCount = getModeCount();
    for (uint8_t ch = 1; ch <= 16; ch++) {
        engine.handleControlChange(ch, 1, ch % modeCount);  // mode per layer (wraps at getModeCount())
    }
    for (int i = 0; i < 16; i++) {
        REQUIRE(engine.getLayerCC(i, 1) == (i + 1) % modeCount);
    }
}

TEST_CASE("setLayerCC / getLayerCC round-trip", "[cc]")
{
    LightEngine engine(10);
    engine.setLayerCC(3, 1, 2);  // layer 3, CC1 = 2 = MODE_MOVING_DOTS
    REQUIRE(engine.getLayerCC(3, 1) == MODE_MOVING_DOTS);
}

// ============================================================================
// LED Count
// ============================================================================

TEST_CASE("Constructor sets numLeds", "[leds]")
{
    LightEngine engine(60);
    REQUIRE(engine.getNumLEDs() == 60);
}

TEST_CASE("setNumLeds clamps to [1, MAX_LEDS]", "[leds]")
{
    LightEngine engine(10);

    engine.setNumLeds(0);
    REQUIRE(engine.getNumLEDs() == 1);

    engine.setNumLeds(LightEngine::MAX_LEDS + 1);
    REQUIRE(engine.getNumLEDs() == LightEngine::MAX_LEDS);
}

TEST_CASE("numLedsChanged fires once on change, clears on second read", "[leds]")
{
    LightEngine engine(10);
    engine.numLedsChanged();  // clear any init flag

    engine.setNumLeds(20);
    REQUIRE(engine.numLedsChanged() == true);
    REQUIRE(engine.numLedsChanged() == false);  // consumed
}

TEST_CASE("numLedsChanged does not fire when count is unchanged", "[leds]")
{
    LightEngine engine(10);
    engine.numLedsChanged();  // clear init flag

    engine.setNumLeds(10);    // same count — no change
    REQUIRE(engine.numLedsChanged() == false);
}

// ============================================================================
// State Serialization
// ============================================================================

TEST_CASE("serialize returns STATE_SIZE bytes", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE];
    size_t written = engine.serializeState(buf, sizeof(buf));
    REQUIRE(written == LightEngine::STATE_SIZE);
}

TEST_CASE("serialized buffer starts with magic and version", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));
    REQUIRE(buf[0] == 0x4C);
    REQUIRE(buf[1] == 0x06);
}

TEST_CASE("serialize returns 0 for undersized buffer", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE - 1];
    size_t written = engine.serializeState(buf, sizeof(buf));
    REQUIRE(written == 0);
}

TEST_CASE("round-trip: mode and waveshapes survive serialize/deserialize", "[serialize]")
{
    LightEngine src(50);
    src.handleControlChange(1,  1, MODE_COMETS);
    src.handleControlChange(1,  3, WAVESHAPE_SINE);
    src.handleControlChange(2,  1, MODE_FLASH);
    src.handleControlChange(16, 5, WAVESHAPE_SQUARE);

    uint8_t buf[LightEngine::STATE_SIZE];
    src.serializeState(buf, sizeof(buf));

    LightEngine dst(10);
    REQUIRE(dst.deserializeState(buf, sizeof(buf)) == true);

    REQUIRE(dst.getLayerCC(0, 1) == MODE_COMETS);
    REQUIRE(dst.getLayerCC(0, 3) == WAVESHAPE_SINE);
    REQUIRE(dst.getLayerCC(1, 1) == MODE_FLASH);
    REQUIRE(dst.getLayerCC(15, 5) == WAVESHAPE_SQUARE);
}

TEST_CASE("round-trip: LED count survives serialize/deserialize", "[serialize]")
{
    LightEngine src(200);
    uint8_t buf[LightEngine::STATE_SIZE];
    src.serializeState(buf, sizeof(buf));

    LightEngine dst(10);
    dst.deserializeState(buf, sizeof(buf));
    REQUIRE(dst.getNumLEDs() == 200);
}

TEST_CASE("deserialize fails on wrong magic byte", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));

    buf[0] = 0xFF;  // corrupt magic
    REQUIRE(engine.deserializeState(buf, sizeof(buf)) == false);
}

TEST_CASE("deserialize fails on wrong version byte", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));

    buf[1] = 0x05;  // old version
    REQUIRE(engine.deserializeState(buf, sizeof(buf)) == false);
}

TEST_CASE("deserialize fails on truncated buffer", "[serialize]")
{
    LightEngine engine(10);
    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));

    REQUIRE(engine.deserializeState(buf, LightEngine::STATE_SIZE - 1) == false);
}

// ============================================================================
// SysEx Handlers
// ============================================================================

TEST_CASE("SysEx 0x01 updates tempo", "[sysex]")
{
    // tempoBPM is private; verify indirectly via another serialize round-trip.
    // BPM = 140.0 → bpmScaled = 1400 = (10 << 7) | 120
    LightEngine engine(10);
    auto msg = sysex(0x01, {10, 120});  // (10<<7)|120 = 1400 → 140.0 BPM
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));

    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));
    // bytes [2-3] = BPM*10 big-endian = 1400 = 0x0578
    uint16_t bpmFixed = ((uint16_t)buf[2] << 8) | buf[3];
    REQUIRE(bpmFixed == 1400);
}

TEST_CASE("SysEx 0x06 sets saveStateRequested flag (consumed once)", "[sysex]")
{
    LightEngine engine(10);
    auto msg = sysex(0x06);
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));

    REQUIRE(engine.saveStateRequested() == true);
    REQUIRE(engine.saveStateRequested() == false);  // consumed
}

TEST_CASE("SysEx 0x09 sets LED count", "[sysex]")
{
    LightEngine engine(10);
    // count = 60 = (0<<7)|60
    auto msg = sysex(0x09, {0, 60});
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));
    REQUIRE(engine.getNumLEDs() == 60);
}

TEST_CASE("SysEx 0x09 clamps LED count to MAX_LEDS", "[sysex]")
{
    LightEngine engine(10);
    // Encode a count larger than MAX_LEDS.
    // MAX_LEDS = 1000; 1000 = (7<<7)|104
    // Use 1500 = (11<<7)|88 to exceed MAX_LEDS.
    auto msg = sysex(0x09, {11, 88});
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));
    REQUIRE(engine.getNumLEDs() == LightEngine::MAX_LEDS);
}

TEST_CASE("SysEx 0x0B disables lights", "[sysex]")
{
    LightEngine engine(10);
    REQUIRE(engine.getLightsEnabled() == true);

    auto off = sysex(0x0B, {0});
    engine.handleSysEx(off.data(), static_cast<uint16_t>(off.size()));
    REQUIRE(engine.getLightsEnabled() == false);

    auto on = sysex(0x0B, {1});
    engine.handleSysEx(on.data(), static_cast<uint16_t>(on.size()));
    REQUIRE(engine.getLightsEnabled() == true);
}

TEST_CASE("SysEx 0x07 sets a full TemporalConfig (opacity)", "[sysex]")
{
    LightEngine engine(10);
    // Set layer 0 opacity TC: panelId=6, profile=SAWTOOTH, amplitude=0, offset=127
    setOpacity(engine, 0, 127);

    // Verify via serialize round-trip: opacity TC offset is at a fixed location.
    // Layer 0 starts at byte 6, then: mode(1) + opacity TC(7) → offset byte is at
    // position 6 + 1 + 2 = 9 (mode, profile, amplitude, then offset).
    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));
    // Layer 0 layout: [6]=mode, [7]=opacity.profile, [8]=opacity.amplitude, [9]=opacity.offset
    REQUIRE(buf[9] == 127);
}

TEST_CASE("SysEx messages with wrong manufacturer byte are ignored", "[sysex]")
{
    LightEngine engine(10);
    // 0x7E is a different manufacturer — should be ignored
    uint8_t badMsg[] = {0xF0, 0x7E, 0x06, 0xF7};
    engine.handleSysEx(badMsg, sizeof(badMsg));
    REQUIRE(engine.saveStateRequested() == false);
}

TEST_CASE("SysEx 0x08 sets a single TC field (opacity offset)", "[sysex]")
{
    LightEngine engine(10);
    // [F0, 7D, 08, layerIdx=0, panelId=6, paramId=0, wfField=2(offset), value=64, F7]
    auto msg = sysex(0x08, {0, 6, 0, 2, 64});  // wfField=2 = offset
    engine.handleSysEx(msg.data(), static_cast<uint16_t>(msg.size()));

    uint8_t buf[LightEngine::STATE_SIZE];
    engine.serializeState(buf, sizeof(buf));
    // Layer 0: mode(1) + opacity.profile(1) + opacity.amplitude(1) + opacity.offset(1) = buf[9]
    REQUIRE(buf[9] == 64);
}

// ============================================================================
// MIDI Notes
// ============================================================================

TEST_CASE("NoteOn in GravityComet mode activates a comet slot", "[notes]")
{
    LightEngine engine(108);
    engine.handleControlChange(1, 1, MODE_GRAVITY_COMET);

    engine.handleNoteOn(1, 60, 100);

    // Serialize state to inspect comets — comets are not in the state blob,
    // so render a frame and check that the engine doesn't crash.
    setOpacity(engine, 0, 127);
    engine.render();
    // No assertion beyond "didn't crash" — comet position is physics-dependent.
    REQUIRE(engine.getNumLEDs() == 108);
}

TEST_CASE("NoteOff does not crash when no note was on", "[notes]")
{
    LightEngine engine(10);
    engine.handleNoteOff(1, 60, 0);  // should be a no-op
    REQUIRE(engine.getNumLEDs() == 10);
}

TEST_CASE("16 simultaneous NoteOn events fill MAX_COMET_POLY slots", "[notes]")
{
    LightEngine engine(108);
    engine.handleControlChange(1, 1, MODE_GRAVITY_COMET);
    setOpacity(engine, 0, 127);

    for (uint8_t note = 60; note < 60 + MAX_COMET_POLY; note++) {
        engine.handleNoteOn(1, note, 100);
    }
    // 17th note should be silently dropped (no crash, no slot corruption).
    engine.handleNoteOn(1, 90, 100);

    // Render several frames to let physics advance without crashing.
    for (int i = 0; i < 5; i++) {
        engine.render();
    }
    REQUIRE(engine.getNumLEDs() == 108);
}

// ============================================================================
// Note-triggered modes (Gate / Spikes / Fireball)
// ============================================================================

// Brightest single-channel value across the whole strip (0-255).
static int maxChannel(LightEngine& engine)
{
    const RGBColor* rgb = engine.getRGB();
    int m = 0;
    for (int i = 0; i < engine.getNumLEDs(); i++) {
        int v = rgb[i].r;
        if (rgb[i].g > v) v = rgb[i].g;
        if (rgb[i].b > v) v = rgb[i].b;
        if (v > m) m = v;
    }
    return m;
}

// Index of the brightest lit LED, or -1 if the strip is dark.
static int brightestLed(LightEngine& engine)
{
    const RGBColor* rgb = engine.getRGB();
    int best = -1, bestV = 0;
    for (int i = 0; i < engine.getNumLEDs(); i++) {
        int v = rgb[i].r;
        if (rgb[i].g > v) v = rgb[i].g;
        if (rgb[i].b > v) v = rgb[i].b;
        if (v > bestV) { bestV = v; best = i; }
    }
    return best;
}

TEST_CASE("Note-triggered modes are registered in mode metadata", "[modes]")
{
    REQUIRE(getModeCount() == 11);
    REQUIRE(getModeInfo(MODE_NOTE_GATE)       != nullptr);
    REQUIRE(getModeInfo(MODE_FIREBALL)        != nullptr);
    REQUIRE(strcmp(getModeInfo(MODE_NOTE_GATE)->name, "Note Gate") == 0);
    REQUIRE(strcmp(getModeInfo(MODE_FIREBALL)->name,  "Fireball")  == 0);
    REQUIRE(getModeInfo(getModeCount()) == nullptr);  // out-of-range rejected
}

TEST_CASE("MODE_NOTE_GATE: dark until a note is held, lit while held", "[notes][render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_NOTE_GATE);
    setOpacity(engine, 0, 127);

    // No note held → fully dark.
    engine.render();
    REQUIRE(maxChannel(engine) == 0);

    // Note held → layer shows its normal (solid) output.
    engine.handleNoteOn(1, 60, 100);
    engine.render();
    REQUIRE(maxChannel(engine) > 0);

    // Note released → dark again.
    engine.handleNoteOff(1, 60, 0);
    engine.render();
    REQUIRE(maxChannel(engine) == 0);
}

TEST_CASE("MODE_NOTE_GATE: polyphonic — stays lit until the last note releases", "[notes][render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_NOTE_GATE);
    setOpacity(engine, 0, 127);

    engine.handleNoteOn(1, 60, 100);
    engine.handleNoteOn(1, 64, 100);
    engine.handleNoteOff(1, 60, 0);
    engine.render();
    REQUIRE(maxChannel(engine) > 0);   // one note still held

    engine.handleNoteOff(1, 64, 0);
    engine.render();
    REQUIRE(maxChannel(engine) == 0);  // all released
}

TEST_CASE("MODE_BRIGHTNESS_SPIKE: Note On boosts brightness, then it decays", "[notes][render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_BRIGHTNESS_SPIKE);
    setOpacity(engine, 0, 127);

    engine.render();
    int base = maxChannel(engine);   // baseline brightness (default val offset)

    engine.handleNoteOn(1, 60, 127);
    engine.render();
    int spiked = maxChannel(engine);
    REQUIRE(spiked > base);

    for (int i = 0; i < 30; i++) engine.render();  // envelope decays (~0.3 s)
    REQUIRE(maxChannel(engine) < spiked);
}

TEST_CASE("MODE_SAT_SPIKE: Note On boosts saturation, then it decays", "[notes][render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_SAT_SPIKE);
    setOpacity(engine, 0, 127);
    // Lower the base saturation so the spike has headroom (default sat is full).
    // 0x07 payload: layer, panel(1=sat), param(1=offsetTemporal), profile, amp, offset, phaseL, phaseH, period, dir
    auto satMsg = sysex(0x07, { 0, 1, 1, 0, 0, 40, 0, 0, 4, 1 });
    engine.handleSysEx(satMsg.data(), static_cast<uint16_t>(satMsg.size()));

    auto maxSat = [&]() {
        const HSVColor* hsv = engine.getLEDs();
        int m = 0;
        for (int i = 0; i < engine.getNumLEDs(); i++) if (hsv[i].s > m) m = hsv[i].s;
        return m;
    };

    engine.render();
    int base = maxSat();

    engine.handleNoteOn(1, 60, 127);
    engine.render();
    int spiked = maxSat();
    REQUIRE(spiked > base);

    for (int i = 0; i < 30; i++) engine.render();
    REQUIRE(maxSat() < spiked);
}

TEST_CASE("MODE_HUE_SPIKE: Note On rotates hue, then it eases back", "[notes][render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_HUE_SPIKE);
    setOpacity(engine, 0, 127);

    engine.render();
    int baseHue = engine.getLEDs()[0].h;

    engine.handleNoteOn(1, 60, 127);
    engine.render();
    int spikedHue = engine.getLEDs()[0].h;
    REQUIRE(spikedHue != baseHue);

    for (int i = 0; i < 40; i++) engine.render();
    int settledHue = engine.getLEDs()[0].h;
    int drift = settledHue - baseHue;
    if (drift < 0) drift = -drift;
    REQUIRE(drift <= 3);  // returns to the baseline hue
}

TEST_CASE("MODE_FIREBALL: Note On sends a comet up the strip that burns out", "[notes][render]")
{
    LightEngine engine(300);
    engine.handleControlChange(1, 1, MODE_FIREBALL);
    setOpacity(engine, 0, 127);

    // No note yet → dark.
    engine.render();
    REQUIRE(brightestLed(engine) == -1);

    // Launch a fireball; after a couple of frames a low LED lights.
    engine.handleNoteOn(1, 60, 127);
    engine.render();
    engine.render();
    int early = brightestLed(engine);
    REQUIRE(early >= 0);

    // It keeps travelling upward — the lit head moves to a higher index.
    for (int i = 0; i < 6; i++) engine.render();
    int later = brightestLed(engine);
    REQUIRE(later > early);

    // Eventually it burns past the top and the layer goes dark again.
    for (int i = 0; i < 400; i++) engine.render();
    REQUIRE(brightestLed(engine) == -1);
}

// ============================================================================
// Rendering
// ============================================================================

TEST_CASE("Lights disabled: render() produces all-zero RGB output", "[render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_SOLID);
    setOpacity(engine, 0, 127);
    engine.setLightsEnabled(false);
    engine.render();

    const RGBColor* rgb = engine.getRGB();
    for (int i = 0; i < engine.getNumLEDs(); i++) {
        REQUIRE(rgb[i].r == 0);
        REQUIRE(rgb[i].g == 0);
        REQUIRE(rgb[i].b == 0);
    }
}

TEST_CASE("Lights re-enabled: render() produces nonzero output after SOLID mode", "[render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_SOLID);
    setOpacity(engine, 0, 127);  // fully opaque

    // Default val offsetTemporal.offset = 64 → val > 0, so output should be nonzero.
    engine.setLightsEnabled(true);
    engine.render();

    const RGBColor* rgb = engine.getRGB();
    bool anyNonzero = false;
    for (int i = 0; i < engine.getNumLEDs(); i++) {
        if (rgb[i].r || rgb[i].g || rgb[i].b) {
            anyNonzero = true;
            break;
        }
    }
    REQUIRE(anyNonzero == true);
}

TEST_CASE("MODE_OFF layer produces no contribution to output", "[render]")
{
    LightEngine engine(10);
    // All layers default to MODE_OFF; lights enabled; no opaque layer.
    engine.render();

    const RGBColor* rgb = engine.getRGB();
    for (int i = 0; i < engine.getNumLEDs(); i++) {
        REQUIRE(rgb[i].r == 0);
        REQUIRE(rgb[i].g == 0);
        REQUIRE(rgb[i].b == 0);
    }
}

TEST_CASE("numLeds=1: render does not crash or overwrite", "[render]")
{
    LightEngine engine(1);
    engine.handleControlChange(1, 1, MODE_SOLID);
    setOpacity(engine, 0, 127);
    engine.render();
    REQUIRE(engine.getNumLEDs() == 1);
    // Just verify the single pixel is accessible.
    (void)engine.getRGB()[0];
}

TEST_CASE("numLeds=MAX_LEDS: render does not crash", "[render]")
{
    LightEngine engine(LightEngine::MAX_LEDS);
    engine.handleControlChange(1, 1, MODE_SOLID);
    setOpacity(engine, 0, 127);
    engine.render();
    REQUIRE(engine.getNumLEDs() == LightEngine::MAX_LEDS);
}

TEST_CASE("Multiple render() calls advance frameCounter without crashing", "[render]")
{
    LightEngine engine(10);
    engine.handleControlChange(1, 1, MODE_MOVING_DOTS);
    setOpacity(engine, 0, 127);

    for (int i = 0; i < 90; i++) {  // 3 seconds at 30fps
        engine.render();
    }

    // Serialize to confirm internal state is still intact.
    uint8_t buf[LightEngine::STATE_SIZE];
    size_t written = engine.serializeState(buf, sizeof(buf));
    REQUIRE(written == LightEngine::STATE_SIZE);
}

// ============================================================================
// SysEx 0x0A — Full-State Sync Fragmentation
// ============================================================================

TEST_CASE("SysEx 0x0A: complete set of fragments applies deserializeState", "[sysex][0x0A]")
{
    // Prepare a source engine with known state.
    LightEngine src(50);
    src.handleControlChange(1, 1, MODE_FLASH);
    src.handleControlChange(2, 1, MODE_COMETS);

    uint8_t state[LightEngine::STATE_SIZE];
    src.serializeState(state, sizeof(state));

    // 7-bit encode state into fragments the same way the plugin sender would.
    // Each fragment carries SYNC_PAYLOAD_PER_FRAG = 192 bytes of encoded payload.
    // Encode: each 7-bit byte from the raw buffer (state bytes are already 0-127
    // after serialization — LightEngine ensures this).  We use a simple 1:1
    // pass-through since all bytes in a serialized state are ≤ 0x7F.
    // (The actual plugin encodes 8-bit data into 7-bit MIDI-safe bytes using an
    // 8→7 packing scheme, but we test the reassembly logic here by feeding the
    // raw state bytes directly, which are all ≤ 0x7F post-serialization.)

    static const int PAYLOAD = 192;
    int totalBytes = static_cast<int>(LightEngine::STATE_SIZE);
    int totalFrags  = (totalBytes + PAYLOAD - 1) / PAYLOAD;  // = 10

    LightEngine dst(10);

    for (int seq = 0; seq < totalFrags; seq++) {
        int payloadLen = (seq < totalFrags - 1) ? PAYLOAD
                       : (totalBytes - seq * PAYLOAD);

        // Fragment format: [F0, 7D, 0A, seq, totalFrags, ...payload..., F7]
        std::vector<uint8_t> frag = {
            0xF0, 0x7D, 0x0A,
            static_cast<uint8_t>(seq),
            static_cast<uint8_t>(totalFrags)
        };
        frag.insert(frag.end(),
                    state + seq * PAYLOAD,
                    state + seq * PAYLOAD + payloadLen);
        frag.push_back(0xF7);

        dst.handleSysEx(frag.data(), static_cast<uint16_t>(frag.size()));
    }

    // After all fragments are received, dst should have src's state.
    REQUIRE(dst.getLayerCC(0, 1) == MODE_FLASH);
    REQUIRE(dst.getLayerCC(1, 1) == MODE_COMETS);
}

TEST_CASE("SysEx 0x0A: out-of-order fragments still reassemble correctly", "[sysex][0x0A]")
{
    LightEngine src(30);
    src.handleControlChange(3, 1, MODE_GRAVITY_COMET);

    uint8_t state[LightEngine::STATE_SIZE];
    src.serializeState(state, sizeof(state));

    static const int PAYLOAD = 192;
    int totalBytes = static_cast<int>(LightEngine::STATE_SIZE);
    int totalFrags  = (totalBytes + PAYLOAD - 1) / PAYLOAD;

    // Collect fragments in reverse order to test out-of-order delivery.
    std::vector<std::vector<uint8_t>> frags;
    for (int seq = 0; seq < totalFrags; seq++) {
        int payloadLen = (seq < totalFrags - 1) ? PAYLOAD
                       : (totalBytes - seq * PAYLOAD);
        std::vector<uint8_t> frag = {
            0xF0, 0x7D, 0x0A,
            static_cast<uint8_t>(seq),
            static_cast<uint8_t>(totalFrags)
        };
        frag.insert(frag.end(),
                    state + seq * PAYLOAD,
                    state + seq * PAYLOAD + payloadLen);
        frag.push_back(0xF7);
        frags.push_back(frag);
    }

    LightEngine dst(10);
    for (int seq = totalFrags - 1; seq >= 0; seq--) {
        dst.handleSysEx(frags[seq].data(), static_cast<uint16_t>(frags[seq].size()));
    }

    REQUIRE(dst.getLayerCC(2, 1) == MODE_GRAVITY_COMET);
}
