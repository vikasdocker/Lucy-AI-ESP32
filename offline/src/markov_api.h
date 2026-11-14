#pragma once

// Self-learning word generator for Lucy Offline OS.
// No training phase: a hashed character Markov table (order 2) starts from a
// small built-in seed text and learns from every message you send it.
// The brain persists to SPIFFS ("/lucy/brain.bin") and can be inspected from the
// serial console ("brain stats", "brain save", "brain reset").
void markovBegin();
void markovLearn(const char *text);
const char *markovGenerate();      // static buffer, printable ASCII, ~60 chars
void markovTick(unsigned long now);
void markovSaveNow();
void markovPrintStats();
void markovReset();
