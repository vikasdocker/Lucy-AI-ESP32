# Lucy AI – ESP32 AP+STA Chatbot

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

The phone's connection to the ESP32 AP does not automatically provide internet routing through the ESP32. AI requests are made by the phone's browser to the HTTPS Cloudflare Worker; phone/carrier routing behavior while connected to a no-internet AP can vary.

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

## OpenRouter / Cloudflare backend

Deploy `worker/worker.js` as a Cloudflare Worker. In Cloudflare Worker → Settings → Variables and Secrets, add a **Secret** named `OPENROUTER_API_KEY`. Do not commit the key to GitHub, put it in the web page, or embed it in firmware.

Optional: add a plain-text variable `OPENROUTER_MODEL` to choose a model available to your OpenRouter account. The Worker calls `https://openrouter.ai/api/v1/chat/completions`.

In Lucy AI → AI settings, save the deployed HTTPS Worker URL. Use exactly one `https://` prefix.

The Worker currently allows browser origins `http://192.168.4.1` and `http://edsoft.local`. CORS is not authentication. Before exposing this beyond personal testing, add authenticated sessions/device tokens, rate limiting and usage budgets.
