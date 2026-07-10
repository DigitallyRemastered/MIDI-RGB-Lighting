#!/usr/bin/env python3
"""
Parallel ArduinoOTA uploader for the Lights ESP32-C3 fleet.

Pushes ONE firmware image to MANY boards at once over WiFi, using the ESP32
Arduino core's bundled espota.py. Run it after every board is already running
ArduinoOTA-capable firmware (either the full lights_esp32 sketch or the tiny
lights_ota_bootstrap provisioning sketch) and is on the same network.

Because the boards now load their BLE/OTA name from NVS at boot (SysEx 0x0F),
one binary works for the whole fleet -- no per-board recompile.

Usage
-----
    # explicit targets (IP or mDNS hostname):
    python ota_upload.py firmware.bin --targets 192.168.1.101 192.168.1.102
    python ota_upload.py firmware.bin --targets Lights-9A2F.local Lights-1C7E.local

    # targets from a file (one per line, '#' comments allowed):
    python ota_upload.py firmware.bin --targets-file boards.txt

    # defaults: uploads ./firmware/lights_esp32.ino.bin to DEFAULT_TARGETS below
    python ota_upload.py

Notes
-----
* Upload the app image (lights_esp32.ino.bin), NOT the .merged/.bootloader bin.
* Set the ESPOTA env var to override espota.py auto-discovery.
* If the firmware sets ArduinoOTA.setPassword(), fill in OTA_AUTH below.
"""

import argparse
import concurrent.futures
import glob
import os
import socket
import subprocess
import sys
import time

# --- defaults (edit to taste) ----------------------------------------------
DEFAULT_BIN = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "firmware", "lights_esp32.ino.bin")
DEFAULT_TARGETS = [
    "192.168.8.130",
    "192.168.8.136",
    "192.168.8.192",
    "192.168.8.159",
    "192.168.8.149",
    "192.168.8.243",
]
OTA_PORT = 3232      # matches ArduinoOTA.setPort(3232) in the sketches
OTA_AUTH = ""        # set only if the firmware calls ArduinoOTA.setPassword(...)


def find_espota():
    """Locate espota.py: $ESPOTA override, else newest installed esp32 core."""
    env = os.environ.get("ESPOTA")
    if env and os.path.isfile(env):
        return env
    patterns = [
        os.path.expandvars(r"%LOCALAPPDATA%\Arduino15\packages\esp32"
                            r"\hardware\esp32\*\tools\espota.py"),
        os.path.expanduser("~/.arduino15/packages/esp32"
                           "/hardware/esp32/*/tools/espota.py"),
        os.path.expanduser("~/Library/Arduino15/packages/esp32"
                           "/hardware/esp32/*/tools/espota.py"),
    ]
    hits = []
    for pat in patterns:
        hits.extend(glob.glob(pat))
    return sorted(hits)[-1] if hits else None


def is_ip(s):
    parts = s.split(".")
    return len(parts) == 4 and all(p.isdigit() and 0 <= int(p) <= 255 for p in parts)


def resolve(target):
    """IP passes through; hostnames (incl. *.local via mDNS) resolve to an IP."""
    if is_ip(target):
        return target
    try:
        return socket.gethostbyname(target)
    except socket.gaierror:
        return None


def upload_one(espota, binfile, target):
    ip = resolve(target)
    label = target if ip in (None, target) else f"{target} ({ip})"
    if ip is None:
        return (label, False, "could not resolve hostname (mDNS/Bonjour running?)")
    cmd = [sys.executable, espota, "-i", ip, "-p", str(OTA_PORT), "-f", binfile]
    if OTA_AUTH:
        cmd += ["-a", OTA_AUTH]
    t0 = time.time()
    proc = subprocess.run(cmd, capture_output=True, text=True)
    dt = time.time() - t0
    if proc.returncode == 0:
        return (label, True, f"{dt:.1f}s")
    tail = (proc.stderr.strip() or proc.stdout.strip() or f"exit {proc.returncode}")
    return (label, False, tail.splitlines()[-1] if tail else f"exit {proc.returncode}")


def load_targets(args):
    targets = list(args.targets or [])
    if args.targets_file:
        with open(args.targets_file) as f:
            for line in f:
                line = line.split("#", 1)[0].strip()
                if line:
                    targets.append(line)
    return targets or DEFAULT_TARGETS


def main():
    ap = argparse.ArgumentParser(
        description="Parallel ArduinoOTA uploader for the Lights fleet.")
    ap.add_argument("binfile", nargs="?", default=DEFAULT_BIN,
                    help="app image (.ino.bin) to upload")
    ap.add_argument("--targets", nargs="+", help="board IPs or hostnames")
    ap.add_argument("--targets-file", help="file with one IP/hostname per line")
    ap.add_argument("--jobs", type=int, default=8,
                    help="max concurrent uploads (default 8)")
    args = ap.parse_args()

    targets = load_targets(args)
    if not targets:
        sys.exit("No targets. Pass --targets ... / --targets-file, "
                 "or edit DEFAULT_TARGETS.")

    binfile = os.path.abspath(args.binfile)
    if not os.path.isfile(binfile):
        sys.exit(f"Firmware not found: {binfile}\n"
                 "Build it first (see ../SETUP_INSTRUCTIONS.md).")

    espota = find_espota()
    if not espota:
        sys.exit("espota.py not found. Set the ESPOTA env var to its path.")

    print(f"Firmware : {binfile} ({os.path.getsize(binfile) // 1024} KB)")
    print(f"espota   : {espota}")
    print(f"Targets  : {len(targets)} board(s), up to {args.jobs} in parallel\n")

    ok_count = 0
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as ex:
        futs = {ex.submit(upload_one, espota, binfile, t): t for t in targets}
        for fut in concurrent.futures.as_completed(futs):
            label, ok, msg = fut.result()
            print(f"  [{'OK  ' if ok else 'FAIL'}] {label}  {msg}")
            ok_count += 1 if ok else 0

    print(f"\n{ok_count}/{len(targets)} succeeded.")
    sys.exit(0 if ok_count == len(targets) else 1)


if __name__ == "__main__":
    main()
