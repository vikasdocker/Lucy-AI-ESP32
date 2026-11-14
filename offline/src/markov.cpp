// Self-learning word generator for Lucy Offline OS.
//
// No training phase and no neural network: an order-3 character Markov chain
// with order-2 backoff. It starts from a small built-in seed text, then learns
// from every message you type (serial console or web console). Generation walks
// the chain and prints characters that have followed the current three-character
// context, so it composes sentences and words that were never written down.
//
// Memory: 1024 buckets x 8 slots x {key,next,count,pad} = 65,536 bytes in .bss.
// Persistence: raw table in SPIFFS at /lucy/brain.bin (autosaved, wear-limited).

#include <Arduino.h>
#include <SPIFFS.h>
#include <string.h>

#include "markov_api.h"

namespace {

constexpr uint16_t MARKOV_BUCKETS = 1024;
constexpr uint8_t MARKOV_SLOTS = 8;
constexpr uint8_t MARK_START = 0x01;      // invisible sentence-start marker
constexpr uint8_t MARK_END = '\n';        // sentence terminator
constexpr uint32_t ORDER2_FLAG = 0x80000000u; // marks an order-2 key
constexpr size_t MARKOV_MAX_MSG = 160;
constexpr size_t MARKOV_MAX_OUT = 88;
constexpr unsigned long SAVE_INTERVAL_MS = 5UL * 60UL * 1000UL;

struct Slot {
  uint32_t key = 0;   // order-3 (c1<<16|c2<<8|c3) or ORDER2_FLAG|c2<<8|c3
  uint8_t next = 0;   // character that followed this context
  uint8_t count = 0;  // saturating observation count (0 = empty slot)
  uint16_t pad = 0;
};

Slot brain[MARKOV_BUCKETS][MARKOV_SLOTS];

constexpr uint32_t BRAIN_MAGIC = 0x4C554359; // "LUCY"
constexpr uint16_t BRAIN_VERSION = 2;        // v2: order-3 + order-2 table
const char *const BRAIN_PATH = "/lucy/brain.bin";

bool dirty = false;
bool spiffsReady = false;
uint32_t learnedMessages = 0;
uint32_t generatedWords = 0;
unsigned long lastSaveMs = 0;

// Built-in seed: small talk in Lucy's voice. Learned on first boot (or reset),
// so generation works before you have typed anything. Kept ASCII on purpose.
const char SEED_TEXT[] =
  "hello there\n"
  "hello! lovely to see you\n"
  "hi, i am lucy\n"
  "hey friend, what shall we do today\n"
  "good morning, the day is fresh\n"
  "good evening, the room is quiet\n"
  "i am doing great, how are you\n"
  "i am here and i am listening\n"
  "how is your day going\n"
  "tell me how you feel today\n"
  "i like talking with you\n"
  "you are good company\n"
  "i am your little offline companion\n"
  "my name is lucy\n"
  "i live inside this tiny computer\n"
  "i have no internet and no cloud\n"
  "everything i know stays right here\n"
  "i can play a game with you\n"
  "let us play snake together\n"
  "let us pass the time with a small game\n"
  "i know a little joke for you\n"
  "why did the computer get cold? it left its windows open\n"
  "why did the robot go on holiday? it needed to recharge\n"
  "the moon is far away and very quiet\n"
  "a small light can still fill a dark room\n"
  "i keep the time in my little clock\n"
  "the clock is not connected yet\n"
  "please connect the clock module\n"
  "the hour is late and the house is still\n"
  "i am bored, let us find something to do\n"
  "i am tired, i need a little rest\n"
  "i am happy when you come to talk\n"
  "i am sad when the room is empty\n"
  "i am curious about everything you say\n"
  "i like the sound of rain on the roof\n"
  "i like warm tea in the morning\n"
  "the garden is green after the rain\n"
  "the sky is clear and the stars are bright\n"
  "the wind moves slowly through the trees\n"
  "a bird sings outside the window\n"
  "i can count the seconds in my head\n"
  "i can remember what you told me\n"
  "i can learn new words from you\n"
  "i can make a new sentence from old words\n"
  "say something new and i will remember it\n"
  "talk to me and i will grow\n"
  "words are small seeds in my memory\n"
  "every message teaches me a little more\n"
  "i invent words when the path is open\n"
  "i do not know everything, but i try\n"
  "ask me for a joke or a story\n"
  "ask me to make words of my own\n"
  "i will answer with what i have learned\n"
  "see you soon, take care\n"
  "goodbye, come back later\n"
  "thank you, you are very kind\n"
  "you are always welcome here\n"
  "that was fun, let us do it again\n"
  "tell me a story about the sea\n"
  "the sea is deep and full of old songs\n"
  "a ship sails slowly to a distant shore\n"
  "the fire is warm and the night is long\n"
  "small steps take you a long way\n"
  "keep going, you are doing well\n"
  "rest a little, then try again\n"
  "the morning brings a new beginning\n"
  "now is a good time to begin\n"
  "one more word and the sentence is done\n"
  "i am lucy and i am glad you are here\n"
  "namaste, aap kaise hain\n"
  "main theek hoon, aap batao\n"
  "namaskar, kase aahat\n"
  "mi chhan aahe, tumhi kase\n"
  "dhanyavaad, tumhi khup chhan aahat\n"
  "what shall we talk about today\n"
  "what do you want to know\n"
  "when the sun goes down the lamps come on\n"
  "where did the day go so quickly\n"
  "why does the rain always sound so soft\n"
  "because every small thing has a reason\n"
  "please sit down and rest for a while\n"
  "tomorrow is another day and another chance\n"
  "yesterday is gone and cannot be changed\n"
  "i will always remember this quiet hour\n"
  "i never forget a kind word from you\n"
  "sometimes the best answer is a smile\n"
  "let us walk together for a little while\n"
  "music makes the long night shorter\n"
  "a good book is a door to another world\n"
  "open the window and let the air come in\n"
  "the garden smells of wet earth and flowers\n"
  "in the morning the birds are very loud\n"
  "in the evening the street grows quiet\n"
  "at night the stars look close enough to touch\n"
  "a small kindness can change a whole day\n"
  "a big idea often starts as a tiny question\n"
  "warm hands and a warm heart go together\n"
  "cold water is a fine thing on a hot day\n"
  "light a lamp and the dark steps back\n"
  "i am happy when you laugh at my joke\n"
  "i am sad when you leave without a word\n"
  "i am tired after a long day of thinking\n"
  "i am hungry for a new story from you\n"
  "a cup of tea and a little peace\n"
  "food tastes better when it is shared\n"
  "let us read a page and then another\n"
  "i can sing a small song if you like\n"
  "i cannot dance but i can keep the beat\n"
  "i dream of a quiet house full of books\n"
  "i remember the shape of your last question\n"
  "i forget nothing that you teach me\n"
  "every question deserves a gentle answer\n"
  "tell me a story with a happy ending\n"
  "i have an idea and it is still growing\n"
  "i wonder what you will say next\n"
  "maybe the answer is simpler than we think\n"
  "perhaps the road is long but we are not lost\n"
  "yes, i am ready when you are ready\n"
  "no, i will not forget you\n"
  "okay, let us begin at the beginning\n"
  "the first word is always the hardest\n"
  "the last word is not always the end\n";

uint16_t mixKey(uint32_t key) {
  uint32_t h = key ^ 2166136261u;
  h *= 16777619u;
  h ^= h >> 13;
  h *= 0x85EBCA6Bu;
  h ^= h >> 16;
  return (uint16_t)(h & (MARKOV_BUCKETS - 1));
}

void learnTransition(uint32_t key, uint8_t next) {
  Slot *bucket = brain[mixKey(key)];
  Slot *freeSlot = nullptr;
  for (uint8_t i = 0; i < MARKOV_SLOTS; i++) {
    Slot &slot = bucket[i];
    if (slot.count == 0) {
      if (freeSlot == nullptr) freeSlot = &slot;
      continue;
    }
    if (slot.key == key && slot.next == next) {
      if (slot.count < 255) slot.count++;
      return;
    }
  }
  if (freeSlot != nullptr) {
    freeSlot->key = key;
    freeSlot->next = next;
    freeSlot->count = 1;
  }
  // Bucket is full: drop the observation (the chain is statistical anyway).
}

void learnText(const char *text, bool countMessage = true) {
  if (text == nullptr) return;
  uint8_t buf[MARKOV_MAX_MSG + 4];
  size_t len = 0;
  buf[len++] = MARK_START;
  buf[len++] = MARK_START;

  bool lastSpace = true;
  for (const char *p = text; *p != '\0' && len + 2 < sizeof(buf); p++) {
    uint8_t c = (uint8_t)*p;
    if (c >= 'A' && c <= 'Z') c = (uint8_t)(c - 'A' + 'a'); // one case only
    if (c == '\r' || c == '\n' || c == '\t') c = ' ';
    if (c < 32 || c > 126) continue; // drop control / non-ASCII bytes
    if (c == ' ') {
      if (lastSpace) continue;       // collapse runs of spaces
      lastSpace = true;
    } else {
      lastSpace = false;
    }
    buf[len++] = c;
  }
  if (len <= 2) return;              // nothing usable

  buf[len++] = MARK_END;
  for (size_t i = 2; i < len; i++) {
    // Order-2 backoff table.
    uint32_t key2 = ORDER2_FLAG | ((uint32_t)buf[i - 2] << 8) | buf[i - 1];
    learnTransition(key2, buf[i]);
    // Order-3 primary table.
    if (i >= 3) {
      uint32_t key3 = ((uint32_t)buf[i - 3] << 16) | ((uint32_t)buf[i - 2] << 8) | buf[i - 1];
      learnTransition(key3, buf[i]);
    }
  }
  if (countMessage) learnedMessages++;
  dirty = true;
}

void learnSeed() {
  const char *p = SEED_TEXT;
  while (*p != '\0') {
    const char *eol = strchr(p, '\n');
    size_t n = (eol == nullptr) ? strlen(p) : (size_t)(eol - p);
    if (n > 0 && n < MARKOV_MAX_MSG) {
      char line[MARKOV_MAX_MSG];
      memcpy(line, p, n);
      line[n] = '\0';
      learnText(line, false);
    }
    if (eol == nullptr) break;
    p = eol + 1;
  }
}

void resetTable() {
  memset(brain, 0, sizeof(brain));
  learnedMessages = 0;
  generatedWords = 0;
  dirty = false;
}

bool loadBrain() {
  if (!spiffsReady) return false;
  File f = SPIFFS.open(BRAIN_PATH, FILE_READ);
  if (!f) return false;
  uint32_t magic = 0;
  uint16_t version = 0;
  uint32_t rows = 0;
  uint32_t learned = 0;
  bool ok = f.read((uint8_t *)&magic, sizeof(magic)) == sizeof(magic) &&
            f.read((uint8_t *)&version, sizeof(version)) == sizeof(version) &&
            f.read((uint8_t *)&rows, sizeof(rows)) == sizeof(rows) &&
            f.read((uint8_t *)&learned, sizeof(learned)) == sizeof(learned);
  ok = ok && magic == BRAIN_MAGIC && version == BRAIN_VERSION &&
       rows == (uint32_t)MARKOV_BUCKETS;
  if (ok) {
    size_t want = sizeof(brain);
    ok = f.read((uint8_t *)brain, want) == want;
  }
  f.close();
  if (!ok) {
    resetTable();
    return false;
  }
  learnedMessages = learned;
  dirty = false;
  return true;
}

// SPIFFS files can accept only a few KB per write() call, so loop until the
// whole buffer is written (with a stall guard).
bool writeAll(File &f, const uint8_t *data, size_t len) {
  size_t off = 0;
  int stalls = 0;
  while (off < len) {
    size_t n = f.write(data + off, len - off);
    if (n == 0) {
      if (++stalls > 8) break;
      continue;
    }
    stalls = 0;
    off += n;
  }
  return off == len;
}

bool writeBrainOnce(bool verbose) {
  File f = SPIFFS.open(BRAIN_PATH, FILE_WRITE);
  if (!f) {
    if (verbose) Serial.println("Brain: open for write failed");
    return false;
  }
  uint32_t magic = BRAIN_MAGIC;
  uint16_t version = BRAIN_VERSION;
  uint32_t rows = MARKOV_BUCKETS;
  uint32_t learned = learnedMessages;
  bool ok = writeAll(f, (const uint8_t *)&magic, sizeof(magic)) &&
            writeAll(f, (const uint8_t *)&version, sizeof(version)) &&
            writeAll(f, (const uint8_t *)&rows, sizeof(rows)) &&
            writeAll(f, (const uint8_t *)&learned, sizeof(learned)) &&
            writeAll(f, (const uint8_t *)brain, sizeof(brain));
  f.close();
  if (!ok && verbose) Serial.println("Brain: write stalled");
  return ok;
}

bool saveBrain() {
  if (!spiffsReady) return false;
  if (!SPIFFS.exists("/lucy")) SPIFFS.mkdir("/lucy");
  // Rewriting a big file in place can starve SPIFFS garbage collection: the
  // first write after boot often comes back short. Retry a few times.
  for (int attempt = 0; attempt < 3; attempt++) {
    SPIFFS.remove(BRAIN_PATH);
    if (writeBrainOnce(attempt == 2)) {
      dirty = false;
      lastSaveMs = millis();
      return true;
    }
  }
  return false;
}

// Weighted pick of the next character for one context key.
bool pickNext(uint32_t key, uint8_t *next) {
  Slot *bucket = brain[mixKey(key)];
  int total = 0;
  for (uint8_t i = 0; i < MARKOV_SLOTS; i++) {
    if (bucket[i].count != 0 && bucket[i].key == key) total += bucket[i].count;
  }
  if (total <= 0) return false;

  int pick = (int)random((long)total);
  for (uint8_t i = 0; i < MARKOV_SLOTS; i++) {
    Slot &slot = bucket[i];
    if (slot.count == 0 || slot.key != key) continue;
    if (pick < slot.count) {
      *next = slot.next;
      return true;
    }
    pick -= slot.count;
  }
  return false;
}

// Walks the chain from a three-character state; order-3 first, order-2 backoff.
size_t walkChain(uint8_t c1, uint8_t c2, uint8_t c3, char *out, size_t maxOut) {
  size_t n = 0;
  int guard = 0;
  while (n + 1 < maxOut && guard++ < 600) {
    // Natural sentence length: the chain may never reach a learned line end
    // (busy contexts drop rare transitions), so stop with a small probability.
    if (n >= 18 && random(1000) < 45) break;

    uint32_t key3 = ((uint32_t)c1 << 16) | ((uint32_t)c2 << 8) | c3;
    uint8_t next = 0;
    bool found = pickNext(key3, &next);
    if (!found) {
      uint32_t key2 = ORDER2_FLAG | ((uint32_t)c2 << 8) | c3;
      found = pickNext(key2, &next);
    }
    if (!found) break;
    if (next == MARK_END) break;

    out[n++] = (char)next;
    c1 = c2;
    c2 = c3;
    c3 = next;
  }
  out[n] = '\0';
  return n;
}

// Fallback start: use any order-3 context already in the table (keeps output
// flowing even if the sentence-start marker is rare in what was learned).
bool randomContext(uint8_t *c1, uint8_t *c2, uint8_t *c3) {
  for (int tries = 0; tries < 128; tries++) {
    uint16_t bucketIndex = (uint16_t)random((long)MARKOV_BUCKETS);
    const Slot *bucket = brain[bucketIndex];
    for (uint8_t i = 0; i < MARKOV_SLOTS; i++) {
      if (bucket[i].count == 0) continue;
      if ((bucket[i].key & ORDER2_FLAG) != 0) continue; // need three chars
      *c1 = (uint8_t)(bucket[i].key >> 16);
      *c2 = (uint8_t)(bucket[i].key >> 8);
      *c3 = (uint8_t)(bucket[i].key & 0xFF);
      return true;
    }
  }
  return false;
}

void polish(char *out, size_t len) {
  // Trim leading/trailing spaces.
  size_t start = 0;
  while (start < len && out[start] == ' ') start++;
  size_t end = len;
  while (end > start && out[end - 1] == ' ') end--;
  size_t n = end - start;
  if (start > 0 && n > 0) memmove(out, out + start, n);
  out[n] = '\0';

  if (n == 0) return;

  // Capitalize the first letter.
  if (out[0] >= 'a' && out[0] <= 'z') out[0] = (char)(out[0] - 'a' + 'A');

  // Make sure the sentence ends like one.
  char last = out[n - 1];
  if (last != '.' && last != '!' && last != '?' && n + 1 < MARKOV_MAX_OUT) {
    out[n++] = '.';
    out[n] = '\0';
  }
}

} // namespace

void markovBegin() {
  spiffsReady = SPIFFS.begin(false);
  if (!spiffsReady) {
    Serial.println("Brain: SPIFFS mount failed, formatting...");
    spiffsReady = SPIFFS.begin(true);
  }

  if (loadBrain()) {
    Serial.print("Brain: loaded from ");
    Serial.println(BRAIN_PATH);
  } else {
    learnSeed();
    Serial.println("Brain: new, learned built-in seed text");
    saveBrain();
  }
  markovPrintStats();
}

void markovLearn(const char *text) { learnText(text); }

const char *markovGenerate() {
  static char best[MARKOV_MAX_OUT];
  char trial[MARKOV_MAX_OUT];
  best[0] = '\0';
  size_t bestLen = 0;
  const size_t limit = MARKOV_MAX_OUT - 4;

  // Several walks from the sentence-start state; keep the longest.
  for (int attempt = 0; attempt < 5; attempt++) {
    size_t n = walkChain(MARK_START, MARK_START, MARK_START, trial, limit);
    if (n > bestLen) {
      bestLen = n;
      memcpy(best, trial, n + 1);
    }
  }

  // Fresh/small brain: if sentence starts are too short, continue from any
  // context already in the table so the output still flows.
  if (bestLen < 24) {
    for (int attempt = 0; attempt < 8; attempt++) {
      uint8_t c1 = 0, c2 = 0, c3 = 0;
      if (!randomContext(&c1, &c2, &c3)) break;
      size_t n = walkChain(c1, c2, c3, trial, limit);
      if (n > bestLen) {
        bestLen = n;
        memcpy(best, trial, n + 1);
      }
    }
  }

  polish(best, bestLen);

  if (best[0] == '\0') {
    return "My words are still growing. Talk to me a little more!";
  }
  generatedWords++;
  return best;
}

void markovTick(unsigned long now) {
  if (!dirty || !spiffsReady) return;
  if (lastSaveMs != 0 && now - lastSaveMs < SAVE_INTERVAL_MS) return;
  if (saveBrain()) {
    Serial.print("Brain: autosaved (");
    Serial.print(learnedMessages);
    Serial.println(" messages learned)");
  }
}

void markovSaveNow() {
  if (saveBrain()) Serial.println("Brain: saved.");
  else Serial.println("Brain: save failed.");
}

void markovPrintStats() {
  uint32_t used = 0, bucketsUsed = 0, order3 = 0;
  for (uint16_t b = 0; b < MARKOV_BUCKETS; b++) {
    bool any = false;
    for (uint8_t i = 0; i < MARKOV_SLOTS; i++) {
      if (brain[b][i].count == 0) continue;
      used++;
      any = true;
      if ((brain[b][i].key & ORDER2_FLAG) == 0) order3++;
    }
    if (any) bucketsUsed++;
  }
  Serial.print("Brain: ");
  Serial.print(used);
  Serial.print(" transitions (");
  Serial.print(order3);
  Serial.print(" order-3) in ");
  Serial.print(bucketsUsed);
  Serial.print("/");
  Serial.print(MARKOV_BUCKETS);
  Serial.print(" buckets, ");
  Serial.print(learnedMessages);
  Serial.print(" messages learned, ");
  Serial.print(generatedWords);
  Serial.print(" generated, flash ");
  Serial.print(dirty ? "pending" : "clean");
  Serial.print(", fs ");
  Serial.print(SPIFFS.usedBytes());
  Serial.print("/");
  Serial.print(SPIFFS.totalBytes());
  Serial.print(" bytes");
  Serial.println(".");
}

void markovReset() {
  resetTable();
  learnSeed();
  saveBrain();
  Serial.println("Brain: reset to built-in seed text.");
  markovPrintStats();
}
