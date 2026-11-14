#!/usr/bin/env python3
"""Provision Lucy for the public model gateway over USB serial.

Run this yourself: it prompts for your Wi-Fi name/password locally and reads
the gateway token from gateway/config.json. Nothing secret is printed.

    python tools\\provision_lucy_ai.py [COM5] [--url https://lucy.blueflutex.in]

Requirements:
  * Lucy flashed with the current firmware and connected over USB.
  * Your Wi-Fi must be 2.4 GHz (ESP32 has no 5 GHz radio).
  * gateway/config.json exists: run `node gateway/setup.js` first.

This sets, in NVS:
    WIFI <ssid> <password>     station credentials (AP stays up)
    AI url https://...         gateway endpoint
    AI token <secret>          same token as gateway/config.json
    AI insecure off            verify the TLS certificate
    AI on                      use the model when reachable

Then it waits for the join, pings the gateway, and asks Lucy one test question.
"""

import argparse
import getpass
import json
import os
import sys
import time
from pathlib import Path

try:
    import serial
except ImportError:
    print("pyserial is missing:  python -m pip install pyserial")
    raise SystemExit(1)

ROOT = Path(__file__).resolve().parent.parent
CONFIG = ROOT / "gateway" / "config.json"


def read_token():
    if not CONFIG.exists():
        print(f"missing {CONFIG}")
        print("run:  node gateway/setup.js")
        raise SystemExit(1)
    token = json.loads(CONFIG.read_text(encoding="utf-8")).get("token", "")
    if len(token) < 24 or token.startswith("CHANGE_ME"):
        print("gateway token is missing or too short; run: node gateway/setup.js --rotate")
        raise SystemExit(1)
    return token


class Lucy:
    def __init__(self, port):
        self.ser = serial.Serial(port, 115200, timeout=0.5)
        time.sleep(5)  # opening the port resets the board
        self.ser.reset_input_buffer()

    def send(self, command, wait=1.2):
        self.ser.write((command + "\n").encode())
        time.sleep(wait)
        out = b""
        end = time.time() + 0.5
        while time.time() < end:
            chunk = self.ser.read(4096)
            if chunk:
                out += chunk
        return [line.strip() for line in out.decode(errors="replace").splitlines() if line.strip()]

    def close(self):
        self.ser.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("port", nargs="?", default="COM5")
    parser.add_argument("--url", default="https://lucy.blueflutex.in")
    parser.add_argument("--ssid", help="Wi-Fi name (otherwise prompted)")
    parser.add_argument("--password", help="Wi-Fi password (discouraged: shows in shell history)")
    args = parser.parse_args()

    token = read_token()
    ssid = args.ssid or os.environ.get("LUCY_WIFI_SSID", "").strip() \
        or input("Wi-Fi SSID (2.4 GHz only): ").strip()
    if not ssid:
        print("SSID cannot be empty")
        return 1
    if args.password is not None:
        password = args.password
    elif os.environ.get("LUCY_WIFI_PASS") is not None:
        password = os.environ["LUCY_WIFI_PASS"]
    else:
        password = getpass.getpass(f"Password for '{ssid}' (blank = open network): ")

    if " " in ssid:
        print("note: spaces in the SSID are not supported by the serial command")
        return 1
    if len(ssid) + len(password) + 6 > 95:
        print("SSID + password are too long for the serial console (max ~89 chars). Use a shorter password.")
        return 1

    print(f"\nConnecting to Lucy on {args.port} ...")
    lucy = Lucy(args.port)

    print("1/5  saving Wi-Fi credentials ...")
    for line in lucy.send(f"WIFI {ssid} {password}", 2.0):
        print("     " + line)

    print("2/5  pointing Lucy at the public gateway ...")
    for line in lucy.send(f"AI url {args.url}"):
        print("     " + line)

    print("3/5  installing the gateway token (hidden) ...")
    for line in lucy.send(f"AI token {token}"):
        print("     " + line)

    print("4/5  enabling verified TLS + the AI bridge ...")
    for line in lucy.send("AI insecure off"):
        print("     " + line)
    for line in lucy.send("AI on"):
        print("     " + line)

    print("5/5  waiting for the Wi-Fi join ...")
    joined = False
    for _ in range(20):
        lines = lucy.send("WIFI status", 1.0)
        text = " ".join(lines)
        if "connected to" in text:
            joined = True
            for line in lines:
                print("     " + line)
            break
        time.sleep(2)
    if not joined:
        print("\nCould not join that network.")
        print("Check: 2.4 GHz band, password, and that the SSID is in range.")
        print("Lucy is still reachable at http://192.168.4.1 over Lucy-Setup.")
        lucy.close()
        return 1

    print("\nPinging the public model gateway (TLS + token) ...")
    status = lucy.send("AI", 15.0)
    for line in status:
        print("     " + line)
    text = " ".join(status)
    if "reachable" not in text:
        print("\nThe Wi-Fi link works but the gateway did not answer.")
        print("On the PC, run:  powershell -ExecutionPolicy Bypass -File tools\\start_lucy_public.ps1")
        lucy.close()
        return 1

    print("\nAsking Lucy a test question ...")
    for line in lucy.send("Who are you, in one short sentence?", 20.0):
        if line.startswith("Lucy:") or line.startswith("You:"):
            print("     " + line)

    print("\nDone. Settings are stored in NVS and survive reboots.")
    lucy.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
