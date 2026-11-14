// Tiny intent classifier inference for Lucy Offline OS.
// Model: hashed char bigrams+trigrams (256 bins) -> 64 ReLU -> 9 intents.
// Generated weights live in intent_model.h (see tools/train_intent.py).
// Int8 weights, float32 biases, int32 accumulation: ~17k MACs per call.

#include <Arduino.h>
#include <ctype.h>
#include <math.h>
#include <string.h>

#include "intent_model.h"
#include "intent_api.h"

namespace {

uint32_t fnv1a(const uint8_t *data, size_t len) {
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < len; i++) {
    h ^= data[i];
    h *= 16777619u;
  }
  return h;
}

// Mirrors tools/train_intent.py:featurize()
void featurize(const char *text, uint8_t *feat) {
  memset(feat, 0, INTENT_FEATURE_COUNT);
  char lower[96];
  size_t n = strlen(text);
  if (n >= sizeof(lower)) n = sizeof(lower) - 1;
  for (size_t i = 0; i < n; i++) lower[i] = (char)tolower((unsigned char)text[i]);
  lower[n] = '\0';
  const uint8_t *raw = (const uint8_t *)lower;

  auto add = [&](uint32_t bin) {
    uint8_t &v = feat[bin & (INTENT_FEATURE_COUNT - 1)];
    if (v < 2) v++;
  };

  if (n >= 2) {
    for (size_t i = 0; i + 1 < n; i++) add(fnv1a(raw + i, 2));
    for (size_t i = 0; i + 2 < n; i++) add(fnv1a(raw + i, 3));
  } else if (n == 1) {
    add(fnv1a(raw, 1));
  }
}

} // namespace

int intentModelPredict(const char *text, float *confidenceOut) {
  uint8_t feat[INTENT_FEATURE_COUNT];
  featurize(text, feat);

  float hidden[INTENT_HIDDEN];
  const float w1Scale = INTENT_W1_SCALE * 0.5f; // features are feat/2
  for (int h = 0; h < INTENT_HIDDEN; h++) {
    int32_t acc = 0;
    const int8_t *row = INTENT_W1 + (size_t)h * INTENT_FEATURE_COUNT;
    for (int f = 0; f < INTENT_FEATURE_COUNT; f++) {
      acc += (int32_t)feat[f] * row[f];
    }
    float v = (float)acc * w1Scale + INTENT_B1[h];
    hidden[h] = v > 0.0f ? v : 0.0f;
  }

  float logits[INTENT_CLASS_COUNT];
  float maxLogit = -1e9f;
  for (int c = 0; c < INTENT_CLASS_COUNT; c++) {
    int32_t acc = 0;
    const int8_t *row = INTENT_W2 + (size_t)c * INTENT_HIDDEN;
    for (int h = 0; h < INTENT_HIDDEN; h++) {
      acc += (int32_t)hidden[h] * row[h];
    }
    logits[c] = (float)acc * INTENT_W2_SCALE + INTENT_B2[c];
    if (logits[c] > maxLogit) maxLogit = logits[c];
  }

  float sum = 0.0f;
  int best = 0;
  float bestScore = -1.0f;
  for (int c = 0; c < INTENT_CLASS_COUNT; c++) {
    float p = expf(logits[c] - maxLogit);
    sum += p;
    if (p > bestScore) {
      bestScore = p;
      best = c;
    }
  }
  if (confidenceOut) *confidenceOut = bestScore / sum;
  return best;
}
