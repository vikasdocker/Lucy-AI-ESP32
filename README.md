# Lucy AI – ESP32 AP+STA Chatbot

Target: classic ESP32 DevKit / ESP32-WROOM-32. Verify flash size and existing OTA partition compatibility before installing.

## Wi-Fi behavior

- Permanent AP SSID: `Edsoft-ESP32-OTA`
- AP password: `LucyAI123`
- AP URL: `http://192.168.4.1/`
- Optional router connection through WiFiManager.
- mDNS URL on the router LAN: `http://edsoft.local/` (where supported).
- Both AP and station interfaces remain enabled after WiFiManager provisioning.

On first boot, WiFiManager may show its credential portal. Connect to `Edsoft-ESP32-OTA`, enter the home router credentials, and let it reconnect. The firmware then restarts/keeps the permanent AP alongside the router connection. If no router is configured within the portal timeout, the AP remains available for local access; AI requests still need a working internet route on the phone.

## OTA compatibility

The upload page remains at `/update`, uses `POST /update`, and sends the multipart file field named `update`, matching the user's existing OTA uploader. It uses Arduino `Update` and does not intentionally change the OTA upload protocol.

**Do not flash until compatibility is confirmed.** OTA application uploads do not normally rewrite the partition table, but the new app must fit the inactive OTA app partition in the partition table already installed on the device. The source photo cannot establish flash size or partition layout. The existing working OTA sketch proves that OTA is enabled, not that this larger application fits. If uncertain, do not install until you can identify the current partition scheme or recover by USB serial.

## Build on GitHub (no laptop)

1. Open repository → Actions.
2. Open **Build Lucy AI ESP32 firmware** and wait for success.
3. Download the `lucy-ai-esp32-firmware` artifact ZIP.
4. In iPhone Files, extract it and select `lucy-ai-esp32dev.bin`.
5. Join `Edsoft-ESP32-OTA`, open `http://192.168.4.1/update`, select the binary and upload.
6. Keep the ESP32 powered. Rejoin the AP after reboot.

## AI backend

Deploy `worker/worker.js` as a Cloudflare Worker and add the Worker secret `OPENAI_API_KEY`. Never commit the key to GitHub or embed it in firmware. In Lucy AI → AI settings, save the deployed HTTPS Worker URL.

The Worker permits browser origins `http://192.168.4.1` and `http://edsoft.local`. CORS is not authentication. Before exposing this beyond personal testing, add authenticated sessions/device tokens, rate limits, abuse controls and usage budgets. OpenAI API billing is separate from ChatGPT subscriptions.

## Internet note

AP+STA keeps the ESP32 connected to the router while it hosts its own AP. It does not automatically route the router's internet to AP clients. The phone may use cellular data for the Worker while connected to the ESP32 AP; if iOS does not route it, use a supported internet bridge or a router-connected phone arrangement.
