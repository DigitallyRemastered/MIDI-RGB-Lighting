# MIDI RGB Lighting - Arduino Firmware

This folder contains two versions of the Arduino firmware that share the same rendering engine:

## Folder Structure

```
Source/
├── LightEngine/           # Shared rendering engine (DO NOT DUPLICATE)
│   ├── LightEngine.h
│   └── LightEngine.cpp
│
├── lights_teensy/         # Teensy USB-MIDI version
│   └── lights_teensy.ino
│
└── lights_esp32/          # ESP32 WiFi-MIDI version
    └── lights_esp32.ino
```

## Versions

### Teensy USB-MIDI (`lights_teensy/`)
- **Hardware**: Teensy board (3.2, 4.0, etc.)
- **Connection**: USB-MIDI (plug and play)
- **LEDs**: GPIO pin 0
- **Best for**: Wired USB connection, lowest latency

### ESP32 WiFi-MIDI (`lights_esp32/`)
- **Hardware**: Seeed XIAO ESP32S3 (or any ESP32 board)
- **Connection**: WiFi (RTP-MIDI/AppleMIDI)
- **LEDs**: GPIO pin 2
- **Best for**: Wireless connection, battery powered setups

## Setup Instructions

### Teensy Version

1. Open Arduino IDE
2. **File → Open** → `lights_teensy/lights_teensy.ino`
3. **Tools → Board** → Select your Teensy board
4. **Tools → USB Type** → Select "MIDI"
5. Update `NUM_LEDS` if needed (default: 108)
6. Upload

### ESP32 Version

1. Install ESP32 board support:
   - **File → Preferences** → Add board manager URL:
     ```
     https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
     ```
   - **Tools → Board → Boards Manager** → Install "esp32"

2. Install required libraries:
   - **Tools → Manage Libraries** → Install:
     - `FastLED`
     - `AppleMIDI` (by lathoub)

3. Open sketch:
   - **File → Open** → `lights_esp32/lights_esp32.ino`

4. Configure:
   - Update WiFi credentials in the sketch:
     ```cpp
     const char* ssid = "YourWiFiName";
     const char* password = "YourWiFiPassword";
     ```
   - Update `NUM_LEDS` if needed (default: 108)

5. Select board:
   - **Tools → Board** → "XIAO_ESP32S3" (or your ESP32 board)
   - **Tools → Port** → Select COM port

6. Upload

7. Connect via rtpMIDI:
   - **Windows**: Install rtpMIDI from https://www.tobias-erichsen.de/software/rtpmidi.html
   - **Mac**: Use built-in Audio MIDI Setup
   - Connect to the ESP32's IP address (shown in Serial Monitor)

## Hardware Wiring

Both versions use the same wiring:

```
Arduino Board          LED Strip
─────────────          ─────────
DATA_PIN    ────────── DIN (Data In)
GND         ────────── GND
5V          ────────── 5V (or external power for >30 LEDs)
```

**Notes:**
- For Teensy: DATA_PIN = GPIO 0
- For ESP32: DATA_PIN = GPIO 2
- Power LEDs externally if using more than 30 LEDs
- Add 330Ω resistor between DATA_PIN and LED strip DIN (optional but recommended)
- Add 1000µF capacitor across LED power supply (optional but recommended)

## LightEngine

Both versions share the **exact same** `LightEngine` code located in `Source/LightEngine/`.

### Features:
- 10 foreground rendering modes (Notes to Drives, Rainbow Wheel, Moving Dots, etc.)
- 3 background modes (Flat, Rainbow, Sinusoid)
- 15 MIDI CC parameters for real-time control
- 30Hz rendering at constant frame rate
- HSV color space for smooth color transitions

### Do NOT duplicate LightEngine!
If you need to modify the rendering logic, edit `Source/LightEngine/LightEngine.cpp` only once. Both Arduino sketches will automatically use the updated code.

## FL Studio Setup

1. **Teensy USB-MIDI**:
   - Plug in Teensy via USB
   - **Options → MIDI Settings**
   - Enable "Teensy MIDI" in Input/Output lists
   - Done!

2. **ESP32 WiFi-MIDI**:
   - Connect ESP32 to power and WiFi
   - Open rtpMIDI (Windows) or Audio MIDI Setup (Mac)
   - Connect to ESP32's IP address
   - **Options → MIDI Settings** in FL Studio
   - Enable rtpMIDI port in Input/Output lists
   - Done!

## MIDI CC Mapping

| CC# | Parameter | Range | Description |
|-----|-----------|-------|-------------|
| 1 | Hue | 0-127 | Foreground color |
| 2 | Saturation | 0-127 | Foreground saturation |
| 3 | Brightness | 0-127 | Foreground brightness |
| 4 | Start | 0-127 | Start LED position |
| 5 | Length | 0-127 | LED strip length |
| 6 | Mode | 0-9 | Foreground rendering mode |
| 7 | Lines | 0-127 | Number of lines/segments |
| 8 | Color Amp | 0-127 | Color wave amplitude |
| 9 | BG Mode | 0-2 | Background mode |
| 10 | Pan | 0-127 | Wave pan position |
| 11 | BG Hue | 0-127 | Background color |
| 12 | BG Sat | 0-127 | Background saturation |
| 13 | BG Bright | 0-127 | Background brightness |
| 14 | BG Start | 0-127 | Background start position |
| 15 | BG Length | 0-127 | Background length |

## Troubleshooting

### Teensy Version
- **No MIDI device**: Select **Tools → USB Type → MIDI**
- **LEDs don't light**: Check wiring and DATA_PIN

### ESP32 Version
- **WiFi won't connect**: Check SSID/password, ensure 2.4GHz network
- **Upload fails**: Hold BOOT button while uploading
- **rtpMIDI can't find device**: Check Serial Monitor for IP address
- **FastLED errors**: Update to FastLED 3.5+

## License

MIT License - See main repository for details
