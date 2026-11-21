#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <time.h>
#include "clipbot_tts_ca.h"
#include "board_config.h"
#include "speaker.h"
#include "cat_animation.h"
#if LUCY_I2S_DIN >= 0
#include <driver/i2s.h>
#include "greeting_data.h"
#endif

namespace {
bool ready = false;
uint8_t volume = 50;
#if LUCY_I2S_DIN >= 0
bool greeting = false;
uint32_t position = 0, remaining = 0, phase = 0, noise = 1;
uint16_t frequency = 440;
uint8_t voice = cat::MEOW;
int16_t buffer[256]; // 128 stereo frames, fixed 512-byte working buffer.
size_t pending = 0, sent = 0;
int16_t decode(uint8_t value) {
  value = ~value;
  int sample = ((value & 15) << 3) + 132;
  sample <<= (value & 0x70) >> 4;
  sample -= 132;
  return value & 128 ? -sample : sample;
}
#endif
}
void speakerBegin(uint8_t value) {
  speakerVolume(value);
#if LUCY_I2S_DIN >= 0
  i2s_config_t cfg = {};
  cfg.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_TX);
  cfg.sample_rate = 16000;
  cfg.bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT;
  cfg.channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT;
  cfg.communication_format = I2S_COMM_FORMAT_STAND_I2S;
  cfg.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1;
  cfg.dma_buf_count = 4;
  cfg.dma_buf_len = 128;
  cfg.tx_desc_auto_clear = true;
  i2s_pin_config_t pins = {};
  pins.bck_io_num = LUCY_I2S_BCLK;
  pins.ws_io_num = LUCY_I2S_LRC;
  pins.data_out_num = LUCY_I2S_DIN;
  pins.data_in_num = I2S_PIN_NO_CHANGE;
  ready = i2s_driver_install(I2S_NUM_0, &cfg, 0, nullptr) == ESP_OK;
  if (ready && i2s_set_pin(I2S_NUM_0, &pins) != ESP_OK) {
    i2s_driver_uninstall(I2S_NUM_0);
    ready = false;
  }
  if (ready) i2s_zero_dma_buffer(I2S_NUM_0);
#endif
}
void speakerVolume(uint8_t value) { volume = value > 100 ? 100 : value; }
bool speakerReady() { return ready; }
bool speakerBusy() {
#if LUCY_I2S_DIN >= 0
  return ready && (remaining || pending);
#else
  return false;
#endif
}
void speakerGreeting() {
#if LUCY_I2S_DIN >= 0
  if (!ready) return;
  greeting = true;
  position = 0;
  remaining = sizeof(GREETING_DATA);
  pending = sent = 0;
#endif
}
void speakerCat(uint8_t behaviour, uint8_t variant) {
#if LUCY_I2S_DIN >= 0
  if (!ready || speakerBusy() || !volume) return;
  if (behaviour < cat::PURR || behaviour > cat::YOWL) return;
  greeting = false;
  voice = behaviour;
  position = phase = 0;
  frequency = behaviour == cat::PURR ? 32 : 420 + variant * 55;
  remaining = behaviour == cat::PURR ? 12000 : 4800 + variant * 900;
#endif
}
void speakerTick() {
#if LUCY_I2S_DIN >= 0
  if (!ready) return;
  for (int batch = 0; batch < 4; batch++) {
  // Nonblocking DMA writes: finish the partial write before producing new audio.
  if (!pending && remaining) {
    const size_t count = remaining > 128 ? 128 : remaining;
    for (size_t i = 0; i < count; i++, position++, remaining--) {
      int32_t sample;
      if (greeting) sample = decode(GREETING_DATA[position]);
      else {
        uint32_t hz = frequency;
        if (voice == cat::MEOW || voice == cat::YOWL) hz += position < 2200 ? position / 12 : 180 - int(position - 2200) / 40;
        if (voice == cat::CHIRP) hz += (position % 1800) / 2;
        phase += hz * 65536U / 16000U;
        const int32_t triangle = int32_t(phase & 65535U);
        sample = triangle < 32768 ? triangle - 16384 : 49152 - triangle;
        if (voice == cat::HISS || voice == cat::CHATTER) {
          noise ^= noise << 13; noise ^= noise >> 17; noise ^= noise << 5;
          sample = int16_t(noise) / 3;
          if (voice == cat::CHATTER && position % 1000 > 450) sample = 0;
        }
        if (voice == cat::PURR) sample /= 3;
        const uint32_t attack = position < 400 ? position : 400;
        const uint32_t release = remaining < 400 ? remaining : 400;
        sample = sample * int32_t(attack < release ? attack : release) / 400;
      }
      // Conservative gain for the small speaker. Both I2S channels carry mono.
      sample = sample * volume / 200;
      buffer[i * 2] = buffer[i * 2 + 1] = int16_t(sample);
    }
    pending = count * 4;
    sent = 0;
  }
  if (pending) {
    size_t written = 0;
    i2s_write(I2S_NUM_0, reinterpret_cast<uint8_t *>(buffer) + sent, pending, &written, 0);
    sent += written;
    pending -= written;
    if (!written) break;
  }
  }
#endif
}

bool speakerSpeakPcm16Url(const String& url, const String& token) {
#if LUCY_I2S_DIN >= 0
  if (!ready || volume == 0 || WiFi.status() != WL_CONNECTED) return false;
  WiFiClient client;
  WiFiClientSecure secure;
  HTTPClient http;
  http.setConnectTimeout(3500);
  http.setTimeout(7000);
  const bool tls = url.startsWith("https://");
  if (tls) {
    if (time(nullptr) < 1700000000) {
      Serial.println("Clipbot TTS: system clock not synchronized; refusing HTTPS");
      return false;
    }
    secure.setCACert(CLIPBOT_TTS_CA);
    if (!http.begin(secure, url)) return false;
  } else if (url.startsWith("http://")) {
    if (!http.begin(client, url)) return false;
  } else return false;
  if (token.length()) http.addHeader("X-Clipbot-Token", token);
  const int status = http.GET();
  const int length = http.getSize();
  if (status != 200 || length <= 0 || length > 1024000 || (length % 2)) {
    Serial.printf("Clipbot TTS HTTP error: %d / size: %d\n", status, length);
    http.end();
    return false;
  }
  // Stop cat effect buffers before streaming actual speech.
  remaining = 0;
  pending = sent = 0;
  i2s_zero_dma_buffer(I2S_NUM_0);
  WiFiClient* stream = http.getStreamPtr();
  uint8_t mono[512];
  int16_t stereo[512];
  int left = length;
  bool ok = true;
  while (left > 0) {
    int chunk = left > int(sizeof(mono)) ? sizeof(mono) : left;
    const size_t read = stream->readBytes(reinterpret_cast<char*>(mono), chunk);
    if (read != size_t(chunk)) { ok = false; break; }
    const int frames = chunk / 2;
    for (int i = 0; i < frames; ++i) {
      const int16_t sample = int16_t(uint16_t(mono[2*i]) | (uint16_t(mono[2*i+1]) << 8));
      // Similar conservative digital gain as existing cat sounds.
      const int16_t scaled = int16_t(int32_t(sample) * volume / 200);
      stereo[2*i] = scaled;
      stereo[2*i+1] = scaled;
    }
    size_t written = 0;
    if (i2s_write(I2S_NUM_0, stereo, frames * 4, &written, pdMS_TO_TICKS(2500)) != ESP_OK ||
        written != size_t(frames * 4)) { ok = false; break; }
    left -= chunk;
  }
  i2s_zero_dma_buffer(I2S_NUM_0);
  http.end();
  return ok && left == 0;
#else
  (void)url;
  return false;
#endif
}
