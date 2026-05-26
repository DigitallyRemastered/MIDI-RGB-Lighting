# MIDI-RGB-Lighting
Control individually addressable LEDs using the FastLED library and MIDI. Supports **Teensy** (USB-MIDI) and **ESP32** (WiFi/BLE-MIDI).

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

A Digital Audio Workstation is required to send MIDI CC messages. The included template targets FL Studio.

## Quick Start: 

The Teensy will need to be connected as shown below:

![image](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/Circuit%20Schematic.png)

In the code:
Enter the total number of RGB LEDs under the NUM_LEDs definition and enter the pin from which
data will be sent to the lights under the DATA_PIN definition.

Upload the code to the board, leave the usb cable connected, and then turn on the 5V PSU.

Start FL Studio and press play!


## Concepts: 

Void loop will write messages to the lights at a frequency of 1/33,333e-6s = ~30Hz
Between writes to the data line, the HSV of each light is determined by midi event handlers
OnNoteOn, OnNoteOff, and (most usually) OnControlChange. On Control change is used to directly
set the value of several global variables as described at the bottom. The global parameters are used
differently or ignored depending on the mode - the parameter with the most effect on how LEDs are displayed.
Several modes have been programmed - Their names are given in the OnControlChange function, but
the best way to figure out what they do is to try them out!

In order to make it more visually interesting, I've incorporated the concept of a foreground and
a background. We'll call them layers. If the foreground doesn't make use of all of the lights,
then the background will cover the rest. This way, you can control two sets of patterns simultaneously
and independently. It's possible to do more patterns than this, but having an foreground an background
seemed intuitive and  manageable.

Global variables are bytes since MIDI standard only allows numbers between 0 and 127 to be transmitted.
some variables don't make use of all 127 values, i.e. ffMode has 9 values - 0,1,2,3,4,5,6,7,8
These variables generally control these aspects of the LEDs:

    Parameter   | MIDI Control # | Description
    ffHue       |       1        | foreground layer hue
    ffSat       |       2        | foreground layer saturation
    ffBright    |       3        | foreground layer brightness
    ffLedStart  |       4        | foreground layer start position of LED
    ffLedLength |       5        | foreground layer length of a line of LEDs
    ffMode      |       6        | foreground layer mode (0-9)

      // 0: notes2MIDIChannel
      // 1: rainbow wheel
      // 2: moving dots
      // 3: comets
      // 4: back and forth
      // 5: Move startLED with each note on event
      // 6: Color Sinusoid
      // 7: Stadium Camera flashes
      // 8: Ocean waves
      // 9: Opposed Ocean waves
  
    lines       |       7        | foreground layer number of LED lines
    cAmp        |       8        | foreground and background layer color Amplitude for use in color sinusoid mode (my favorite)
    bgMode      |       9        | background layer mode (0-2)
      //0: Flat Color background
      //1: rainbow wheel background
      //2: Color Sinusoid
    pan         |      10        | used in ffMode "Ocean Waves"
    bgHue       |      11        | background layer hue
    bgSat       |      12        | background layer sat
    bgBright    |      13        | background layer brightness
    bgLedStart  |      14        | background layer start position of LED
    bgLedLength |      15        | background layer length of line of LEDs

## Controlling from FL Studio:
![Controlling from FL Studio](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/FL%20GUI%20help.png)

1. Click on the Lights MIDI Channel to show the control knobs. You can right click the control knobs and click *Configure* to show that e.g. *sat* has *Controller* set to 2, so that the value of *sat* in Fl Studio is assigned to *ffSat* on the Teensy.

2. Ensure that the port is the same as what is configured for the MIDI device (Check MIDI settings by pressing F10)

3. You can access Foreground and Background controls on separate pages accessible by this dropdown

4. You can right click a control knob and select *create automation clip* to be able to automate that property temporally.

## Extending the Light System

All rendering logic lives in the shared `LightEngine` library (`libraries/LightEngine/src/`). Changes there apply to both the Teensy and ESP32 firmware as well as the Windows DLL used by the Light Studio VST3 plugin.

### Adding a New Parameter

1. Declare the parameter in `LightEngine.h` and add it to the `RenderContext` struct.
2. Handle the new CC number in `LightEngine.cpp` → `handleControlChange()`.
3. Use the parameter in one or more mode render functions.
4. Update the FL Studio MIDI Out channel to include the new CC (right-click a knob in the channel → **Create automation clip**, or wire it to a new controller).

### Adding a New Mode

1. Add an entry to the `LayerMode` enum in `LightEngine.h`.
2. Implement the render logic as a new `case` in the foreground or background switch in `LightEngine.cpp`.
3. Add the mode name to the metadata table in `LightEngine.cpp` (or `Metadata.cpp` if building the Windows DLL).
4. Rebuild and re-flash the firmware (`lights_teensy.ino` or `lights_esp32.ino`).

