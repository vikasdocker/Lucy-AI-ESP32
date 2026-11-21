# Lucy AI – ESP32 AP+STA Chatbot

**Cat OS upgrade for ESP32-C3 Mini:** see [offline/C3_MINI.md](offline/C3_MINI.md)
for the animated OLED pet, 120 clips, MAX98357A audio and one-button settings.
Build it from `offline/` with `pio run -e lucy_c3_mini`. The web-only firmware
below remains a separate classic ESP32 target.

Target: classic ESP32 DevKit / ESP32-WROOM-32 (`esp32dev`).

## Wi-Fi setup

- Permanent setup AP: `Edsoft-ESP32-OTA`
- AP password: `Intel2023#`
- AP dashboard: `http://192.168.4.1/`
- Wi-Fi manager: `http://192.168.4.1/wifi`
- Live logs: `http://192.168.4.1/logs`
- Diagnostics: `http://192.168.4.1/info`
- OTA update: `http://192.168.4.1/update`
- mDNS on the router LAN: `http://edsoft.local/` (where supported)

Connect your phone to the ESP32 setup AP, open the dashboard, choose **Wi-Fi settings**, scan for networks, select the SSID, enter its password and save. The credentials are stored in ESP32 NVS. The setup AP remains available while the ESP32 connects to the router. If no credentials have been saved, a captive DNS portal directs common captive-network checks to the dashboard.

Chat uses a local ESP32 proxy: the ESP32 reaches the Cloudflare Worker through the saved router Wi-Fi. A phone on Lucy's hotspot does not need its own internet connection. A privately provisioned firmware credential and matching Worker secret are required; public CI images provide offline behavior only.

## Live system log monitor

Open `/logs` for an EventSource/SSE live stream and the most recent 80 buffered events. Logs include boot, Wi-Fi state changes, SSID/IP/RSSI, browser-side AI request outcomes, OTA upload progress/errors and server events. Use Download to save the visible log session. The buffer is in RAM and clears on reboot; it is not persistent storage.

## OTA compatibility and safety

The upload page remains at `/update`, uses `POST /update`, and sends the multipart file field named `update`.

**Important:** This firmware is built with the repository's `min_spiffs.csv` partition layout. Do not install it over an existing device until the existing partition table and inactive OTA app-slot capacity are confirmed. The new application must fit the inactive OTA slot. If uncertain, do not install until the partition scheme is identified or USB serial recovery is available. Keep the device powered during OTA.

## Build on GitHub (no laptop)

1. Open this repository → **Actions**.
2. Open **Build Lucy AI ESP32 firmware** and wait for the run to finish successfully.
3. Download the `lucy-ai-esp32-firmware` artifact ZIP.
4. Extract it and select `lucy-ai-esp32dev.bin`.
5. Join `Edsoft-ESP32-OTA`, open `http://192.168.4.1/update`, select the binary and upload only after checking partition compatibility.
6. Keep the ESP32 powered. Rejoin the AP after reboot.

The artifact also includes `SHA256SUMS.txt` for integrity verification.

## Cloudflare / OpenAI backend

Lucy uses the fixed Worker `https://sparkling-fog-fdc8.vikasshu7.workers.dev` and `gpt-4o-mini`. Users only enter Wi-Fi credentials; AI URL/model/provider controls are absent. The OpenAI key stays in the Worker secret `OPENAI_API_KEY`. Firmware uses a separate private device token, never a provider key. Public GitHub firmware artifacts deliberately omit this credential.

See [the provisioning, security and rollout guide](worker/ZERO_CONFIG.md) for private builds, authenticated API formats, verified TLS, offline fallback and approval-gated deployment. No device flashing or production Worker deployment is automatic. The older optional `gateway/` utilities are not part of the default cloud path.
