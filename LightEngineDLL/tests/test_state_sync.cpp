/*
  Host-side test for LightEngine state serialization (v6) and the SysEx 0x0A
  full-state sync reassembly.  Links the real engine — no mirrored logic.

  Build & run (Windows, from a VS developer prompt):
    cl /nologo /W4 /EHsc /O2 /fsanitize=address /I..\..\libraries\LightEngine\src ^
       test_state_sync.cpp ..\..\libraries\LightEngine\src\LightEngine.cpp ^
       /Fe:test_state_sync.exe
    test_state_sync.exe

  The plugin-side 7-bit encoder/fragmenter below is copied from
  PluginProcessor::sendWholeEngineState — keep them in sync.
*/

#include "LightEngine.h"

#include <cstdio>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <vector>
#include <random>

static int gFailures = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("FAIL: %s\n", msg); ++gFailures; } } while (0)

// ---------------------------------------------------------------------------
// Plugin-side encoder — copied from PluginProcessor::sendWholeEngineState.
// Produces fully framed SysEx messages: [F0, 7D, 0A, seq, total, payload..., F7]
// ---------------------------------------------------------------------------
static std::vector<std::vector<uint8_t>> encodeAndFragment(const uint8_t* rawState, size_t len) {
    std::vector<uint8_t> encoded;
    for (size_t i = 0; i < len; ) {
        const size_t groupSize = (len - i < 7) ? (len - i) : 7;
        uint8_t msbs = 0;
        for (size_t k = 0; k < groupSize; ++k)
            msbs |= (uint8_t)(((rawState[i + k] >> 7) & 1) << k);
        encoded.push_back(msbs);
        for (size_t k = 0; k < groupSize; ++k)
            encoded.push_back(rawState[i + k] & 0x7F);
        i += groupSize;
    }

    const size_t PAYLOAD_PER_FRAG = 192;
    const int totalFrags = (int)((encoded.size() + PAYLOAD_PER_FRAG - 1) / PAYLOAD_PER_FRAG);
    std::vector<std::vector<uint8_t>> msgs;
    for (int seq = 0; seq < totalFrags; ++seq) {
        const size_t offset = (size_t)seq * PAYLOAD_PER_FRAG;
        const size_t payloadLen = (encoded.size() - offset < PAYLOAD_PER_FRAG)
                                ? (encoded.size() - offset) : PAYLOAD_PER_FRAG;
        std::vector<uint8_t> m;
        m.push_back(0xF0);
        m.push_back(0x7D);
        m.push_back(0x0A);
        m.push_back((uint8_t)seq);
        m.push_back((uint8_t)totalFrags);
        m.insert(m.end(), encoded.begin() + offset, encoded.begin() + offset + payloadLen);
        m.push_back(0xF7);
        msgs.push_back(std::move(m));
    }
    return msgs;
}

static void feed(LightEngine& e, const std::vector<uint8_t>& msg) {
    e.handleSysEx(msg.data(), (uint16_t)msg.size());
}

// Randomize an engine's state through its public interface so a serialized
// snapshot has non-default content in every layer.
static void scrambleEngine(LightEngine& e, std::mt19937& rng) {
    for (int layer = 0; layer < 16; ++layer) {
        for (int cc = 1; cc <= 5; ++cc)
            e.setLayerCC(layer, cc, (int)(rng() % 128));
        for (int panel = 0; panel <= 6; ++panel) {
            for (int param = 0; param <= 3; ++param) {
                uint8_t sysex[14] = {
                    0xF0, 0x7D, 0x07,
                    (uint8_t)layer, (uint8_t)panel, (uint8_t)param,
                    (uint8_t)(rng() % 4),    // profile
                    (uint8_t)(rng() % 128),  // amplitude
                    (uint8_t)(rng() % 128),  // offset
                    (uint8_t)(rng() % 128),  // phaseL
                    (uint8_t)(rng() % 128),  // phaseH
                    (uint8_t)(1 + rng() % 127),  // period
                    (uint8_t)(rng() % 2),    // direction
                    0xF7
                };
                e.handleSysEx(sysex, 14);
                if (panel >= 3) break;  // mask panels ignore paramId
            }
        }
    }
}

int main() {
    std::mt19937 rng(20260612);

    // --- Test 1: v6 serialize → 0x0A fragments → handleSysEx round-trip -----
    {
        LightEngine src(321);   // non-default LED count must survive the trip
        scrambleEngine(src, rng);
        uint8_t snapshot[LightEngine::STATE_SIZE];
        CHECK(src.serializeState(snapshot, sizeof(snapshot)) == LightEngine::STATE_SIZE,
              "T1: serializeState size");

        LightEngine dst(108);
        dst.numLedsChanged();   // clear construction-time flag
        auto msgs = encodeAndFragment(snapshot, sizeof(snapshot));
        CHECK(msgs.size() == 12, "T1: expected 12 fragments");
        for (auto& m : msgs) feed(dst, m);

        uint8_t echo[LightEngine::STATE_SIZE];
        CHECK(dst.serializeState(echo, sizeof(echo)) == LightEngine::STATE_SIZE,
              "T1: echo serialize size");
        CHECK(memcmp(snapshot, echo, LightEngine::STATE_SIZE) == 0,
              "T1: state round-trip mismatch");
        CHECK(dst.getNumLEDs() == 321, "T1: LED count not applied from state");
        CHECK(dst.numLedsChanged(), "T1: numLedsChanged flag not set for firmware");
        printf("T1 v6 round-trip via 0x0A: ok\n");
    }

    // --- Test 2: reversed + duplicated fragment delivery ---------------------
    {
        LightEngine src(777);
        scrambleEngine(src, rng);
        uint8_t snapshot[LightEngine::STATE_SIZE];
        src.serializeState(snapshot, sizeof(snapshot));

        LightEngine dst(108);
        auto msgs = encodeAndFragment(snapshot, sizeof(snapshot));
        for (auto it = msgs.rbegin(); it != msgs.rend(); ++it) { feed(dst, *it); feed(dst, *it); }

        uint8_t echo[LightEngine::STATE_SIZE];
        dst.serializeState(echo, sizeof(echo));
        CHECK(memcmp(snapshot, echo, LightEngine::STATE_SIZE) == 0,
              "T2: reversed/duplicated delivery mismatch");
        CHECK(dst.getNumLEDs() == 777, "T2: LED count mismatch");
        printf("T2 reversed + duplicated fragments: ok\n");
    }

    // --- Test 3: v5-sized / wrong-version data rejected ----------------------
    {
        LightEngine e(108);
        uint8_t v5[1860];
        memset(v5, 0, sizeof(v5));
        v5[0] = 0x4C; v5[1] = 0x05;
        CHECK(!e.deserializeState(v5, sizeof(v5)), "T3: v5 blob must be rejected");
        uint8_t v6bad[LightEngine::STATE_SIZE];
        memset(v6bad, 0, sizeof(v6bad));
        v6bad[0] = 0x4C; v6bad[1] = 0x07;
        CHECK(!e.deserializeState(v6bad, sizeof(v6bad)), "T3: future version must be rejected");
        CHECK(!e.deserializeState(v6bad, 4), "T3: short buffer must be rejected");
        printf("T3 version/length rejection: ok\n");
    }

    // --- Test 4: corrupt mode bytes and LED count are clamped ----------------
    {
        LightEngine src(108);
        uint8_t snapshot[LightEngine::STATE_SIZE];
        src.serializeState(snapshot, sizeof(snapshot));
        snapshot[4] = 0xFF; snapshot[5] = 0xFF;  // LED count 65535
        snapshot[6] = 0xC9;                       // layer 0 mode byte = 201
        LightEngine dst(108);
        CHECK(dst.deserializeState(snapshot, sizeof(snapshot)), "T4: should still apply");
        CHECK(dst.getNumLEDs() == LightEngine::MAX_LEDS, "T4: LED count not clamped");
        CHECK(dst.getLayerCC(0, 1) == 0, "T4: corrupt mode not clamped to Off");
        printf("T4 corruption clamping: ok\n");
    }

    // --- Test 5: adversarial / garbage fragments are memory-safe (ASan) ------
    {
        LightEngine e(108);
        std::uniform_int_distribution<int> len(0, 260);
        std::uniform_int_distribution<int> bytev(0, 255);
        for (int iter = 0; iter < 100000; ++iter) {
            std::vector<uint8_t> m;
            m.push_back(0xF0);
            m.push_back(0x7D);
            m.push_back(0x0A);
            int n = len(rng);
            for (int i = 0; i < n; ++i) m.push_back((uint8_t)(bytev(rng) & 0x7F));
            // occasionally corrupt seq/total wildly
            if (m.size() > 4 && (rng() & 1)) { m[3] = (uint8_t)bytev(rng); }
            if (m.size() > 5 && (rng() & 1)) { m[4] = (uint8_t)bytev(rng); }
            m.push_back(0xF7);
            feed(e, m);
            if ((iter & 0xFFF) == 0) e.render();  // advance frames so staleness paths run
        }
        printf("T5 fuzz 100k garbage fragments: ok\n");
    }

    // --- Test 6: interrupted push followed by a clean push recovers ----------
    {
        LightEngine src(444);
        scrambleEngine(src, rng);
        uint8_t snapshot[LightEngine::STATE_SIZE];
        src.serializeState(snapshot, sizeof(snapshot));

        LightEngine dst(108);
        auto msgs = encodeAndFragment(snapshot, sizeof(snapshot));
        for (size_t i = 0; i < 5; ++i) feed(dst, msgs[i]);   // partial push, then "lost" frames
        for (int f = 0; f < 120; ++f) dst.render();          // > SYNC_STALE_FRAMES
        for (auto& m : msgs) feed(dst, m);                   // clean re-push

        uint8_t echo[LightEngine::STATE_SIZE];
        dst.serializeState(echo, sizeof(echo));
        CHECK(memcmp(snapshot, echo, LightEngine::STATE_SIZE) == 0,
              "T6: recovery after stale partial push failed");
        printf("T6 stale-partial recovery: ok\n");
    }

    // --- Test 7: getRGB output matches reference spectrum HSV conversion -----
    // Reference copy of the engine's spectrum hsvToRgbF (LightEngine.cpp) —
    // the contract is: rgbOut == this conversion of the layer's HSV, so the
    // firmware (writing rgbOut verbatim) matches the preview (drawing rgbOut
    // verbatim) and both match the engine's blending math.
    {
        auto refConvert = [](uint8_t h, uint8_t s, uint8_t v, uint8_t out[3]) {
            float vf = v / 255.0f, sf = s / 255.0f;
            float r, g, b;
            if (sf == 0.0f) { r = g = b = vf; }
            else {
                float hf = (h / 255.0f) * 6.0f;
                int   i  = (int)hf;
                float f  = hf - i;
                float p  = vf * (1.0f - sf);
                float q  = vf * (1.0f - sf * f);
                float t  = vf * (1.0f - sf * (1.0f - f));
                switch (i % 6) {
                    case 0:  r = vf; g = t;  b = p;  break;
                    case 1:  r = q;  g = vf; b = p;  break;
                    case 2:  r = p;  g = vf; b = t;  break;
                    case 3:  r = p;  g = q;  b = vf; break;
                    case 4:  r = t;  g = p;  b = vf; break;
                    default: r = vf; g = p;  b = q;  break;
                }
            }
            out[0] = (uint8_t)(r * 255.0f + 0.5f);
            out[1] = (uint8_t)(g * 255.0f + 0.5f);
            out[2] = (uint8_t)(b * 255.0f + 0.5f);
        };

        int mismatches = 0;
        for (int hueOff = 0; hueOff <= 127 && mismatches < 5; hueOff += 3) {
            LightEngine e(8);
            e.setLayerCC(0, 1, 1);    // layer 0: Solid
            // Opacity moved to SysEx (panelId 6); CC 2 is a no-op. Set base opaque.
            // Layout: [F0,7D,07, layer, panel=6, param=0, profile=0, amp=0,
            //          offset, phaseL=0, phaseH=0, period=4, dir=1, F7]
            uint8_t opac[14] = { 0xF0, 0x7D, 0x07, 0, 6, 0,
                                 0, 0, 127, 0, 0, 4, 1, 0xF7 };
            e.handleSysEx(opac, 14);
            // Flat color via TC offsets (amplitude defaults to 0):
            // panel 0=hue,1=sat,2=val; param 1=offsetTemporal; offset doubles in evalPanel.
            auto setOffset = [&](int panel, int offset) {
                uint8_t sysex[14] = { 0xF0, 0x7D, 0x07, 0, (uint8_t)panel, 1,
                                      0, 0, (uint8_t)offset, 0, 0, 4, 1, 0xF7 };
                e.handleSysEx(sysex, 14);
            };
            setOffset(0, hueOff);  // hue   = hueOff*2
            setOffset(1, 127);     // sat   = 254
            setOffset(2, 64);      // value = 128
            e.render();

            uint8_t expected[3];
            refConvert((uint8_t)(hueOff * 2), 254, 128, expected);
            const RGBColor* rgb = e.getRGB();
            for (int ch = 0; ch < 3; ++ch) {
                int got = ch == 0 ? rgb[0].r : ch == 1 ? rgb[0].g : rgb[0].b;
                if (got < expected[ch] - 2 || got > expected[ch] + 2) {
                    printf("  hueOff %d ch %d: got %d expected %d\n",
                           hueOff, ch, got, expected[ch]);
                    ++mismatches;
                }
            }
        }
        CHECK(mismatches == 0, "T7: getRGB deviates from reference spectrum conversion");
        if (mismatches == 0) printf("T7 RGB output matches reference conversion: ok\n");
    }

    // --- Test 8: SysEx 0x0C full-layer bundle matches individual CC + 0x07 ---
    // Verifies the coalesced bulk-sync format (one packet per layer, used by
    // PluginProcessor::pushAllLayersToHardware to cut a 16-layer sync from up
    // to ~320 rtpMIDI packets down to 16) decodes to the same engine state as
    // the CC1/3-5 + per-slot 0x07 messages it replaces.
    {
        static const struct { int panelId, paramId; } kSlots[16] = {
            { 0, 0 }, { 0, 1 }, { 0, 2 }, { 0, 3 },
            { 1, 0 }, { 1, 1 }, { 1, 2 }, { 1, 3 },
            { 2, 0 }, { 2, 1 }, { 2, 2 }, { 2, 3 },
            { 3, 0 }, { 4, 0 }, { 5, 0 }, { 6, 0 },
        };

        LightEngine ref(108);
        LightEngine test(108);

        for (int layer = 0; layer < 16; ++layer) {
            const uint8_t mode    = (uint8_t)(rng() % getModeCount());
            const uint8_t hueWave = (uint8_t)(rng() % 4);
            const uint8_t satWave = (uint8_t)(rng() % 4);
            const uint8_t valWave = (uint8_t)(rng() % 4);

            ref.setLayerCC(layer, 1, mode);
            ref.setLayerCC(layer, 3, hueWave);
            ref.setLayerCC(layer, 4, satWave);
            ref.setLayerCC(layer, 5, valWave);

            uint8_t bundle[121];
            bundle[0] = 0xF0; bundle[1] = 0x7D; bundle[2] = 0x0C;
            bundle[3] = (uint8_t)layer;
            bundle[4] = mode; bundle[5] = hueWave; bundle[6] = satWave; bundle[7] = valWave;

            uint8_t* p = bundle + 8;
            for (const auto& s : kSlots) {
                uint8_t profile   = (uint8_t)(rng() % 4);
                uint8_t amplitude = (uint8_t)(rng() % 128);
                uint8_t offsetV   = (uint8_t)(rng() % 128);
                uint8_t phaseL    = (uint8_t)(rng() % 128);
                uint8_t phaseH    = (uint8_t)(rng() % 128);
                uint8_t period    = (uint8_t)(1 + rng() % 127);
                uint8_t direction = (uint8_t)(rng() % 2);

                uint8_t sysex07[14] = {
                    0xF0, 0x7D, 0x07,
                    (uint8_t)layer, (uint8_t)s.panelId, (uint8_t)s.paramId,
                    profile, amplitude, offsetV, phaseL, phaseH, period, direction,
                    0xF7
                };
                ref.handleSysEx(sysex07, 14);

                p[0] = profile; p[1] = amplitude; p[2] = offsetV;
                p[3] = phaseL;  p[4] = phaseH;    p[5] = period; p[6] = direction;
                p += 7;
            }
            bundle[120] = 0xF7;
            test.handleSysEx(bundle, sizeof(bundle));
        }

        uint8_t refState[LightEngine::STATE_SIZE];
        uint8_t testState[LightEngine::STATE_SIZE];
        CHECK(ref.serializeState(refState, sizeof(refState)) == LightEngine::STATE_SIZE,
              "T8: ref serialize size");
        CHECK(test.serializeState(testState, sizeof(testState)) == LightEngine::STATE_SIZE,
              "T8: test serialize size");
        CHECK(memcmp(refState, testState, LightEngine::STATE_SIZE) == 0,
              "T8: 0x0C bundle decode mismatches individual CC + 0x07 application");
        printf("T8 SysEx 0x0C full-layer bundle matches individual CC+0x07: ok\n");
    }

    if (gFailures == 0) { printf("\nALL TESTS PASSED\n"); return 0; }
    printf("\n%d FAILURES\n", gFailures);
    return 1;
}
