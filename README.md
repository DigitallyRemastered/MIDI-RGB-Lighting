# MIDI-RGB-Lighting
Control individually addressable LEDs using the FastLED library and USB MIDI using the Teensy 3.6

This takes midi control bytes and notes and processes them to control RGB lighting using the FastLED library

Requirements: Requires a Teensy device. I am using a Teensy 3.6. I programmed it via Teensyduino Configuring as a USBMIDI device:



Quick Start: Enter the total number of RGB LEDs under the NUM_LEDs definition and enter the pin from which
data will be sent to the lights.

Concepts: Void loop will write messages to the lights at a frequency of 1/33,333e-6s = ~30Hz
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
ffMode      |       6        | foreground layer mode (0-8)
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
