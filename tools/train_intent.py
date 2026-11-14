#!/usr/bin/env python3
"""Train Lucy's tiny intent model (offline, classic ESP32 friendly).

Model: character n-gram hashing (256 bins) -> dense 64 (ReLU) -> 9 intents.
Weights are exported int8 into offline/src/intent_model.h; biases stay float32.
Inference is hand-rolled in offline/src/intent_model.cpp (no TFLite dependency,
same int8 math, ~16 KB of weights in flash).

Run:  python tools/train_intent.py
"""
import math
import random
import struct
from pathlib import Path

import numpy as np

BINS = 256
HIDDEN = 64
INTENTS = [
    "greeting",
    "howareyou",
    "bored",
    "joke",
    "time",
    "name",
    "thanks",
    "bye",
    "other",
]
NCLASS = len(INTENTS)

# ---------------------------------------------------------------- dataset ----
SEEDS = {
    "greeting": [
        "hello", "hi", "hey", "namaste", "namaskar", "hello lucy", "hi lucy",
        "hey lucy", "hello there", "hi there", "good morning", "good evening",
        "good afternoon", "good morning lucy", "good night lucy", "yo",
        "hey buddy", "hi buddy", "hello friend", "hi friend", "hii", "helloo",
        "namaste lucy", "namaskar lucy", "are you there", "lucy hello",
        "hello how are you", "hey there", "whats up", "howdy",
    ],
    "howareyou": [
        "how are you", "how are you doing", "how do you do", "how is it going",
        "how are things", "how are you today", "are you okay", "are you fine",
        "how are you feeling", "kaise ho", "kasa ahes", "kase aahat",
        "kaise hain aap", "how is your day", "how you doing", "how r u",
        "whats up with you", "everything good", "are you well", "hope you are well",
        "how have you been", "you okay", "you good", "kya haal hai", "kay challay",
        "how is life", "how are you my friend",
    ],
    "bored": [
        "i am bored", "im bored", "i am so bored", "bored", "so boring",
        "this is boring", "i feel bored", "nothing to do", "i have nothing to do",
        "i am getting bored", "boring day", "im so bored", "i am borred",
        "kantaala", "kanthala", "i am bored lucy", "entertain me", "im bored help",
        "feeling bored", "this day is boring", "i am bored at home", "boredom",
        "help me pass time", "i am tired of nothing", "sitting idle and bored",
    ],
    "joke": [
        "tell me a joke", "joke", "say something funny", "make me laugh",
        "do you know any jokes", "tell a joke lucy", "another joke",
        "funny joke", "crack a joke", "tell me something funny", "any jokes",
        "make me smile", "say a joke", "one more joke", "got any jokes",
        "make me laugh please", "tell me a funny joke", "jokes",
        "do you have jokes", "something funny please", "make me happy with a joke",
        "tell me a good joke", "give me a joke", "joke please",
    ],
    "time": [
        "what time is it", "time", "current time", "tell me the time",
        "whats the time", "what is the time", "time please", "whats the current time",
        "do you know the time", "check the time", "what time", "samay kya hua",
        "kitne baje hain", "tell the time", "time now", "what is current time",
        "whats the clock", "clock time please", "how late is it", "timing",
        "what time is it now", "current time please",
    ],
    "name": [
        "what is your name", "whats your name", "your name", "name",
        "who are you", "what are you called", "tell me your name",
        "do you have a name", "what should i call you", "introduce yourself",
        "who am i talking to", "your good name", "aapka naam kya hai",
        "tumcha naav kay aahe", "whats your good name", "may i know your name",
        "name please", "who is this", "identify yourself", "whats your identity",
    ],
    "thanks": [
        "thank you", "thanks", "thank you so much", "thanks a lot", "thx",
        "thank u", "thanks lucy", "many thanks", "thankyou", "dhanyavaad",
        "dhanyawad", "thank you very much", "much appreciated", "thanks buddy",
        "thank you friend", "great thanks", "ok thanks", "thanks for helping",
        "thank you for that", "you are helpful thanks",
    ],
    "bye": [
        "bye", "goodbye", "see you", "see you soon", "ok bye", "bye bye",
        "talk to you later", "catch you later", "i am leaving", "good night",
        "goodbye lucy", "bye lucy", "see ya", "later", "take care", "exit",
        "alvida", "chalo bye", "i will go now", "meeting you later",
        "good bye", "bye for now", "ttyl",
    ],
    "other": [
        "what is the weather", "will it rain today", "what is 2 plus 2",
        "how far is the moon", "tell me a story", "what is your favourite colour",
        "how do i cook rice", "what is the capital of india", "who is the president",
        "what is love", "explain physics", "translate this to english",
        "how do i learn coding", "what is a microcontroller", "how old are you",
        "can you sing", "play music", "what is the news", "open the door",
        "set an alarm", "call my mother", "send a message", "what is ai",
        "do you sleep", "are you human", "what is the meaning of life",
        "how much do you cost", "where do you live", "can you drive",
        "remind me tomorrow", "what is quantum computing", "help me debug python",
        "what is the temperature outside", "who made you", "are you real",
        "what is your favourite food", "do you eat", "how tall are you",
        "what is the time in london", "convert 5 km to miles", "spell banana",
        "what is the fastest car", "how do planes fly", "what is water",
        "tell me about space", "what is the biggest animal", "who won the match",
        "how to grow a plant", "what is a virus", "is the earth round",
        "sometimes i wonder", "i wonder why", "let me think about it",
        "just wondering", "i have a question", "what do you think",
        "anything interesting", "keep me company", "tell me something new",
        "what can you do", "help me decide", "think about it",
    ],
}


def expand(variants):
    """Light template expansion for robustness."""
    out = []
    for v in variants:
        out.append(v)
        out.append(v + "?")
        out.append(v + " please")
        out.append(v + " lucy")
        out.append("lucy " + v)
    return out


def dataset():
    rows = []
    for idx, name in enumerate(INTENTS):
        phrases = expand(SEEDS[name]) if name != "other" else SEEDS[name]
        for p in phrases:
            rows.append((p, idx))
    random.Random(42).shuffle(rows)
    return rows


# --------------------------------------------------------------- features ----
def fnv1a(data: bytes) -> int:
    h = 2166136261
    for b in data:
        h ^= b
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def featurize(text: str) -> np.ndarray:
    # Match the firmware exactly: ASCII lowercase, raw bytes, bigrams+trigrams
    # hashed into BINS, counts clipped to 2.
    raw = text.lower().encode("utf-8", "replace")
    feat = np.zeros(BINS, dtype=np.float32)
    n = len(raw)
    if n >= 2:
        for i in range(n - 1):
            feat[fnv1a(raw[i:i + 2]) & (BINS - 1)] += 1
        for i in range(n - 2):
            feat[fnv1a(raw[i:i + 3]) & (BINS - 1)] += 1
    elif n == 1:
        feat[fnv1a(raw) & (BINS - 1)] = 1
    np.clip(feat, 0, 2, out=feat)
    return feat / 2.0  # 0, 0.5, 1.0


# --------------------------------------------------------------- training ----
def train(rows, epochs=900, lr=0.15, seed=7):
    rng = np.random.default_rng(seed)
    X = np.stack([featurize(t) for t, _ in rows])
    y = np.array([c for _, c in rows])

    n = len(rows)
    perm = rng.permutation(n)
    split = int(n * 0.85)
    tr, va = perm[:split], perm[split:]

    W1 = rng.normal(0, 0.3, size=(BINS, HIDDEN)).astype(np.float64)
    b1 = np.zeros(HIDDEN)
    W2 = rng.normal(0, 0.3, size=(HIDDEN, NCLASS)).astype(np.float64)
    b2 = np.zeros(NCLASS)

    def forward(xb):
        h = np.maximum(0.0, xb @ W1 + b1)
        z = h @ W2 + b2
        z -= z.max(axis=1, keepdims=True)
        e = np.exp(z)
        return h, e / e.sum(axis=1, keepdims=True)

    best = (0.0, W1.copy(), b1.copy(), W2.copy(), b2.copy())
    bs = 64
    for ep in range(epochs):
        order = rng.permutation(len(tr))
        for s in range(0, len(order), bs):
            idx = tr[order[s:s + bs]]
            xb, yb = X[idx], y[idx]
            h, p = forward(xb)
            g = p.copy()
            g[np.arange(len(yb)), yb] -= 1.0
            g /= len(yb)
            dW2 = h.T @ g
            db2 = g.sum(axis=0)
            dh = (g @ W2.T) * (h > 0)
            dW1 = xb.T @ dh
            db1 = dh.sum(axis=0)
            W2 -= lr * dW2
            b2 -= lr * db2
            W1 -= lr * dW1
            b1 -= lr * db1
        if ep % 25 == 0 or ep == epochs - 1:
            _, pv = forward(X[va])
            acc = (pv.argmax(1) == y[va]).mean()
            if acc > best[0]:
                best = (acc, W1.copy(), b1.copy(), W2.copy(), b2.copy())
    acc, W1, b1, W2, b2 = best

    _, pt = forward(X[tr])
    print(f"train acc {100*(pt.argmax(1)==y[tr]).mean():.1f}%  val acc {100*acc:.1f}%  "
          f"({len(tr)} train / {len(va)} val)")
    return W1, b1, W2, b2, X, y, va


def export_cpp(W1, b1, W2, b2, X, y, va, out_path: Path):
    def q(arr):
        scale = float(np.abs(arr).max()) / 127.0 or 1.0
        return np.clip(np.rint(arr / scale), -127, 127).astype(np.int8), scale

    W1q, s1 = q(W1)
    W2q, s2 = q(W2)

    def c_array(name, data, ctype, per_line=30):
        flat = data.flatten().tolist()
        lines = []
        for i in range(0, len(flat), per_line):
            lines.append("  " + ", ".join(str(v) for v in flat[i:i + per_line]) + ",")
        return f"static const {ctype} {name}[{len(flat)}] = {{\n" + "\n".join(lines) + "\n};\n"

    out = []
    out.append("// Generated by tools/train_intent.py - do not edit by hand.")
    out.append(f"// intents: {', '.join(INTENTS)}")
    out.append("#pragma once")
    out.append("#include <stdint.h>")
    out.append("")
    out.append(f"static constexpr int INTENT_CLASS_COUNT = {NCLASS};")
    out.append(f"static constexpr int INTENT_FEATURE_COUNT = {BINS};")
    out.append(f"static constexpr int INTENT_HIDDEN = {HIDDEN};")
    out.append("")
    out.append(c_array("INTENT_W1", W1q.T, "int8_t"))      # [HIDDEN][BINS]
    out.append(c_array("INTENT_B1", b1.astype(np.float32), "float"))
    out.append(c_array("INTENT_W2", W2q.T, "int8_t"))      # [NCLASS][HIDDEN]
    out.append(c_array("INTENT_B2", b2.astype(np.float32), "float"))
    out.append(f"static constexpr float INTENT_W1_SCALE = {s1:.8f}f;")
    out.append(f"static constexpr float INTENT_W2_SCALE = {s2:.8f}f;")
    out.append("")
    out_path.write_text("\n".join(out), encoding="utf-8")
    print(f"wrote {out_path} ({out_path.stat().st_size} bytes, "
          f"weights {W1q.size + W2q.size} int8)")

    # Simulate the exact int8 math of offline/src/intent_model.cpp (layout check).
    W1t, W2t = W1q.T.astype(np.float64), W2q.T.astype(np.float64)
    feat_counts = np.rint(X * 2).astype(np.int64)          # 0, 1, 2
    h = np.maximum(0.0, (feat_counts @ W1t.T) * (s1 * 0.5) + b1)
    z = (h @ W2t.T) * s2 + b2
    acc = (z.argmax(1) == y).mean()
    print(f"int8 C++-simulated accuracy: {100*acc:.1f}%")


def main():
    rows = dataset()
    W1, b1, W2, b2, X, y, va = train(rows)
    repo = Path(__file__).resolve().parent.parent
    export_cpp(W1, b1, W2, b2, X, y, va, repo / "offline" / "src" / "intent_model.h")


if __name__ == "__main__":
    main()
