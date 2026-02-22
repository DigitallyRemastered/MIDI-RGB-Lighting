/* lights
  This takes midi control bytes and notes and processes them to control RGB lighting using the FastLED library

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
  and independently. It's possible to do more patterns than this, but having a foreground and background
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
*/

#include <FastLED.h>
#define NUM_LEDS 108
#define DATA_PIN 0

CRGB leds[NUM_LEDS];

elapsedMicros t;

byte currentNote[] = {LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
int ffBright = 200;
int bgBright = 0;
int ffHue = 0;
int bgHue = 0;
int ffSat = 200;
int bgSat = 200;
int ffLedStart = 0;
int bgLedStart = 0;
int ffLedLength = 0;
int bgLedLength = 0;
int pan = 64;
byte ffMode = 0;
byte bgMode = 0;
byte lines = 0;
int lineOffset;
byte cAmp = 0;

//Constants

const byte  RAINBOW_INC = 255 / NUM_LEDS; // for rainbow wheel

//Represents a sin wave for the color sinusoid. 100sin(theta)+0
const int COLOR_PHASE[] = {10, 20,  29,  38, 47,  56,  63,  71,  77,  83,  88,  92,  96,  98,  100,  100,
                      100,  98,  96,  92,  88,  83,  77,  71,  63,  56,  47,  38,  29,  20,  10,  0,
                      -10, -20, -29, -38, -47, -56, -63, -71, -77, -83, -88, -92, -96, -98, -100, -100,
                      -100, -98, -96, -92, -88, -83, -77, -71, -63, -56, -47, -38, -29, -20, -10, 0
                     };

//Using a LUT of LED positions for the stadium cameras mode
byte RANDOM_LEDS[] = {18, 43, 26, 27, 32, 19, 13, 82, 0, 24, 1, 19, 80, 80, 76, 53, 82, 64, 92, 90, 22, 84, 80, 67, 61, 74, 10, 36, 38,
                   35, 7, 92, 31, 91, 61, 55, 23, 45, 66, 58, 92, 75, 67, 63, 8, 67, 71, 17, 66, 64, 17, 76, 37, 75, 62, 73, 66, 6, 52,
                   0, 95, 29, 43, 3, 53, 18, 58, 52, 18, 8, 13, 43, 20, 69, 68, 89, 25, 75, 28, 45, 49, 69, 60, 95, 26, 30, 95, 58, 8,
                   76, 29, 47, 39, 6, 58, 53
                  };

//Using a 2D array LUT of LED positions that match up to the width of floppy drives with corresponding MIDI channel
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
    case 0:
      for (byte driveLED = 0; driveLED < 6; driveLED++) {
        leds[CHANNEL_TO_LED[channel][driveLED]] = CHSV(ffHue, ffSat, ffBright);
      }
      break;
    case 5:
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
    // 0: notes to drives
    // 1: rainbow wheel
    // 2: moving dots
    // 3: comets
    // 4: back and forth
    // 5: Move startLED with each note on event
    // 6: Color Sinusoid
    // 7: Flash Lights
    // 8: Ocean waves
    case 7: lines = value; break;
    case 8: cAmp = value; break;
    case 9: bgMode = value; break;
    case 10: pan = value; break;
    case 11: bgHue = 2 * value; break;
    case 12: bgSat = 2 * value; break;
    case 13: bgBright = 2 * value; break;
    case 14: bgLedStart = value; break;
    case 15: bgLedLength = value; break;
    //0: Flat Color background
    //1: rainbow wheel background
    //2: Color Sinusoid
    default:  break;
  }

  byte amp = 0;
  int tMiddle = 0;

  //depending on the value of the mode knob
  switch (ffMode) {
    case 1: //rainbow wheel
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV((ffHue + led * RAINBOW_INC), ffSat, ffBright);
      }
      break;
    //---------------------------------------------------------------
    case 2://moving dots
      lineOffset = NUM_LEDS / lines;
      updateBG();
      for (byte line = 0; line < lines; line++) {
        for (byte led = ffLedStart; led < ffLedStart + ffLedLength; led++) {
          leds[(led + line * lineOffset) % NUM_LEDS] = CHSV( ffHue, ffSat, ffBright);
        }
      }
      break;
    //---------------------------------------------------------------
    case 3://comet
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
    case 4://back and forth
      updateBG();
      for (int block = 0; block < NUM_LEDS; block += 2 * ffLedLength) {
        for (int led = 0; led < ffLedLength; led++) {
          leds[(ffLedStart * ffLedLength + block + led) % NUM_LEDS] = CHSV(ffHue, ffSat, ffBright);
        }
      }
      break;
    //---------------------------------------------------------------
    case 6:// color sinusoid
      for (int led = 0; led < NUM_LEDS; led += 1) {
        byte cphaseIdx = ((led + ffLedStart) * 64 / ffLedLength) % 64;
        byte h = (256 + ffHue + cAmp * COLOR_PHASE[cphaseIdx] / 100) % 256;
        leds[led] = CHSV(h, ffSat, ffBright);
      }
      break;
    //---------------------------------------------------------------
    case 7:// Flash lights
      updateBG();
      //leds[RANDOM_LEDS[ffLedStart]] = CHSV(ffHue, ffSat, ffBright);
      leds[random(NUM_LEDS)] = CHSV(ffHue, ffSat, ffBright);
      break;
    //---------------------------------------------------------------
    case 8:// Ocean Waves
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
    case 9:// Opposing Waves
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
    case 0:// Plain BG color
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV(bgHue, bgSat, bgBright);
      }
      break;
    case 1:// Rainbow Circle
      for (int led = 0; led < NUM_LEDS; led += 1) {
        leds[led] = CHSV((bgHue + led * RAINBOW_INC), bgSat, bgBright);
      }
      break;
    case 2:// Color Sinusoid
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
