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
- Open `Source/lights/lights.ino`
- Select **Tools → Board → Teensy X.X**
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
