

# MIDI-RGB-

Lighting
Control individually addressable LEDs using the FastLED library and MIDI. Supports **Teensy** (USB-MIDI) and **ESP32** (WiFi/BLE-MIDI).

https://github.com/user-attachments/assets/a2517aeb-3479-4f27-bc56-3e1d3fea39d8

Thanks to [PCBWay](https://www.pcbway.com/) for providing their PCB manufacturing services after finding this repo. Their service was thorough and easy.

See a demo and video of construction here: https://www.youtube.com/watch?v=1hBeVJ6QlaU

## Requirements

### Hardware

Choose one of two controller options:

| Option | Hardware | Connection |
|---|---|---|
| **Teensy** (recommended) | Teensy 3.6 (or 3.2/4.x) | USB-MIDI (plug and play) |
| **ESP32** | Seeed XIAO ESP32S3 (or any ESP32) | WiFi (rtpMIDI) or BLE-MIDI |

The Teensy is configured as a USB-MIDI device via Teensyduino:
![image](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/Teensy%20USB%20MIDI.png)

The PSU must be rated for the power required by your LED strips. Each LED at full white brightness draws ~50 mA. For 108 LEDs: 108 × 0.05 A = 5.4 A → use a 5 V supply rated for at least 27 W with headroom.

A Digital Audio Workstation is required to send MIDI messages. The included template targets FL Studio.

## Quick Start: 

The Teensy will need to be connected as shown below:

![image](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/Circuit%20Schematic.png)

In the code:
Enter the initial number of RGB LEDs under the NUM_LEDS definition and enter the pin from which
data will be sent to the lights under the DATA_PIN definition. (The LED count can also be changed
at runtime over MIDI — see the SysEx table below.)

Upload the code to the board, leave the usb cable connected, and then turn on the 5V PSU.

Start FL Studio and press play!


## Concepts: 

All rendering logic lives in the shared `LightEngine` library
(`libraries/LightEngine/src/LightEngine.h` / `.cpp`). The same engine runs on the Teensy,
the ESP32, and inside a Windows DLL used by the Light Studio VST3 plugin, so every target
renders identically. The firmware renders at a fixed 30 Hz (one frame every 33,333 µs) and
pushes the result to the strip with FastLED.

### Layers

The engine renders **16 independent layers**, and the **MIDI channel selects the layer**:
messages on channel 1 control layer 1, channel 2 controls layer 2, and so on. Each layer has
its own mode, color configuration, and opacity. After all layers are rendered, they are
composited bottom-to-top with proper RGB alpha blending — opacity is itself an animatable
parameter, so layers can fade in and out in time with the music. A layer in mode 0 (Off) or
with opacity 0 costs nothing and contributes nothing.

This generalizes the old foreground/background idea: instead of two fixed layers, you get
sixteen, each a full pattern of its own.

### Color as spatial waveforms

Each layer describes its **Hue**, **Saturation**, and **Value** (brightness) as a waveform
laid out *spatially* across the LED strip. Each of the three color components has:

- a **waveshape** — Sawtooth, Triangle, Square, or Sine (set via CC 3-5),
- an **amplitude** — how far the value swings,
- an **offset** — the base value (a flat color is just amplitude 0 + offset),
- a **wavelength** — how many LEDs one cycle spans,
- a **phase shift** — where along the strip the wave starts.

A solid red strip, a full rainbow, and a saturation ripple are all just different settings of
the same three waveforms.

### Temporal modulation

Here is where it gets fun: every one of those spatial parameters (and several others, like
opacity and motion position) is driven by a **TemporalConfig** — a small oscillator that
makes the parameter move over time, synchronized to the DAW's tempo:

| Field | Range | Meaning |
|---|---|---|
| profile | 0-3 | Sawtooth / Triangle / Square / Sine |
| amplitude | 0-127 | How much the parameter oscillates (0 = static) |
| offset | 0-127 | The base value when amplitude is 0 |
| phaseShift | 0-16383 | Frame offset for phase alignment between parameters |
| period | 1-127 | Beats per cycle (tempo-synced) |
| direction | 0/1 | Reverse / forward |

The TemporalConfig is always active — there is no enable flag. With amplitude 0 it simply
holds the offset as a static value. Animate the hue offset and the colors cycle; animate the
phase shift and the wave scrolls down the strip; animate the wavelength and the pattern
breathes. The engine receives the DAW tempo over SysEx so everything stays locked to the beat.

### Modes

The mode (CC 1) selects the procedural motion applied on top of the layer's color waveforms:

| # | Mode | Description |
|---|---|---|
| 0 | Off | Layer produces no output |
| 1 | Solid | Pure waveform evaluation across the whole strip — no procedural motion |
| 2 | Moving Dots | One or more line segments, positioned by the motion parameters |
| 3 | Comets | Like Moving Dots, but each segment fades from tail to head |
| 4 | Flash | A random single LED lights each frame (stadium camera flashes) |
| 5 | Gravity Comet | Each Note On launches a comet up the strip under gravity physics; it decelerates, falls back, and extinguishes. Polyphonic (up to 16 comets per layer) |

In Gravity Comet mode the note number picks the target LED — the comet is launched with
exactly the velocity needed to reach it, just like tossing a ball to a chosen height.

## MIDI Protocol

MIDI values are 0-127 since the MIDI standard only allows 7-bit data bytes. Larger values
(LED count, tempo, phase shift) are split across two 7-bit bytes.

### Control Change (channel = layer 1-16)

| CC # | Parameter | Range | Description |
|---|---|---|---|
| 1 | Mode | 0-5 | Layer mode (see table above) |
| 2 | Opacity | 0-127 | Layer blend weight base (0 = transparent, 127 = opaque) |
| 3 | Hue Waveshape | 0-3 | Spatial waveshape for hue |
| 4 | Sat Waveshape | 0-3 | Spatial waveshape for saturation |
| 5 | Val Waveshape | 0-3 | Spatial waveshape for brightness |

### System Exclusive (manufacturer ID `0x7D`)

Everything else — all the spatial wave parameters and their temporal modulation — is set via
SysEx, because a single knob can't carry a whole oscillator config:

| Type | Message | Description |
|---|---|---|
| `0x01` | `F0 7D 01 bpmHi bpmLo F7` | Tempo update; BPM × 10 = (bpmHi << 7) \| bpmLo |
| `0x03` | `F0 7D 03 F7` | Reset the frame counter (re-aligns all temporal phases to the start of playback) |
| `0x06` | `F0 7D 06 F7` | Save the full engine state to the board's persistent memory |
| `0x07` | `F0 7D 07 layer panel param profile amp offset phaseL phaseH period dir F7` | Set a complete TemporalConfig atomically |
| `0x08` | `F0 7D 08 layer panel param field value F7` | Set a single TemporalConfig field |
| `0x09` | `F0 7D 09 countHi countLo F7` | Set active LED count = (countHi << 7) \| countLo, clamped to [1, 1000] |
| `0x0A` | `F0 7D 0A seq total payload… F7` | Full engine state sync, sent as 7-bit-encoded fragments (used by the Light Studio plugin; ESP32 firmware) |
| `0x0C` | `F0 7D 0C layer mode hueWave satWave valWave <16 slots x 7 bytes: profile amp offset phaseL phaseH period dir> F7` | One layer's mode, waveshapes, and all 16 TemporalConfig slots in a single message. Slot order: hue amp/off/wl/phase, sat amp/off/wl/phase, val amp/off/wl/phase, lines, motion offset, motion length, opacity. Used for bulk syncs (preset load, lights re-enable) to cut a 16-layer push from up to ~320 packets down to 16 — important for rtpMIDI/UDP, where a lost SysEx packet is never retransmitted |

Addressing for `0x07`/`0x08`:

- **layer**: 0-15
- **panel**: 0 = Hue, 1 = Sat, 2 = Val, 3 = Lines, 4 = Motion Offset, 5 = Motion Length, 6 = Opacity
- **param** (color panels 0-2 only): 0 = Amplitude, 1 = Offset, 2 = Wavelength, 3 = Phase Shift (ignored for panels 3-6)
- **field** (`0x08` only): 0 = profile, 1 = amplitude, 2 = offset, 3 = phase low 7 bits, 4 = phase high 7 bits, 5 = period, 6 = direction

Lines, Motion Offset, and Motion Length feed the Moving Dots and Comets modes: number of
parallel segments, start LED, and segment length — each animatable like everything else.

## Controlling from FL Studio:

1. Add a **MIDI Out** channel and set its port to match the device port configured in MIDI
   settings (press F10 to check). Set the channel's **MIDI channel** to the layer you want to
   control (1-16).

2. Map knobs to CC 1-5 (right-click a knob → *Configure* → set the *Controller* number) for
   mode, opacity, and the three waveshapes. Right-click a knob and select *Create automation
   clip* to automate it over time.

3. The wave parameters live behind SysEx, which MIDI Out knobs can't send — use the **Light
   Studio** VST3 plugin (built on the same engine via `Release/LightEngine.dll`) to edit
   them with a full GUI and keep the hardware in sync. The plugin also sends tempo updates
   (`0x01`) and frame-counter resets (`0x03`) so the lights stay locked to the playlist.

4. Note On events matter too: in Gravity Comet mode, every note on a layer's channel launches
   a comet whose target LED is the note number.

## Extending the Light System

All rendering logic lives in the shared `LightEngine` library (`libraries/LightEngine/src/`). Changes there apply to both the Teensy and ESP32 firmware as well as the Windows DLL used by the Light Studio VST3 plugin.

### Adding a New Parameter

1. Add the parameter to the `Layer` struct in `LightEngine.h` — usually as a `TemporalConfig` so it is animatable for free.
2. Wire it up in `LightEngine.cpp`: a new CC number in `handleControlChange()` for simple 0-127 values, or a new `panelId` in `resolveTemporalConfig()` for SysEx-addressable TemporalConfigs.
3. Use the parameter in one or more mode render functions (evaluate it once per frame in `applyTimeModulationForLayer()`).
4. Update the serialization in `serializeState()`/`deserializeState()` and bump `STATE_SIZE`, and add the parameter to `PARAMETER_TABLE` in `LightEngine.cpp` so the plugin can display it.

### Adding a New Mode

1. Add an entry to the `LayerMode` enum in `LightEngine.h`.
2. Implement a `renderYourMode()` function in `LightEngine.cpp` and add its `case` to the switch in `renderLayer()`.
3. Add the mode name to `MODE_TABLE` in `LightEngine.cpp`. The mode count is derived from that table (`getModeCount()`), and `handleControlChange()` / `deserializeState()` clamp against it automatically — no magic number to bump.
4. Rebuild and re-flash the firmware (`lights_teensy.ino` or `lights_esp32.ino`), and rebuild the DLL (`LightEngineDLL/`) if you use the plugin.
