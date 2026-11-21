"""Clipbot speech bridge: convert Edge neural speech to 16kHz PCM.
Requires Python 3.11+, `pip install edge-tts` and ffmpeg on PATH.
Uses internet access for TTS. Keep this server on a trusted LAN only.
"""
import asyncio
import os
import subprocess
import tempfile
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import parse_qs, urlparse
import hmac
import edge_tts

HOST = os.environ.get("CLIPBOT_TTS_HOST", "0.0.0.0")
PORT = int(os.environ.get("CLIPBOT_TTS_PORT", "8765"))
VOICE = os.environ.get("CLIPBOT_TTS_VOICE", "en-US-AriaNeural")
TOKEN = os.environ.get("CLIPBOT_TTS_TOKEN", "")

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if TOKEN and not hmac.compare_digest(self.headers.get("X-Clipbot-Token", ""), TOKEN):
            self.send_error(401, "Valid TTS token required")
            return
        u = urlparse(self.path)
        if u.path != "/tts":
            self.send_error(404)
            return
        text = parse_qs(u.query).get("text", [""])[0].strip()
        if not text or len(text) > 180:
            self.send_error(400, "Text must be 1-180 characters")
            return
        try:
            with tempfile.TemporaryDirectory() as folder:
                mp3 = os.path.join(folder, "speech.mp3")
                asyncio.run(edge_tts.Communicate(text, VOICE).save(mp3))
                pcm = subprocess.run(
                    ["ffmpeg", "-nostdin", "-loglevel", "error", "-i", mp3,
                     "-f", "s16le", "-ac", "1", "-ar", "16000", "pipe:1"],
                    capture_output=True, check=True, timeout=25
                ).stdout
            if not pcm or len(pcm) > 1024000:
                self.send_error(413, "Speech too long")
                return
            self.send_response(200)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(len(pcm)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(pcm)
        except Exception as err:
            print("TTS error:", type(err).__name__, err)
            try: self.send_error(502, "TTS backend unavailable")
            except OSError: pass

if __name__ == "__main__":
    if not TOKEN and HOST not in ("127.0.0.1", "::1", "localhost"):
        raise SystemExit("Set CLIPBOT_TTS_TOKEN for network-accessible deployments")
    print(f"Clipbot TTS listening on {HOST}:{PORT}")
    ThreadingHTTPServer((HOST, PORT), Handler).serve_forever()
