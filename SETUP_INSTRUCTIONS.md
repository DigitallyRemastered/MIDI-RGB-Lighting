# LightEngine / Lights ESP32 — Setup & Fleet Flashing

This covers building the ESP32-C3 firmware with **arduino-cli** and flashing a
whole fleet of boards over WiFi with **ArduinoOTA**. Because each board now loads
its BLE/OTA name from non-volatile memory at boot (set at runtime from the Light
Studio plugin via SysEx `0x0F`), **one firmware binary works for every board** —
no per-board recompile.

- **Target board:** Nologo ESP32-C3 Super Mini (`esp32:esp32:esp32c3` is fine for
  building; the exact board id is `esp32:esp32:nologo_esp32c3_super_mini`).
- **Toolchain:** `arduino-cli` (1.5+) with the `esp32` core installed.

---

## 1. One-time toolchain setup

### 1a. Install arduino-cli + ESP32 core

```powershell
# core (once):
arduino-cli config init                     # if you have no config yet
arduino-cli config add board_manager.additional_urls `
  https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

### 1b. Libraries

Repo-local libraries (`LightEngine`, `ESP32-BLE-MIDI`, `NimBLE-Arduino`) live in
`libraries/` and are passed with `--libraries`. **AppleMIDI** and **FastLED** must
live in a second directory you also pass with `--libraries`. On this machine that
is `C:\Users\sandm\Documents\Arduino\libraries` (the exact path used in the build
command below).

```powershell
# Install into that directory. Note: `arduino-cli lib install` places libraries
# in `arduino-cli config get directories.user`\libraries — if that differs from
# the path you pass to --libraries, copy them over or just point --libraries at
# wherever they landed.
arduino-cli lib install "FastLED" "AppleMIDI"
```

> The build always works as long as both `--libraries` paths (repo `libraries/`
> **and** the AppleMIDI/FastLED dir) are on the command line — that's why the
> commands below pass two `--libraries` flags.

### 1c. WiFi credentials

Copy `Source/lights_esp32/secrets.h.example` → `secrets.h` and fill in your
network (git-ignored). Do the same for `Source/lights_ota_bootstrap/` if you use
the bootstrap sketch. All boards must be on the **same network** for OTA/rtpMIDI.

---

## 2. Build the firmware (arduino-cli)

The ESP32-C3's default OTA partition slot is only 1.25 MB and the firmware fills
99% of it. We use the **min_spiffs** layout (1.875 MB app, still dual-OTA) via a
sketch-local `partitions.csv` (already committed in `Source/lights_esp32/`). The
core honors it automatically. You **also** must raise the size-check gate with
`--build-property upload.maximum_size=1966080`, otherwise arduino-cli still
rejects anything over 1.25 MB even though it physically fits.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32c3 `
  --libraries "D:\Code\Lights\MIDI-RGB-Lighting\libraries" `
  --libraries "C:\Users\sandm\Documents\Arduino\libraries" `
  --build-property upload.maximum_size=1966080 `
  --output-dir "D:\Code\Lights\MIDI-RGB-Lighting\Release\firmware" `
  "D:\Code\Lights\MIDI-RGB-Lighting\Source\lights_esp32"
```

Expected: `Sketch uses 1302713 bytes (66%) of program storage space. Maximum is
1966080 bytes.`

### Output artifacts (`Release/firmware/`)

| File | What it is | Used for |
|------|------------|----------|
| **`lights_esp32.ino.bin`** | app image (~1.27 MB) | **OTA uploads (espota)** |
| `lights_esp32.ino.merged.bin` | full 4 MB flash image | one-shot USB flash via esptool |
| `lights_esp32.ino.bootloader.bin` / `.partitions.bin` | bootloader / partition table | USB flash internals |

> There is no file literally named `firmware.bin` — the OTA image is
> **`lights_esp32.ino.bin`**. `Release/ota_upload.py` defaults to that path.

---

## 3. Initial USB flash (per board, once)

A board must already be running ArduinoOTA-capable firmware before it can be
updated wirelessly. You have two options for the very first flash:

### Option A — flash the real firmware directly
```powershell
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p COM13 `
  --input-dir "D:\Code\Lights\MIDI-RGB-Lighting\Release\firmware"
```

### Option B — flash the tiny bootstrap sketch first (faster per board)
`Source/lights_ota_bootstrap/` is a minimal WiFi + ArduinoOTA sketch that
compiles and USB-uploads slightly faster. Flash it to every board, then push the real
firmware to all of them **in parallel** over WiFi (step 4). It ships the **same
min_spiffs `partitions.csv`**, so the partition table it lays down is big enough
for the real firmware's later OTA push.

```powershell
arduino-cli compile --fqbn esp32:esp32:esp32c3 `
  --libraries "C:\Users\sandm\Documents\Arduino\libraries" `
  "D:\Code\Lights\MIDI-RGB-Lighting\Source\lights_ota_bootstrap"
arduino-cli upload --fqbn esp32:esp32:esp32c3 -p COM13 `
  "D:\Code\Lights\MIDI-RGB-Lighting\Source\lights_ota_bootstrap"
```

Either way, each board comes up on WiFi advertising itself over mDNS as
**`Lights-XXXX.local`** (`XXXX` = last two MAC octets), unique per board.

---

## 4. Fleet OTA upload (all boards at once)

`Release/ota_upload.py` runs the ESP32 core's `espota.py` against many boards
concurrently (stdlib only — no pip installs). Auto-discovers `espota.py` from the
installed core; override with the `ESPOTA` env var.

```powershell
# by IP:
python Release\ota_upload.py Release\firmware\lights_esp32.ino.bin `
  --targets 192.168.1.101 192.168.1.102 192.168.1.103

# by mDNS hostname (needs Bonjour/mDNS resolution on the host):
python Release\ota_upload.py --targets Lights-9A2F.local Lights-1C7E.local

# from a file (one IP/hostname per line, '#' comments ok):
python Release\ota_upload.py --targets-file boards.txt
```

It prints a per-board `[OK]/[FAIL]` line with timing and exits non-zero if any
board failed. Tune concurrency with `--jobs N` (default 8). ~10 boards in
parallel is comfortable on a normal network; 50+ starts to saturate WiFi.

---

## 5. Name each board (Light Studio plugin)

Fresh boards default to `Lights-XXXX`. To give one a friendly name:

1. Connect the plugin to **one** board (rtpMIDI or BLE).
2. **File → Set Board Name…**, type a name (e.g. `Stage Left`).
3. The board saves it to NVS and **reboots** into the new name (BLE scan list +
   mDNS hostname). Reconnect under the new name, then move to the next board.

The rename targets whichever board the plugin is connected to, so rename boards
**one connection at a time**.

---

## 6. Connect Light Studio over the network (rtpMIDI, Windows)

1. Install **rtpMIDI** by Tobias Erichsen:
   `https://www.tobias-erichsen.de/software/rtpmidi.html`
2. Create a session under **My Sessions** (e.g. "Light Studio").
3. Under **Directory**, add each board: **Address** = board IP, **Port** `5004`,
   then **Connect**.
4. In Light Studio, select the rtpMIDI virtual port as the MIDI output. Notes,
   CCs, and SysEx now route Light Studio → rtpMIDI → ESP32 → LED strip.

> **BLE-MIDI alternative:** the firmware is dual-mode. If your host supports
> BLE-MIDI you can pair directly and skip rtpMIDI. (BLE advertising is paused
> while an rtpMIDI session is active so the C3's single radio isn't time-sliced.)

---

## 7. Teensy build (USB-MIDI)

```powershell
arduino-cli compile --fqbn teensy:avr:teensy41:usb=midi `
  --libraries "D:\Code\Lights\MIDI-RGB-Lighting\libraries" `
  --libraries "C:\Users\sandm\Documents\Arduino\libraries" `
  "D:\Code\Lights\MIDI-RGB-Lighting\Source\lights_teensy"
```

`usb=midi` is required or `usbMIDI` is undeclared. Teensy state persists to EEPROM
on SysEx `0x06`.

---

## Troubleshooting

**OTA fails with "Not Enough Space"** — the board was USB-flashed with the default
1.25 MB partition, too small for the ~1.3 MB firmware. Re-flash over USB once with
the min_spiffs `partitions.csv` in the sketch folder (both `lights_esp32` and
`lights_ota_bootstrap` already include it), then OTA works.

**Sketch too big at 99% / "text section exceeds available space"** — you omitted
`--build-property upload.maximum_size=1966080`. The `partitions.csv` sets the
table but not the CLI/IDE size gate.

**`AppleMIDI.h: No such file or directory`** — install FastLED + AppleMIDI into
`~/Documents/Arduino/libraries` (step 1b), and confirm both `--libraries` paths
are passed.

**`espota.py not found`** — set `ESPOTA` to its full path, e.g.
`%LOCALAPPDATA%\Arduino15\packages\esp32\hardware\esp32\<ver>\tools\espota.py`.

**Board not reachable for OTA** — confirm it's on WiFi (serial at 115200 prints
`Lights-XXXX ready for OTA at <ip>`), on the same subnet, and not asleep.
