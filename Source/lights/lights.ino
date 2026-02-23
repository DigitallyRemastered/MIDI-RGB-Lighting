/* lights
  This takes midi control bytes and notes and processes them to control RGB lighting using the FastLED library

  Quick Start: Enter the total number of RGB LEDs under the NUM_LEDs definition and enter the pin from which
  data will be sent to the lights.

  Concepts: Void loop will write messages to the lights at a frequency of 1/33,333e-6s = ~30Hz
  Between writes to the data line, the HSV of each light is determined by midi event handlers
  OnNoteOn, OnNoteOff, and (most usually) OnControlChange. On Control change is used to directly
  set the value of several global variables. The global parameters are used differently or ignored
  depending on the mode - the parameter with the most effect on how LEDs are displayed.
  Several modes have been programmed - Their names are given in the mode annotations, but
  the best way to figure out what they do is to try them out!

  In order to make it more visually interesting, I've incorporated the concept of a foreground and
  a background. We'll call them layers. If the foreground doesn't make use of all of the lights,
  then the background will cover the rest. This way, you can control two sets of patterns simultaneously
  and independently. It's possible to do more patterns than this, but having a foreground and background
  seemed intuitive and  manageable.

  Structured Comment Schema:
  This file uses structured comments to define MIDI parameters and modes. A Python script (generate_csv.py)
  parses these comments to automatically generate the Template.csv file for FL Studio.

  Parameter Metadata Format:
  \**
   * @param DisplayName
   * @cc N
   * @layer Foreground|Background|Shared
   * @tooltip Description text for the parameter
   *\
  type varName = defaultValue;

  Mode Selector Format (for ffMode and bgMode):
  \**
   * @param DisplayName
   * @cc N
   * @modes 0:ModeName0,1:ModeName1,2:ModeName2,...
   *\
  byte varName = 0;

  Mode Annotation Format (in switch case statements):
  case N: // @mode ModeName @uses var1,var2,var3

  To regenerate Template.csv after adding parameters or modes, run: python generate_csv.py
*/

#include <FastLED.h>
#define NUM_LEDS 108
#define DATA_PIN 0

CRGB leds[NUM_LEDS];

elapsedMicros t;

// ============================================================================
// MIDI-Controlled Parameters (CC 1-15)
// These variables are controlled via MIDI Control Change messages
// ============================================================================

/**
 * @param Hue
 * @cc 1
 * @layer Foreground
 * @tooltip Sets color [roygbivmr]. Cyclic (min val = max val)
 */
int ffHue = 0;

/**
 * @param Saturation
 * @cc 2
 * @layer Foreground
 * @tooltip Sets saturation [white, chosen hue]
 */
int ffSat = 200;

/**
 * @param Brightness
 * @cc 3
 * @layer Foreground
 * @tooltip Sets intensity [LED off, LED on]
 */
int ffBright = 200;

/**
 * @param Start
 * @cc 4
 * @layer Foreground
 * @tooltip start position of line
 */
int ffLedStart = 0;

/**
 * @param Length
 * @cc 5
 * @layer Foreground
 * @tooltip length of line
 */
int ffLedLength = 0;

/**
 * @param Foreground
 * @cc 6
 * @modes 0:Notes to Drives,1:Rainbow Wheel,2:Moving Dots,3:Comets,4:Back and Forth,5:Move startLED with each note on event,6:Color Sinusoid,7:Flash Lights,8:Ocean Waves,9:Opposing Waves
 */
byte ffMode = 0;

/**
 * @param Number of Lines
 * @cc 7
 * @layer Foreground
 * @tooltip Number of lines
 */
byte lines = 0;

/**
 * @param Color Amplitude
 * @cc 8
 * @layer Shared
 * @tooltip color Amplitude for color sinusoid
 */
byte cAmp = 0;

/**
 * @param Background
 * @cc 9
 * @modes 0:Flat Color background,1:rainbow wheel background,2:Color Sinusoid
 */
byte bgMode = 0;

/**
 * @param Pan
 * @cc 10
 * @layer Foreground
 * @tooltip Pan position for wave effects
 */
int pan = 64;

/**
 * @param Hue
 * @cc 11
 * @layer Background
 * @tooltip Sets color [roygbivmr]. Cyclic (min val = max val)
 */
int bgHue = 0;

/**
 * @param Saturation
 * @cc 12
 * @layer Background
 * @tooltip Sets saturation [white, chosen hue]
 */
int bgSat = 200;

/**
 * @param Brightness
 * @cc 13
 * @layer Background
 * @tooltip Sets intensity [LED off, LED on]
 */
int bgBright = 0;

/**
 * @param Start
 * @cc 14
 * @layer Background
 * @tooltip start position of line
 */
int bgLedStart = 0;

/**
 * @param Length
 * @cc 15
 * @layer Background
 * @tooltip length of line
 */
int bgLedLength = 0;

// ============================================================================
// Helper Variables (not MIDI-controlled)
// ============================================================================

byte currentNote[] = {LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int lineOffset;

// ============================================================================
// Constants and Lookup Tables
// ============================================================================

// Hue increment per LED for rainbow wheel effect
const byte RAINBOW_INC = 255 / NUM_LEDS;

// Lookup table: 64-element discretized sine wave [-100,100] for color sinusoid mode
// Represents 100*sin(theta) where theta ranges from 0 to 2*pi
const int COLOR_PHASE[] = {10, 20,  29,  38, 47,  56,  63,  71,  77,  83,  88,  92,  96,  98,  100,  100,
                      100,  98,  96,  92,  88,  83,  77,  71,  63,  56,  47,  38,  29,  20,  10,  0,
                      -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100, -100,
                      -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10, 0
                     };

// Random LED position lookup table for Flash Lights mode (legacy, currently unused - random() used instead)
byte RANDOM_LEDS[] = {18, 43, 26, 27, 32, 19, 13, 82, 0, 24, 1, 19, 80, 80, 76, 53, 82, 64, 92, 90, 22, 84, 80, 67, 61, 74, 10, 36, 38,
                   35, 7, 92, 31, 91, 61, 55, 23, 45, 66, 58, 92, 75, 67, 63, 8, 67, 71, 17, 66, 64, 17, 76, 37, 75, 62, 73, 66, 6, 52,
                   0, 95, 29, 43, 3, 53, 18, 58, 52, 18, 8, 13, 43, 20, 69, 68, 89, 25, 75, 28, 45, 49, 69, 60, 95, 26, 30, 95, 58, 8,
                   76, 29, 47, 39, 6, 58, 53
                  };

// Maps MIDI channels (1-16) to LED indices for Notes to Drives mode
// Each floppy drive has 6 LEDs controlled by one MIDI channel
// CHANNEL_TO_LED[MIDI Channel][led along width of drive]
byte CHANNEL_TO_LED[][6] =
{ {0, 0, 0, 0, 0, 0},
  {19, 20, 21, 22, 23, 24},
  {29, 30, 31, 32, 33, 34},
  {13, 14, 15, 16, 17, 18},
  {35, 36, 37, 38, 39, 40},
  {7 , 8 , 9 , 10, 11, 12},
  {41, 42, 43, 44, 45, 46},
  {1 , 2 , 3 , 4 , 5, 6},
  {47, 48, 49, 50, 51, 52},
  {73, 74, 75, 76, 77, 78},
  {83, 84, 85, 86, 87, 88},
  {67, 68, 69, 70, 71, 72},
  {89, 90, 91, 92, 93, 94},
  {61, 62, 63, 64, 65, 66},
  {95, 96, 97, 98, 99, 100},
  {55, 56, 57, 58, 59, 60},
  {101, 102, 103, 104, 105, 106},

};

// Mirrors top strip (LEDs 0-23) and bottom strip (LEDs 72-95) for Ocean Waves modes
// Used to create symmetric wave effects on top and bottom LED strips
byte TOP_BOTTOM_MIRROR_MAP[] = {23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1, 0,
                  95, 94, 93, 92, 91, 90, 89, 88, 87, 86, 85, 84, 83, 82, 81, 80, 79, 78, 77, 76, 75, 74, 73, 72
                 };

void setup() {
  FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

  usbMIDI.setHandleNoteOff(OnNoteOff);
  usbMIDI.setHandleNoteOn(OnNoteOn);
  usbMIDI.setHandleControlChange(OnControlChange);

  Serial.begin(250000);

  for (int led = 0; led < 108; led++) {
    if (led % 2 == 0) leds[led] = CHSV(80, 200, ffBright);
    else                  leds[led] = CHSV(100, 200, ffBright);
  }
}

void loop() {
  usbMIDI.read();
  if (t > 33333) {
    FastLED.show();
    t = 0;
  }
}

void OnNoteOn(byte channel, byte note, byte velocity) {
  currentNote[channel] = note;

  switch (ffMode) {
    case 0: // @mode Notes to Drives @uses ffHue,ffSat,ffBright
      for (byte driveLED = 0; driveLED < 6; driveLED++) {
        leds[CHANNEL_TO_LED[channel][driveLED]] = CHSV(ffHue, ffSat, ffBright);
      }
      break;
    case 5: // @mode Move startLED with each note on event @uses ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines
      ffLedStart += 1;
      lineOffset = NUM_LEDS / lines;
      //start from a clean slate
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV(ffHue, ffSat, 0);
      }
      for (byte line = 0; line < lines; line++) {
        for (byte led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
          leds[(led + line * lineOffset) % NUM_LEDS] = CHSV((ffHue + led * RAINBOW_INC) % 255, ffSat, ffBright);
        }
      }
      break;
    default: break;
  }
}

void OnNoteOff(byte channel, byte note, byte velocity) {
  if (ffMode == 0) {
    if (currentNote[channel] == note) {
      currentNote[channel] = 0;
      for (byte driveLED = 0; driveLED < 6; driveLED++) {
        if (driveLED % 2 == 0) leds[CHANNEL_TO_LED[channel][driveLED]] = CHSV(80, 200, ffBright);
        else                  leds[CHANNEL_TO_LED[channel][driveLED]] = CHSV(100, 200, ffBright);
      }
    }
  }
}

void OnControlChange(byte channel, byte control, byte value) {
  //  Serial.print("control: "); Serial.print(control); Serial.print("  ,  value: "); Serial.print(value); Serial.print("  ,  Time: "); Serial.println(t);
  //Which knobs (configure DAW to send these (below in decimal) control bytes)
  Serial.print(control); Serial.print("      "); Serial.println(value);
  switch (control) {
    case 1: ffHue = 2 * value; break;
    case 2: ffSat = 2 * value; break;
    case 3: ffBright = 2 * value; break;
    case 4: ffLedStart = value; break;
    case 5: ffLedLength = value; break;
    case 6: ffMode = value; break;
    case 7: lines = value; break;
    case 8: cAmp = value; break;
    case 9: bgMode = value; break;
    case 10: pan = value; break;
    case 11: bgHue = 2 * value; break;
    case 12: bgSat = 2 * value; break;
    case 13: bgBright = 2 * value; break;
    case 14: bgLedStart = value; break;
    case 15: bgLedLength = value; break;
    default:  break;
  }

  byte amp = 0;
  int tMiddle = 0;

  //depending on the value of the mode knob
  switch (ffMode) {
    case 1: // @mode Rainbow Wheel @uses ffHue,ffSat,ffBright
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV((ffHue + led * RAINBOW_INC), ffSat, ffBright);
      }
      break;
    //---------------------------------------------------------------
    case 2: // @mode Moving Dots @uses ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines
      lineOffset = NUM_LEDS / lines;
      updateBG();
      for (byte line = 0; line < lines; line++) {
        for (byte led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
          leds[(led + line * lineOffset) % NUM_LEDS] = CHSV( ffHue, ffSat, ffBright);
        }
      }
      break;
    //---------------------------------------------------------------
    case 3: // @mode Comets @uses ffHue,ffSat,ffBright,ffLedStart,ffLedLength,lines
      lineOffset = NUM_LEDS / lines;
      updateBG();
      for (byte line = 0; line < lines; line++) {
        byte count = ffLedLength - 1;
        for (byte led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
          leds[(led + line * lineOffset) % NUM_LEDS] = CHSV( ffHue, ffSat, ffBright * (ffLedLength - count) / ffLedLength);
          count -= 1;
        }
      }
      break;
    //---------------------------------------------------------------
    case 4: // @mode Back and Forth @uses ffHue,ffSat,ffBright,ffLedStart,ffLedLength
      updateBG();
      for (int block = 0; block < NUM_LEDS; block += 2 * ffLedLength) {
        for (int led = 0; led < ffLedLength; led++) {
          leds[(ffLedStart * ffLedLength + block + led) % NUM_LEDS] = CHSV(ffHue, ffSat, ffBright);
        }
      }
      break;
    //---------------------------------------------------------------
    case 6: // @mode Color Sinusoid @uses ffHue,ffSat,ffBright,ffLedStart,ffLedLength,cAmp
      for (int led = 0; led < NUM_LEDS; led += 1) {
        byte cphaseIdx = ((led + ffLedStart) * 64 / ffLedLength) % 64;
        byte h = (256 + ffHue + cAmp * COLOR_PHASE[cphaseIdx] / 100) % 256;
        leds[led] = CHSV(h, ffSat, ffBright);
      }
      break;
    //---------------------------------------------------------------
    case 7: // @mode Flash Lights @uses ffHue,ffSat,ffBright
      updateBG();
      //leds[RANDOM_LEDS[ffLedStart]] = CHSV(ffHue, ffSat, ffBright);
      leds[random(NUM_LEDS)] = CHSV(ffHue, ffSat, ffBright);
      break;
    //---------------------------------------------------------------
    case 8: // @mode Ocean Waves @uses ffHue,ffSat,ffBright,ffLedLength,pan
      updateBG();

      tMiddle = (NUM_LEDS / 2 - 1) * pan / 127 + NUM_LEDS / 4; // if Pan is 1, tmWave is 0, the right side of
      //int bMiddle =  TOP_BOTTOM_MIRROR_MAP[tMiddle-24];
      amp = ffLedLength / 2;

      if (tMiddle - amp <= 24) {
        amp = tMiddle - 24;
      }
      else if (tMiddle + amp > 71) {
        amp = 71 - tMiddle;
      }

      for (byte p = 0; p < amp; p++) {
        leds[tMiddle + p] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[tMiddle - p] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[TOP_BOTTOM_MIRROR_MAP[tMiddle + p - 24]] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[TOP_BOTTOM_MIRROR_MAP[tMiddle - p - 24]] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
      }
      break;
    //---------------------------------------------------------------
    case 9: // @mode Opposing Waves @uses ffHue,ffSat,ffBright,ffLedLength,pan
      updateBG();

      tMiddle = (NUM_LEDS / 2 - 1) * pan / 127 + NUM_LEDS / 4; // if Pan is 1, tmWave is 0, the right side of
      //int bMiddle =  TOP_BOTTOM_MIRROR_MAP[tMiddle-24];
      amp = ffLedLength / 2;

      if (tMiddle - amp <= 24) {
        amp = tMiddle - 23;
      }
      else if (tMiddle + amp > 71) {
        amp = 71 - tMiddle;
      }

      for (byte p = 0; p <= amp; p++) {
        leds[tMiddle + p] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[tMiddle - p] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[TOP_BOTTOM_MIRROR_MAP[(NUM_LEDS - tMiddle) + p - 24]] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
        leds[TOP_BOTTOM_MIRROR_MAP[(NUM_LEDS - tMiddle) - p - 24]] = CHSV( ffHue, ffSat, ffBright * (amp - p) / amp);
      }
      break;
    //---------------------------------------------------------------
    default: break;
  }
}

void updateBG() {

  switch (bgMode) {
    case 0: // @mode Flat Color background @uses bgHue,bgSat,bgBright
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV(bgHue, bgSat, bgBright);
      }
      break;
    case 1: // @mode rainbow wheel background @uses bgHue,bgSat,bgBright
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV((bgHue + led * RAINBOW_INC), bgSat, bgBright);
      }
      break;
    case 2: // @mode Color Sinusoid @uses bgHue,bgSat,bgBright,bgLedStart,bgLedLength,cAmp
      for (int led = 0; led < NUM_LEDS; led += 1) {
        byte cphaseIdx = ((led + bgLedStart) * 64 / bgLedLength) % 64;
        byte h = (256 + bgHue + cAmp * COLOR_PHASE[cphaseIdx] / 100) % 256;
        leds[led] = CHSV(h, bgSat, bgBright);
      }
      break;
    default:
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV(bgHue, bgSat, bgBright);
      }
      break;
  }
}

void lightsOut() {
  //start from a clean slate
  for (int led = 0; led < NUM_LEDS; led += 1) {
    leds[led] = CHSV(ffHue, ffSat, 0);
  }
}
