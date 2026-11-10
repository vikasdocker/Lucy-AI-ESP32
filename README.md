# Lucy AI – ESP32 AP+STA Chatbot

Target: classic ESP32 DevKit / ESP32-WROOM-32. Verify flash size and existing OTA partition compatibility before installing.

## Wi-Fi behavior

- Permanent AP SSID: `Edsoft-ESP32-OTA`
- AP password: `LucyAI123`
- AP URL: `http://192.168.4.1/`
- Optional router connection through WiFiManager.
- mDNS URL on the router LAN: `http://edsoft.local/` (where supported).
- Both AP and station interfaces remain enabled after WiFiManager provisioning.

On first boot, WiFiManager may show its credential portal. Connect to `Edsoft-ESP32-OTA`, enter the home router credentials, and let it reconnect. If no router is configured within the portal timeout, the AP remains available for local access. AI requests require the phone to have internet access (for example, cellular data while connected to the ESP32 AP, if the phone supports that routing).

## OTA compatibility

The upload page remains at `/update`, uses `POST /update`, and sends the multipart file field named `update`, matching the existing OTA uploader.

**Do not flash until compatibility is confirmed.** The new app must fit the inactive OTA application partition in the partition table already installed on the device. The board photo cannot establish flash size or partition layout. If uncertain, do not install until the current partition scheme is identified or USB serial recovery is available.

## Build on GitHub (no laptop)

1. Open repository → Actions.
2. Open **Build Lucy AI ESP32 firmware** and wait for success.
3. Download the `lucy-ai-esp32-firmware` artifact ZIP.
4. Extract it and select `lucy-ai-esp32dev.bin`.
5. Join `Edsoft-ESP32-OTA`, open `http://192.168.4.1/update`, select the binary and upload.
6. Keep the ESP32 powered. Rejoin the AP after reboot.

## OpenRouter backend setup

Deploy `worker/worker.js` as a Cloudflare Worker. In Cloudflare Worker → Settings → Variables and Secrets, add a **Secret** named `OPENROUTER_API_KEY` with your newly rotated OpenRouter key. Do not commit the key to GitHub, put it in the web page, or embed it in firmware.

Optional: add a plain-text variable `OPENROUTER_MODEL` to choose a model available to your OpenRouter account. The default is `openai/gpt-4.1-mini`; verify availability and pricing in OpenRouter before use.

In Lucy AI → AI settings, save the deployed HTTPS Worker URL. The Worker calls `https://openrouter.ai/api/v1/chat/completions`.

The Worker currently allows browser origins `http://192.168.4.1` and `http://edsoft.local`. CORS is not authentication. Before exposing this beyond personal testing, add authenticated sessions/device tokens, rate limiting and usage budgets.

## Internet note

AP+STA keeps the ESP32 connected to the router while it hosts its own AP. It does not automatically route the router's internet to AP clients. The phone may use cellular data for the Worker while connected to the ESP32 AP; iOS carrier/Wi-Fi routing behavior can vary.
