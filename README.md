# MIDI-RGB-Lighting
Control individually addressable LEDs using the FastLED library and USB MIDI using the Teensy 3.6

See a demo and video of construction here: https://www.youtube.com/watch?v=1hBeVJ6QlaU

## Requirements: 

Requires a Teensy device. I am using a Teensy 3.6. I programmed it via Teensyduino configuring as a USBMIDI device:
![image](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/Teensy%20USB%20MIDI.png)

The PSU must be rated for power required by your LED strips. Each LED with each color at full brightness (white) can be estimated to draw 50mA. 108 
LEDs as I have programmed has a current draw of 108 lights * 0.05 Amps/light = 5.4A. 5V * 5.4A = 27W minimum for the power supply you shoudl buy. You should get a PSU that can output a good margin more power than is required (although it will only supply as much power as is demanded from the lights).

You will require an Digital Audio Workstation to send MIDI control messages. To use the included template/example, you will need FL Studio (I'm using version 20)

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

## Controlling from FL Studio:
![Controlling from FL Studio](https://github.com/DigitallyRemastered/MIDI-RGB-Lighting/blob/main/images/FL%20GUI%20help.png)

1. Click on the Lights MIDI Channel to show the control knobs. You can right click the control knobs and click *Configure* to show that e.g. *sat* has *Controller* set to 2, so that the value of *sat* in Fl Studio is assigned to *ffSat* on the Teensy.

2. Ensure that the port is the same as what is configured for the MIDI device (Check MIDI settings by pressing F10)

3. You can access Foreground and Background controls on separate pages accessible by this dropdown

4. You can right click a control knob and select *create automation clip* to be able to automate that property temporally.

## Extending the Light System

The code uses structured comments to define MIDI parameters and modes. The `generate_csv.py` script automatically parses these comments to create `Template.csv` for FL Studio.

### Adding a New Parameter

1. Add a metadata block in `lights.ino` before the variable declaration:
```cpp
/**
 * @param MyParameter
 * @cc 16
 * @layer Foreground
 * @tooltip Description of what this parameter does
 */
int myVar = 0;
```

2. Add a case to the `OnControlChange` switch statement:
```cpp
case 16: myVar = value; break;
```

3. Use the variable in one or more mode implementations

4. Regenerate the CSV: `python generate_csv.py`

### Adding a New Mode

1. Add the mode name to the appropriate mode selector's `@modes` list:
```cpp
/**
 * @param Foreground
 * @cc 6
 * @modes 0:Notes to Drives,...,10:MyNewMode
 */
```

2. Implement the mode logic in the appropriate switch statement with a mode annotation:
```cpp
case 10: // @mode MyNewMode @uses ffHue,ffSat,ffBright,myVar
  // Your mode logic here
  break;
```

3. Regenerate the CSV: `python generate_csv.py`

### Structured Comment Tags

- `@param DisplayName` - Name shown in FL Studio
- `@cc N` - MIDI Control Change number (1-15, or higher if MIDI allows)
- `@layer Foreground|Background|Shared` - Which layer uses this parameter (omit for mode selectors)
- `@tooltip Description` - Help text for the parameter
- `@modes N:ModeName,M:OtherMode` - For mode selectors only, lists available modes
- `@mode ModeName @uses var1,var2` - Annotates a case statement with mode info

### Validation

Run validation to check for errors before regenerating:
```bash
python generate_csv.py --validate
```

This will:
- Check all CCs 1-15 have definitions
- Verify no duplicate CC numbers
- Ensure all modes in `@modes` are implemented
- Compare generated CSV with existing to detect changes

