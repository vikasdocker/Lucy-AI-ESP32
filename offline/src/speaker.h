#pragma once
#include <stdint.h>
#include <Arduino.h>
void speakerBegin(uint8_t volume);
void speakerVolume(uint8_t volume);
void speakerGreeting();
void speakerCat(uint8_t behaviour, uint8_t variant);
void speakerTick();
bool speakerReady();
bool speakerBusy();

// Stream 16 kHz, mono, signed 16-bit little-endian PCM from an HTTP TTS gateway.
// The amplifier outputs a bounded, conservative version of the PCM stream.
bool speakerSpeakPcm16Url(const String& url, const String& token = "");
