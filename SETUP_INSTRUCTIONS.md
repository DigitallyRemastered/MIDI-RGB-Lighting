# LightEngine Library - First Time Setup

## Prerequisites

1. Install Arduino IDE 2.x from https://www.arduino.cc/en/software
2. Install required libraries via **Tools → Manage Libraries**:
   - `FastLED` (for all versions)
   - `AppleMIDI` by lathoub (for ESP32 only)

## Step 1: Link Library to Arduino IDE

The LightEngine is in `libraries/LightEngine/` but Arduino IDE needs to see it in its libraries folder. We use a symbolic link to keep it in Git while making it available to Arduino.

### Windows

1. **Right-click PowerShell** and select **"Run as Administrator"**
2. Navigate to this repo:
   ```powershell
   cd "D:\Code\Lights\MIDI-RGB-Lighting"
   ```
3. Run the setup script:
   ```powershell
   .\setup_library.ps1
   ```
4. **Restart Arduino IDE**

### Mac/Linux

```bash
ln -s "$(pwd)/libraries/LightEngine" ~/Documents/Arduino/libraries/LightEngine
```

## Step 2: Verify Installation

1. Open Arduino IDE
2. Go to **Sketch → Include Library**
3. You should see **LightEngine** in the list

## Step 3: Open Your Sketch

### For Teensy (USB-MIDI):
- Open `Source/lights_teensy/lights_teensy.ino`
- Select **Tools → Board → Teensy 3.6** (or your Teensy model)
- Select **Tools → USB Type → MIDI**
- Upload

### For ESP32 (WiFi-MIDI):
- First, install ESP32 board support:
  - **File → Preferences** → Add URL:
    ```
    https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
    ```
  - **Tools → Board → Boards Manager** → Install "esp32"
- Open `Source/lights_esp32/lights_esp32.ino`
- **Update WiFi credentials** in the sketch (lines 17-18)
- Select **Tools → Board → XIAO_ESP32S3** (or your ESP32 board)
- Upload

---

## ESP32 Out-of-the-Box Setup (WiFi-MIDI)

Follow these steps to get the ESP32 connected to Light Studio over the network.

### 1. Install ESP32 Board Support

In Arduino IDE:
1. **File → Preferences** → add this URL to "Additional boards manager URLs":
   ```
   https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
   ```
2. **Tools → Board → Boards Manager** → search "esp32" → Install **"esp32 by Espressif Systems"**

### 2. Install Required Libraries

**Tools → Manage Libraries**, install:
- `FastLED`
- `AppleMIDI` by lathoub
- `MIDI Library` by lathoub (dependency of AppleMIDI)

### 3. Configure the Sketch

Open `Source/lights_esp32/lights_esp32.ino` and update the WiFi credentials near the top:
```cpp
const char* ssid = "YourNetworkName";
const char* password = "YourPassword";
```

Also set `NUM_LEDS` to your strip length and `DATA_PIN` to the GPIO pin connected to the strip's data line.

> **BLE-MIDI alternative**: The ESP32 sketch also supports BLE-MIDI (Bluetooth Low Energy). If your DAW supports BLE-MIDI you can skip the WiFi/rtpMIDI steps entirely and connect wirelessly without a network.

### 4. Upload

1. Plug in the ESP32 via USB
2. **Tools → Board** → select your ESP32 board (e.g. **XIAO_ESP32S3**, **ESP32 Dev Module**)
3. **Tools → Port** → select the COM port for the ESP32
4. Click **Upload**

### 5. Find the ESP32's IP Address

After uploading:
1. Open **Tools → Serial Monitor** (set baud to **115200**)
2. Press the reset button on the ESP32
3. Watch for output like:
   ```
   WiFi Connected!
   IP Address: 192.168.1.XXX
   ```
   Note this IP address — you'll need it for rtpMIDI.

### 6. Install rtpMIDI (Windows)

1. Download and install **rtpMIDI** by Tobias Erichsen from:
   `https://www.tobias-erichsen.de/software/rtpmidi.html`
2. Open rtpMIDI
3. Under **"My Sessions"**, click **+** to create a new session (e.g. "Light Studio")
4. Under **"Directory"**, click **+** to add a remote participant:
   - **Address:** the ESP32's IP address (from Serial Monitor)
   - **Port:** `5004`
5. Click **Connect** — the ESP32 Serial Monitor should print `MIDI Connected to: ...`

### 7. Configure Light Studio

In Light Studio (the JUCE plugin), select the rtpMIDI virtual port (the session name you created in step 6) as the MIDI output device. MIDI notes, CCs, and SysEx will now route from Light Studio → rtpMIDI → ESP32 → LED strip.

---

## Done!

Both sketches now use `#include <LightEngine.h>` and Arduino IDE will find the library.

**To update the library**: Just edit files in `libraries/LightEngine/src/` - changes apply to both versions immediately!

## Troubleshooting

**"LightEngine.h: No such file or directory"**
- Ensure you ran setup script as Administrator
- Verify symlink exists:
  ```powershell
  ls "$env:USERPROFILE\Documents\Arduino\libraries"
  ```
  Should show `LightEngine` folder
- Restart Arduino IDE

**Setup script fails**
- Must run PowerShell as Administrator
- Windows may block execution - run:
  ```powershell
  Set-ExecutionPolicy -Scope Process -ExecutionPolicy Bypass
  ```
- Then run setup script again
