#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include <ctype.h>
#include <string.h>
#include <WiFi.h>
#include <WebServer.h>

#include "ai_api.h"
#include "intent_api.h"
#include "markov_api.h"

// Lucy Offline OS v0.1 — classic ESP32 DevKit V1, SSD1306 128x64.
// Fork addition: WiFi AP web console for headless testing (mirrors the screen,
// injects button presses, chats with the offline brain). No internet, no cloud, no STA.

static constexpr uint8_t PIN_BTN_UP = 16;
static constexpr uint8_t PIN_BTN_SELECT = 17;
static constexpr uint8_t PIN_BTN_BACK = 4;
static constexpr uint8_t PIN_SD_CS = 13; // avoids GPIO5 boot-strapping interaction
static constexpr uint32_t DEBOUNCE_MS = 35;
static constexpr uint32_t IDLE_RETURN_MS = 15000;

U8G2_SSD1306_128X64_NONAME_F_HW_I2C display(U8G2_R0, U8X8_PIN_NONE);
RTC_DS3231 rtc;
Preferences prefs;

enum class Screen : uint8_t { HOME, MENU, CLOCK, COMPANION, GAMES, SNAKE, SETTINGS, ABOUT };
enum class Language : uint8_t { ENGLISH, HINDI, MARATHI };

struct Button {
  uint8_t pin;
  bool stable = HIGH;
  bool lastRead = HIGH;
  uint32_t changedAt = 0;
  bool pressed = false;
};

Button btnUp{PIN_BTN_UP}, btnSelect{PIN_BTN_SELECT}, btnBack{PIN_BTN_BACK};
Screen screen = Screen::HOME;
Language language = Language::ENGLISH;
bool aiEnabled = false;
bool rtcReady = false;
bool sdReady = false;
bool soundEnabled = false;
uint8_t menuIndex = 0;
uint8_t gameIndex = 0;
uint32_t lastInteraction = 0;
uint32_t lastFrame = 0;
uint32_t lastClockSync = 0;
uint16_t snakeScore = 0;
uint8_t snakeX[48], snakeY[48], snakeLength = 3;
int8_t snakeDx = 1, snakeDy = 0;
uint8_t foodX = 8, foodY = 4;
uint32_t lastSnakeStep = 0;
bool snakeOver = false;
char serialLine[96];
size_t serialLength = 0;

// Optional station (home Wi-Fi / hotspot) credentials, stored in NVS.
String wifiSsid;
String wifiPass;
bool wifiWasConnected = false;
uint32_t wifiLastKick = 0;
uint32_t wifiLastCheck = 0;

static const char *menuItems[] = {
  "Companion", "Clock", "Games", "Settings", "About"
};
static constexpr uint8_t MENU_COUNT = sizeof(menuItems) / sizeof(menuItems[0]);

const char *languageName(Language value) {
  switch (value) {
    case Language::HINDI: return "Hindi";
    case Language::MARATHI: return "Marathi";
    default: return "English";
  }
}

void drawCentered(const char *text, uint8_t y) {
  int16_t x = (128 - display.getStrWidth(text)) / 2;
  if (x < 0) x = 0;
  display.drawStr(x, y, text);
}

void drawHeader(const char *title) {
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(2, 9, title);
  display.drawHLine(0, 12, 128);
}

void drawFooter(const char *text) {
  display.drawHLine(0, 54, 128);
  display.setFont(u8g2_font_5x8_tf);
  display.drawStr(2, 63, text);
}

void drawEyes(uint32_t now) {
  const bool blink = (now % 4700U) < 140U;
  const uint8_t eyeH = blink ? 3 : 18;
  display.drawRBox(31, 20 + (18 - eyeH) / 2, 25, eyeH, 7);
  display.drawRBox(72, 20 + (18 - eyeH) / 2, 25, eyeH, 7);
  if (!blink) {
    display.setDrawColor(0);
    display.drawDisc(43, 29, 3);
    display.drawDisc(84, 29, 3);
    display.setDrawColor(1);
  }
}

void saveSettings() {
  prefs.putUChar("lang", static_cast<uint8_t>(language));
  prefs.putBool("sound", soundEnabled);
  prefs.putBool("aiOn", aiEnabled);
  // aiHost/aiPort are kept in sync for older builds that still read them.
  prefs.putString("aiUrl", aiUrl());
  prefs.putString("aiHost", aiUrl());
  prefs.putUShort("aiPort", 0);
  prefs.putString("aiModel", aiModel());
  prefs.putString("aiTok", aiToken());
  prefs.putBool("aiIns", aiInsecure());
  prefs.putString("wifiSsid", wifiSsid);
  prefs.putString("wifiPass", wifiPass);
}

void loadSettings() {
  uint8_t stored = prefs.getUChar("lang", 0);
  if (stored <= 2) language = static_cast<Language>(stored);
  soundEnabled = prefs.getBool("sound", false);
  aiEnabled = prefs.getBool("aiOn", false);

  String url = prefs.getString("aiUrl", "");
  if (url.length() == 0) {
    // Migrate the older host/port settings.
    String host = prefs.getString("aiHost", "192.168.4.2");
    uint16_t port = prefs.getUShort("aiPort", 11434);
    aiSetEndpoint(host.c_str(), port == 0 ? 11434 : port);
  } else {
    aiSetUrl(url.c_str());
  }
  String model = prefs.getString("aiModel", "qwen2.5:7b");
  aiSetModel(model.c_str());
  aiSetToken(prefs.getString("aiTok", "").c_str());
  aiSetInsecureTls(prefs.getBool("aiIns", false));

  wifiSsid = prefs.getString("wifiSsid", "");
  wifiPass = prefs.getString("wifiPass", "");
}

void pollButton(Button &button) {
  bool reading = digitalRead(button.pin);
  button.pressed = false;
  if (reading != button.lastRead) {
    button.lastRead = reading;
    button.changedAt = millis();
  }
  if ((millis() - button.changedAt) >= DEBOUNCE_MS && reading != button.stable) {
    button.stable = reading;
    if (button.stable == LOW) {
      button.pressed = true;
      lastInteraction = millis();
    }
  }
}

bool take(Button &button) {
  bool value = button.pressed;
  button.pressed = false;
  return value;
}

void showHome() {
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(3, 9, "LUCY OS");
  display.drawHLine(0, 12, 128);
  drawEyes(millis());
  display.setFont(u8g2_font_5x8_tf);
  char status[32];
  snprintf(status, sizeof(status), "%s  %s", rtcReady ? "RTC" : "NO RTC", sdReady ? "SD OK" : "NO SD");
  display.drawStr(2, 48, status);
  display.drawStr(2, 62, "SELECT: Apps   UP: Chat");
  display.sendBuffer();
}

void showMenu() {
  display.clearBuffer();
  drawHeader("LUCY APPLICATIONS");
  display.setFont(u8g2_font_6x10_tf);
  for (uint8_t i = 0; i < 3; i++) {
    uint8_t item = (menuIndex / 3) * 3 + i;
    if (item >= MENU_COUNT) break;
    uint8_t y = 25 + i * 13;
    if (item == menuIndex) {
      display.drawBox(1, y - 9, 126, 12);
      display.setDrawColor(0);
    }
    display.drawStr(5, y, menuItems[item]);
    if (item == menuIndex) display.setDrawColor(1);
  }
  drawFooter("UP: Next  SELECT: Open  BACK: Home");
  display.sendBuffer();
}

void showClock() {
  display.clearBuffer();
  drawHeader("CLOCK & DATE");
  display.setFont(u8g2_font_logisoso20_tf);
  if (rtcReady) {
    DateTime now = rtc.now();
    char timeText[12];
    snprintf(timeText, sizeof(timeText), "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
    drawCentered(timeText, 38);
    display.setFont(u8g2_font_6x10_tf);
    char dateText[24];
    snprintf(dateText, sizeof(dateText), "%04d-%02d-%02d", now.year(), now.month(), now.day());
    drawCentered(dateText, 50);
  } else {
    drawCentered("RTC NOT FOUND", 34);
    display.setFont(u8g2_font_5x8_tf);
    drawCentered("Install DS3231 for offline time", 47);
  }
  drawFooter("BACK: Home");
  display.sendBuffer();
}

void showCompanion(const char *message = nullptr) {
  display.clearBuffer();
  drawHeader("LUCY COMPANION");
  display.setFont(u8g2_font_6x10_tf);
  if (message) {
    // Small screen: caller supplies short ASCII status text.
    display.drawUTF8(3, 28, message);
  } else {
    drawEyes(millis());
    drawCentered("Talk to me!", 49);
  }
  drawFooter("UP: Language  SELECT: Demo reply");
  display.sendBuffer();
}

void showSettings() {
  display.clearBuffer();
  drawHeader("SETTINGS");
  display.setFont(u8g2_font_6x10_tf);
  char line[32];
  snprintf(line, sizeof(line), "Language: %s", languageName(language));
  display.drawStr(4, 27, line);
  snprintf(line, sizeof(line), "Voice output: %s", soundEnabled ? "ON" : "OFF");
  display.drawStr(4, 40, line);
  drawFooter("UP: Change language  SELECT: Sound");
  display.sendBuffer();
}

void showAbout() {
  display.clearBuffer();
  drawHeader("ABOUT LUCY OS");
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(4, 27, "Offline Companion");
  display.drawStr(4, 39, "Build: 0.1.0");
  display.drawStr(4, 51, "ESP32 DevKit V1");
  drawFooter("BACK: Apps");
  display.sendBuffer();
}

void spawnFood() {
  foodX = random(1, 15);
  foodY = random(1, 6);
}

void startSnake() {
  snakeLength = 3;
  snakeX[0] = 7; snakeY[0] = 3;
  snakeX[1] = 6; snakeY[1] = 3;
  snakeX[2] = 5; snakeY[2] = 3;
  snakeDx = 1; snakeDy = 0;
  snakeScore = 0;
  snakeOver = false;
  spawnFood();
  screen = Screen::SNAKE;
  lastSnakeStep = millis();
}

void showSnake() {
  display.clearBuffer();
  drawHeader("SNAKE  UP: TURN");
  display.drawFrame(0, 14, 128, 39);
  display.drawBox(foodX * 8, 17 + foodY * 5, 4, 4);
  for (uint8_t i = 0; i < snakeLength; i++) {
    display.drawBox(snakeX[i] * 8, 17 + snakeY[i] * 5, 5, 4);
  }
  display.setFont(u8g2_font_5x8_tf);
  char score[20];
  snprintf(score, sizeof(score), "SCORE %u", snakeScore);
  display.drawStr(2, 63, snakeOver ? "GAME OVER - SELECT" : score);
  display.sendBuffer();
}

void updateSnake() {
  if (snakeOver || millis() - lastSnakeStep < 220) return;
  lastSnakeStep = millis();
  if (take(btnUp)) {
    int8_t oldDx = snakeDx;
    snakeDx = -snakeDy;
    snakeDy = oldDx;
  }
  if (take(btnSelect)) {
    int8_t oldDx = snakeDx;
    snakeDx = snakeDy;
    snakeDy = -oldDx;
  }
  int16_t nx = snakeX[0] + snakeDx;
  int16_t ny = snakeY[0] + snakeDy;
  if (nx < 1 || nx > 14 || ny < 0 || ny > 6) { snakeOver = true; return; }
  for (uint8_t i = 0; i < snakeLength; i++) {
    if (snakeX[i] == nx && snakeY[i] == ny) { snakeOver = true; return; }
  }
  for (int i = snakeLength - 1; i > 0; i--) {
    snakeX[i] = snakeX[i - 1];
    snakeY[i] = snakeY[i - 1];
  }
  snakeX[0] = static_cast<uint8_t>(nx);
  snakeY[0] = static_cast<uint8_t>(ny);
  if (snakeX[0] == foodX && snakeY[0] == foodY) {
    if (snakeLength < 48) snakeLength++;
    snakeScore += 10;
    spawnFood();
  }
}

// Whole-word match so short keywords ("hi") do not fire inside words
// like "nothing"/"something", and "time" does not match "sometimes".
static bool textHasWord(const char *text, const char *word) {
  size_t wl = strlen(word);
  const char *p = text;
  while ((p = strstr(p, word)) != nullptr) {
    const bool startOk = (p == text) || !isalnum((unsigned char)p[-1]);
    const bool endOk = !isalnum((unsigned char)p[wl]);
    if (startOk && endOk) return true;
    p += wl;
  }
  return false;
}

// Intent classes produced by the tiny embedded model (tools/train_intent.py order).
enum IntentClass : uint8_t {
  INTENT_GREETING, INTENT_HOWAREYOU, INTENT_BORED, INTENT_JOKE,
  INTENT_TIME, INTENT_NAME, INTENT_THANKS, INTENT_BYE, INTENT_OTHER
};

// Response pack for model-detected intents; identical wording to the keyword path.
static const char *replyForIntent(int intent) {
  switch (intent) {
    case INTENT_GREETING:
      if (language == Language::HINDI) return "Namaste! Aap kaise hain?";
      if (language == Language::MARATHI) return "Namaskar! Kase aahat?";
      return "Hello! Lovely to see you.";
    case INTENT_HOWAREYOU:
      if (language == Language::HINDI) return "Main badhiya hoon! Aap batao.";
      if (language == Language::MARATHI) return "Mi chhan aahe! Tumhi kase?";
      return "I am doing great! How are you?";
    case INTENT_BORED:
      return "Let's play a game together!";
    case INTENT_JOKE:
      return "Why did the computer get cold? It left its Windows open!";
    case INTENT_TIME:
      return rtcReady ? "Open Clock to see the exact local time." : "Please connect the DS3231 clock module.";
    case INTENT_NAME:
      return "I am Lucy, your offline little companion.";
    case INTENT_THANKS:
      return "You are always welcome!";
    case INTENT_BYE:
      return "See you soon! Take care.";
    default:
      return nullptr; // INTENT_OTHER: fall through to the generic replies
  }
}

const char *offlineReply(const char *input) {
  static char normalized[96];
  size_t n = strlen(input);
  if (n >= sizeof(normalized)) n = sizeof(normalized) - 1;
  for (size_t i = 0; i < n; i++) normalized[i] = static_cast<char>(tolower(static_cast<unsigned char>(input[i])));
  normalized[n] = '\0';

  // Self-learning generator is always local and always wins.
  if (textHasWord(normalized, "generate") || textHasWord(normalized, "invent") ||
      strstr(normalized, "make words") || strstr(normalized, "new words") ||
      strstr(normalized, "something new") || strstr(normalized, "own words") ||
      strstr(normalized, "make up")) {
    return markovGenerate();
  }

  // Clock questions need the real RTC, which the PC model cannot see.
  if (textHasWord(normalized, "time") || textHasWord(normalized, "clock")) {
    return rtcReady ? "Open Clock to see the exact local time." : "Please connect the DS3231 clock module.";
  }

  // Brain maintenance commands belong to the serial console.
  if (strncmp(normalized, "brain", 5) == 0) {
    return "Use the serial console for brain commands: brain stats, save, reset.";
  }

  // PC-hosted model (Ollama). Falls through to the offline brain if disabled,
  // unreachable, or in its retry backoff window.
  if (aiEnabled) {
    const char *ai = aiReply(input);
    if (ai != nullptr) return ai;
  }

  if (strstr(normalized, "hello") || textHasWord(normalized, "hi") || textHasWord(normalized, "hey") || strstr(normalized, "namaste") || strstr(normalized, "namaskar")) {
    if (language == Language::HINDI) return "Namaste! Aap kaise hain?";
    if (language == Language::MARATHI) return "Namaskar! Kase aahat?";
    return "Hello! Lovely to see you.";
  }
  if (strstr(normalized, "how are you") || strstr(normalized, "kasa") || strstr(normalized, "kaise")) {
    if (language == Language::HINDI) return "Main badhiya hoon! Aap batao.";
    if (language == Language::MARATHI) return "Mi chhan aahe! Tumhi kase?";
    return "I am doing great! How are you?";
  }
  if (strstr(normalized, "bored") || strstr(normalized, "boring") || strstr(normalized, " कंटाळा")) return "Let's play a game together!";
  if (strstr(normalized, "joke")) return "Why did the computer get cold? It left its Windows open!";
  if (textHasWord(normalized, "name")) return "I am Lucy, your offline little companion.";
  if (textHasWord(normalized, "thank") || textHasWord(normalized, "thanks") || textHasWord(normalized, "thx")) return "You are always welcome!";
  if (textHasWord(normalized, "bye") || textHasWord(normalized, "goodbye")) return "See you soon! Take care.";
  // Keyword rules above cover the core phrases; the tiny on-device intent model
  // catches paraphrases the fixed keywords miss (see tools/train_intent.py).
  float intentConfidence = 0.0f;
  int intent = intentModelPredict(normalized, &intentConfidence);
  if (intentConfidence >= 0.55f) {
    const char *modelReply = replyForIntent(intent);
    if (modelReply != nullptr) return modelReply;
  }
  if (language == Language::HINDI) return "Main abhi offline hoon. Thoda aur bataiye!";
  if (language == Language::MARATHI) return "Mi offline aahe. Jara adhik sanga!";
  return "I am here! Try hello, joke, time, or how are you.";
}

void handleRtcSetCommand(const char *line);

static void handleLangCommand(const char *arg) {
  if (strncmp(arg, "en", 2) == 0) language = Language::ENGLISH;
  else if (strncmp(arg, "hi", 2) == 0) language = Language::HINDI;
  else if (strncmp(arg, "mr", 2) == 0) language = Language::MARATHI;
  else {
    Serial.println("Usage: LANG en|hi|mr");
    return;
  }
  saveSettings();
  Serial.print("Language: ");
  Serial.println(languageName(language));
}

// Only real AI commands are intercepted; sentences like "AI is cool" stay chat.
static bool isAiCommand(const char *line) {
  return strcmp(line, "AI") == 0 || strcmp(line, "AI on") == 0 ||
         strcmp(line, "AI off") == 0 || strncmp(line, "AI host ", 8) == 0 ||
         strncmp(line, "AI url ", 7) == 0 || strncmp(line, "AI model ", 9) == 0 ||
         strncmp(line, "AI token ", 9) == 0 || strncmp(line, "AI insecure ", 12) == 0;
}

static void printWifiStatus() {
  Serial.print("WiFi: ");
  if (wifiSsid.length() == 0) {
    Serial.println("station not configured (AP only)");
  } else if (WiFi.status() == WL_CONNECTED) {
    Serial.print("connected to '");
    Serial.print(wifiSsid);
    Serial.print("' as ");
    Serial.print(WiFi.localIP());
    Serial.print(" (");
    Serial.print(WiFi.RSSI());
    Serial.println(" dBm)");
  } else {
    Serial.print("joining '");
    Serial.print(wifiSsid);
    Serial.print("' (state ");
    Serial.print(static_cast<int>(WiFi.status()));
    Serial.println(")");
  }
  Serial.print("  AP 'Lucy-Setup': ");
  Serial.print(WiFi.softAPIP());
  Serial.print(" clients: ");
  Serial.println(WiFi.softAPgetStationNum());
}

void handleAiCommand(const char *line) {
  if (strcmp(line, "AI") == 0) {
    aiClearRetry();
    Serial.print("AI: ");
    Serial.println(aiEnabled ? "on" : "off");
    Serial.print("  endpoint: ");
    Serial.println(aiUrl());
    Serial.print("  model:    ");
    Serial.println(aiModel());
    Serial.print("  token:    ");
    if (aiTokenLength() > 0) {
      Serial.print(aiTokenLength());
      Serial.println(" chars");
    } else {
      Serial.println(aiUsesTls() ? "none (gateway will reject)" : "none (LAN Ollama mode)");
    }
    Serial.print("  TLS:      ");
    if (!aiUsesTls()) Serial.println("n/a (http)");
    else Serial.println(aiInsecure() ? "insecure, not verified" : "verified root CA");
    printWifiStatus();
    if (aiEnabled) {
      Serial.print("  ping: ");
      if (aiPing(aiUsesTls() ? 12000 : 4000)) {
        Serial.println("reachable");
      } else if (aiLastStatus() == 401) {
        Serial.println("HTTP 401 - token rejected (run AI token ...)");
      } else if (aiLastStatus() > 0) {
        Serial.print("HTTP ");
        Serial.print(aiLastStatus());
        Serial.println(" - server error");
      } else {
        Serial.println("unreachable (offline brain will answer)");
      }
    }
  } else if (strcmp(line, "AI on") == 0) {
    aiEnabled = true;
    aiClearRetry();
    saveSettings();
    Serial.println("AI: on. Chat uses the PC model when reachable, local brain otherwise.");
  } else if (strcmp(line, "AI off") == 0) {
    aiEnabled = false;
    saveSettings();
    Serial.println("AI: off. Using the built-in offline brain.");
  } else if (strncmp(line, "AI url ", 7) == 0) {
    aiSetUrl(line + 7);
    aiClearRetry();
    saveSettings();
    Serial.print("AI: endpoint ");
    Serial.println(aiUrl());
  } else if (strncmp(line, "AI token ", 9) == 0) {
    aiSetToken(line + 9);
    aiClearRetry();
    saveSettings();
    Serial.print("AI: token set (");
    Serial.print(aiTokenLength());
    Serial.println(" chars, hidden)");
  } else if (strncmp(line, "AI insecure ", 12) == 0) {
    bool insecure = strncmp(line + 12, "on", 2) == 0;
    aiSetInsecureTls(insecure);
    saveSettings();
    Serial.print("AI: TLS verification ");
    Serial.println(insecure ? "OFF (insecure)" : "on");
  } else if (strncmp(line, "AI host ", 8) == 0) {
    char host[32] = {0};
    int port = 0;
    if (sscanf(line + 8, "%31s %d", host, &port) >= 1) {
      aiSetEndpoint(host, (port > 0 && port <= 65535) ? static_cast<uint16_t>(port) : 0);
      aiClearRetry();
      saveSettings();
      Serial.print("AI: endpoint ");
      Serial.println(aiUrl());
    } else {
      Serial.println("AI: usage AI host <ip> [port]");
    }
  } else if (strncmp(line, "AI model ", 9) == 0) {
    aiSetModel(line + 9);
    aiClearRetry();
    saveSettings();
    Serial.print("AI: model ");
    Serial.println(aiModel());
  } else {
    Serial.println("AI: AI | AI on|off | AI url <http(s)://...> | AI host <ip> [port]");
    Serial.println("    AI token <secret> | AI insecure on|off | AI model <name>");
  }
}

static bool isWifiCommand(const char *line) {
  return strncmp(line, "WIFI", 4) == 0 &&
         (line[4] == '\0' || line[4] == ' ');
}

void handleWifiCommand(const char *line) {
  if (strcmp(line, "WIFI") == 0 || strcmp(line, "WIFI status") == 0) {
    printWifiStatus();
    return;
  }
  if (strcmp(line, "WIFI off") == 0) {
    wifiSsid = "";
    wifiPass = "";
    saveSettings();
    WiFi.disconnect(true, false);
    WiFi.mode(WIFI_AP);
    if (WiFi.softAPIP() == INADDR_NONE) WiFi.softAP("Lucy-Setup");
    Serial.println("WiFi: station disabled, AP-only again");
    return;
  }
  char ssid[33] = {0};
  char pass[65] = {0};
  int fields = sscanf(line + 5, "%32s %64s", ssid, pass);
  if (fields < 1) {
    Serial.println("WIFI usage: WIFI <ssid> [password] | WIFI status | WIFI off");
    return;
  }
  wifiSsid = ssid;
  wifiPass = pass;
  saveSettings();
  // AP + STA: the web console stays reachable on 192.168.4.1.
  WiFi.mode(WIFI_AP_STA);
  WiFi.setSleep(false);
  WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
  wifiLastKick = millis();
  wifiLastCheck = millis();
  Serial.print("WiFi: joining '");
  Serial.print(wifiSsid);
  Serial.println("' (Lucy-Setup stays up)");
  Serial.println("WiFi: use WIFI status to check; 2.4 GHz networks only");
}

void handleSerialConsole() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine[serialLength] = '\0';
      if (serialLength) {
        if (strncmp(serialLine, "SETTIME ", 8) == 0) {
          handleRtcSetCommand(serialLine);
        } else if (strncmp(serialLine, "LANG ", 5) == 0) {
          handleLangCommand(serialLine + 5);
        } else if (isWifiCommand(serialLine)) {
          handleWifiCommand(serialLine);
        } else if (isAiCommand(serialLine)) {
          handleAiCommand(serialLine);
        } else if (strcmp(serialLine, "brain stats") == 0) {
          markovPrintStats();
        } else if (strcmp(serialLine, "brain save") == 0) {
          markovSaveNow();
        } else if (strcmp(serialLine, "brain reset") == 0) {
          markovReset();
        } else {
          Serial.print("You: "); Serial.println(serialLine);
          markovLearn(serialLine);           // every message teaches the generator
          const char *reply = offlineReply(serialLine);
          Serial.print("Lucy: "); Serial.println(reply);
          if (screen == Screen::COMPANION) showCompanion(reply);
        }
      }
      serialLength = 0;
    } else if (serialLength < sizeof(serialLine) - 1) {
      serialLine[serialLength++] = c;
    }
  }
}

void openMenuItem() {
  switch (menuIndex) {
    case 0: screen = Screen::COMPANION; showCompanion(); break;
    case 1: screen = Screen::CLOCK; showClock(); break;
    case 2: screen = Screen::GAMES; gameIndex = 0; break;
    case 3: screen = Screen::SETTINGS; showSettings(); break;
    default: screen = Screen::ABOUT; showAbout(); break;
  }
}

// ---------------------------------------------------------------------------
// Web console (headless testing): WiFi softAP + framebuffer mirror.
// Open "Lucy-Setup" WiFi AP, then http://192.168.4.1 in a browser.
// Plain HTTP, no auth (AP is open and test-scoped). No internet, no cloud, no STA,
// no MCP — serial console and physical buttons behave as before.
// ---------------------------------------------------------------------------
static WebServer webServer(80);

static const char WEB_PAGE[] = R"HTML(<!doctype html>
<html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>LUCY OS - Web Console</title>
<style>
body{background:#0a0f14;color:#def6ff;font-family:ui-sans-serif,system-ui,sans-serif;max-width:720px;margin:0 auto;padding:10px 12px 20px;text-align:center}
h1{font-size:1.05rem;letter-spacing:.18em;color:#7de2ff;margin:.6em 0 .8em}
#screen{background:#000;border:2px solid #123;border-radius:10px;width:640px;max-width:100%;height:auto;image-rendering:pixelated;box-shadow:0 0 24px rgba(125,226,255,.12)}
.controls{margin:12px 0 10px}
.controls button{background:#102a33;color:#8ff0ff;border:1px solid #1e4b58;border-radius:12px;font-size:1.15rem;padding:14px 26px;margin:4px;min-width:110px;cursor:pointer}
.controls button:active{background:#1e4b58}
#chatlog{background:#05090c;border:1px solid #16323c;border-radius:10px;height:150px;overflow-y:auto;padding:10px;text-align:left;font-family:ui-monospace,monospace;font-size:.85rem;white-space:pre-wrap}
#chatlog .lucy{color:#8ff0ff}
#chatform{display:flex;gap:6px;margin-top:8px}
#chatform input{flex:1;background:#05090c;color:#def6ff;border:1px solid #16323c;border-radius:8px;padding:10px 12px;font-size:16px}
#chatform button{background:#1e4b58;color:#8ff0ff;border:0;border-radius:8px;padding:10px 18px;font-size:16px;cursor:pointer}
.hint{color:#4a6a78;font-size:.72rem;margin-top:8px}
</style></head><body>
<h1>LUCY OS &middot; WEB CONSOLE</h1>
<canvas id="screen" width="128" height="64"></canvas>
<div class="controls">
  <button id="up">&#9650; UP</button>
  <button id="select">&#9679; SELECT</button>
  <button id="back">&#9664; BACK</button>
</div>
<div id="chatlog"><div class="lucy">Lucy: Hello from the web console! Try "hello", "joke", "time", or "make words".</div></div>
<form id="chatform"><input id="msg" placeholder="Talk to Lucy..." autocomplete="off"><button type="submit">Send</button></form>
<div class="hint">Mirror of the 128x64 screen. Press the virtual buttons to drive the OS. Chat uses the model server when the AI bridge is on, Lucy's offline brain otherwise. Lucy stays offline-first: it only reaches the internet for the optional model gateway.</div>
<script>
var cv=document.getElementById('screen'),cx=cv.getContext('2d');
var log=document.getElementById('chatlog'),form=document.getElementById('chatform'),msg=document.getElementById('msg');
function say(who,text){var d=document.createElement('div');d.className=who==='Lucy'?'lucy':'';d.textContent=who+': '+text;log.appendChild(d);log.scrollTop=log.scrollHeight;}
function render(buf){var img=cx.createImageData(128,64);for(var p=0;p<8;p++){for(var c=0;c<128;c++){var b=buf[p*128+c];for(var bit=0;bit<8;bit++){var on=(b>>bit)&1,di=((p*8+bit)*128+c)*4;img.data[di]=7;img.data[di+1]=on?242:0;img.data[di+2]=on?176:0;img.data[di+3]=255;}}}cx.putImageData(img,0,0);}
setInterval(function(){fetch('/frame.bin').then(function(r){return r.arrayBuffer();}).then(function(a){render(new Uint8Array(a));}).catch(function(){});},250);
render(new Uint8Array(1024));
document.getElementById('up').onclick=function(){fetch('/press?btn=up');};
document.getElementById('select').onclick=function(){fetch('/press?btn=select');};
document.getElementById('back').onclick=function(){fetch('/press?btn=back');};
form.onsubmit=function(e){e.preventDefault();var t=msg.value.trim();if(!t)return;say('You',t);msg.value='';fetch('/say?text='+encodeURIComponent(t)).then(function(r){return r.text();}).then(function(t){say('Lucy',t);}).catch(function(){say('Lucy','(no reply)');});};
</script>
</body></html>)HTML";

static void handleWebRoot() { webServer.send(200, "text/html; charset=utf-8", WEB_PAGE); }

static void handleWebFrame() {
  // U8g2 full-buffer ("F") mode keeps the whole 128x64 mono framebuffer in RAM (8 pages x 128 cols).
  uint8_t *buf = display.getU8g2()->tile_buf_ptr;
  webServer.send(200, "application/octet-stream", String((char *)buf, 128 * 64 / 8));
}

static void handleWebPress() {
  String btn = webServer.arg("btn");
  Button *target = nullptr;
  if (btn == "up") target = &btnUp;
  else if (btn == "select") target = &btnSelect;
  else if (btn == "back") target = &btnBack;
  if (target == nullptr) { webServer.send(400, "text/plain", "unknown btn"); return; }
  target->pressed = true; // consumed by the normal take() path in loop(); bypasses debounce
  lastInteraction = millis();
  webServer.send(200, "text/plain", "ok");
}

static void handleWebSay() {
  String text = webServer.arg("text");
  text.trim();
  if (text.isEmpty()) { webServer.send(400, "text/plain", "empty"); return; }
  markovLearn(text.c_str()); // same learning path as the serial console
  webServer.send(200, "text/plain; charset=utf-8", offlineReply(text.c_str()));
}

static void webConsoleBegin() {
  WiFi.persistent(false);
  // AP + STA when a home network is configured: the web console keeps working
  // on 192.168.4.1 while Lucy reaches the internet.
  WiFi.mode(wifiSsid.length() ? WIFI_AP_STA : WIFI_AP);
  if (wifiSsid.length()) {
    WiFi.setSleep(false); // lower latency for chat
    WiFi.begin(wifiSsid.c_str(), wifiPass.c_str());
    Serial.print("WiFi: joining '");
    Serial.print(wifiSsid);
    Serial.println("' (Lucy-Setup stays up)");
  }

  if (!WiFi.softAP("Lucy-Setup")) {
    Serial.println("WARNING: web console AP failed to start");
    return;
  }
  webServer.on("/", HTTP_GET, handleWebRoot);
  webServer.on("/frame.bin", HTTP_GET, handleWebFrame);
  webServer.on("/press", HTTP_GET, handleWebPress);
  webServer.on("/say", HTTP_ANY, handleWebSay);
  webServer.begin();
  Serial.print("Web console AP 'Lucy-Setup': http://");
  Serial.println(WiFi.softAPIP());
}

static void webConsoleLoop() { webServer.handleClient(); }

// Non-blocking station supervisor: reports state changes and nudges a dead link.
static void wifiLoop() {
  if (wifiSsid.length() == 0) return;
  if (millis() - wifiLastCheck < 5000) return;
  wifiLastCheck = millis();

  bool connected = WiFi.status() == WL_CONNECTED;
  if (connected != wifiWasConnected) {
    wifiWasConnected = connected;
    if (connected) {
      Serial.print("WiFi: connected as ");
      Serial.print(WiFi.localIP());
      Serial.print(" (");
      Serial.print(WiFi.RSSI());
      Serial.println(" dBm)");
      aiClearRetry(); // let chat retry immediately
    } else {
      Serial.println("WiFi: connection lost; retrying in the background");
    }
  }
  if (!connected && millis() - wifiLastKick > 30000) {
    wifiLastKick = millis();
    WiFi.reconnect();
  }
}

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("Lucy Offline OS v0.1 booting — no internet / no cloud AI");

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  lastInteraction = millis();

  Wire.begin(21, 22);
  display.begin();
  display.setContrast(180);
  display.clearBuffer();
  display.setFont(u8g2_font_6x10_tf);
  drawCentered("LUCY OS", 27);
  drawCentered("OFFLINE STARTING", 43);
  display.sendBuffer();

  prefs.begin("lucy-os", false);
  loadSettings();

  rtcReady = rtc.begin();
  if (rtcReady && rtc.lostPower()) {
    // Do not invent the time. User must set RTC once via serial command.
    Serial.println("RTC lost power; set it using: SETTIME YYYY-MM-DD HH:MM:SS");
  }

  SPI.begin(18, 19, 23, PIN_SD_CS);
  sdReady = SD.begin(PIN_SD_CS);
  if (sdReady) {
    if (!SD.exists("/lucy")) SD.mkdir("/lucy");
    File info = SD.open("/lucy/boot.log", FILE_APPEND);
    if (info) { info.println("Lucy Offline OS boot"); info.close(); }
  }

  randomSeed(esp_random());
  markovBegin();
  Serial.println("Serial console ready. Type: hello, how are you, joke, time, name.");
  Serial.println("Self-learning words: 'make words' to generate, 'brain stats|save|reset' to manage.");
  Serial.println("RTC setup command: SETTIME YYYY-MM-DD HH:MM:SS");
  webConsoleBegin();
  Serial.print("AI bridge: ");
  Serial.print(aiEnabled ? "on" : "off");
  Serial.print(" (");
  Serial.print(aiUrl());
  Serial.print(", ");
  Serial.print(aiModel());
  Serial.print(", token ");
  if (aiTokenLength() > 0) {
    Serial.print(aiTokenLength());
    Serial.print(" chars");
  } else {
    Serial.print("none");
  }
  Serial.println(")");
  Serial.println("AI: AI | AI on|off | AI url <http(s)://...> | AI host <ip> [port]");
  Serial.println("    AI token <secret> | AI insecure on|off | AI model <name>");
  Serial.println("WiFi: WIFI <ssid> [password] | WIFI status | WIFI off   (2.4 GHz only)");
  screen = Screen::HOME;
  showHome();
}

void handleRtcSetCommand(const char *line) {
  int year, month, day, hour, minute, second;
  if (sscanf(line, "SETTIME %d-%d-%d %d:%d:%d", &year, &month, &day, &hour, &minute, &second) == 6) {
    if (rtcReady && year >= 2024 && month >= 1 && month <= 12 && day >= 1 && day <= 31 &&
        hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && second >= 0 && second <= 59) {
      rtc.adjust(DateTime(year, month, day, hour, minute, second));
      Serial.println("RTC time saved.");
    } else {
      Serial.println("Invalid date/time or RTC not detected.");
    }
  }
}

void loop() {
  pollButton(btnUp);
  pollButton(btnSelect);
  pollButton(btnBack);
  handleSerialConsole();
  webConsoleLoop();
  wifiLoop();
  markovTick(millis());

  // Serial time-setting is kept separate from chat parsing.
  if (serialLength == 0 && Serial.available() == 0) {
    // no-op; RTC can also be set with the dedicated command after reboot
  }

  if (screen == Screen::HOME) {
    if (take(btnSelect)) { screen = Screen::MENU; menuIndex = 0; showMenu(); }
    else if (take(btnUp)) { screen = Screen::COMPANION; showCompanion(); }
    else if (millis() - lastFrame > 250) { lastFrame = millis(); showHome(); }
  } else if (screen == Screen::MENU) {
    if (take(btnUp)) { menuIndex = (menuIndex + 1) % MENU_COUNT; showMenu(); }
    if (take(btnSelect)) openMenuItem();
    if (take(btnBack)) { screen = Screen::HOME; showHome(); }
  } else if (screen == Screen::CLOCK) {
    if (take(btnBack)) { screen = Screen::HOME; showHome(); }
    else if (millis() - lastFrame > 500) { lastFrame = millis(); showClock(); }
  } else if (screen == Screen::COMPANION) {
    if (take(btnUp)) { language = static_cast<Language>((static_cast<uint8_t>(language) + 1) % 3); saveSettings(); showCompanion(languageName(language)); }
    if (take(btnSelect)) showCompanion(offlineReply("hello"));
    if (take(btnBack)) { screen = Screen::HOME; showHome(); }
  } else if (screen == Screen::GAMES) {
    if (take(btnUp)) { gameIndex = (gameIndex + 1) % 3; }
    if (take(btnSelect)) {
      if (gameIndex == 0) startSnake();
      else { showCompanion(gameIndex == 1 ? "Tic Tac Toe: coming soon" : "Memory game: coming soon"); screen = Screen::COMPANION; }
    }
    if (take(btnBack)) { screen = Screen::MENU; showMenu(); }
    if (millis() - lastFrame > 250) {
      lastFrame = millis();
      display.clearBuffer(); drawHeader("GAMES");
      display.setFont(u8g2_font_6x10_tf);
      const char *games[] = {"Snake", "Tic Tac Toe", "Memory Match"};
      display.drawStr(5, 30, games[gameIndex]);
      drawFooter("UP: Next  SELECT: Play  BACK: Apps");
      display.sendBuffer();
    }
  } else if (screen == Screen::SNAKE) {
    if (take(btnBack)) { screen = Screen::GAMES; }
    if (snakeOver && take(btnSelect)) startSnake();
    updateSnake();
    if (millis() - lastFrame > 80) { lastFrame = millis(); showSnake(); }
  } else if (screen == Screen::SETTINGS) {
    if (take(btnUp)) { language = static_cast<Language>((static_cast<uint8_t>(language) + 1) % 3); saveSettings(); showSettings(); }
    if (take(btnSelect)) { soundEnabled = !soundEnabled; saveSettings(); showSettings(); }
    if (take(btnBack)) { screen = Screen::HOME; showHome(); }
  } else if (screen == Screen::ABOUT) {
    if (take(btnBack) || take(btnSelect)) { screen = Screen::MENU; showMenu(); }
  }

  if (millis() - lastInteraction > IDLE_RETURN_MS && screen != Screen::HOME && screen != Screen::SNAKE) {
    screen = Screen::HOME;
    lastInteraction = millis();
    showHome();
  }
  delay(4);
}
