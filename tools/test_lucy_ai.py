#!/usr/bin/env python3
"""Smoke-test Lucy's PC AI bridge over the USB serial console.

Run this AFTER:
  1. tools\\start_lucy_ai.ps1  (Ollama bound to 0.0.0.0:11434)
  2. connecting the PC's Wi-Fi to Lucy-Setup
  3. setting the PC address once:  AI host 192.168.4.x 11434
     then:                         AI on

Usage:  python tools\\test_lucy_ai.py [COM5]
"""
import sys
import time

import serial

port = sys.argv[1] if len(sys.argv) > 1 else "COM5"
ser = serial.Serial(port, 115200, timeout=0.5)
time.sleep(5)  # opening the port resets the board; let it boot
ser.reset_input_buffer()


def send(text, wait=1.5):
    ser.write((text + "\n").encode())
    time.sleep(wait)
    out = b""
    end = time.time() + 0.5
    while time.time() < end:
        chunk = ser.read(4096)
        if chunk:
            out += chunk
    return [line.strip() for line in out.decode(errors="replace").splitlines() if line.strip()]


print("== bridge status ==")
for line in send("AI", 20.0):
    print("  " + line)

for prompt in ["hello", "tell me a short story about rain", "what can you do?"]:
    print(f"\n>> {prompt}")
    for line in send(prompt, 25.0):
        if line.startswith("Lucy:") or line.startswith("You:"):
            print("  " + line)

ser.close()
