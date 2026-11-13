#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <Preferences.h>
#include <U8g2lib.h>
#include <RTClib.h>
#include <ctype.h>
#include <string.h>

// Lucy Offline OS v0.1 — classic ESP32 DevKit V1, SSD1306 128x64.
// This build has no network client, cloud endpoint, API key, or LLM dependency.

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
}

void loadSettings() {
  uint8_t stored = prefs.getUChar("lang", 0);
  if (stored <= 2) language = static_cast<Language>(stored);
  soundEnabled = prefs.getBool("sound", false);
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

const char *offlineReply(const char *input) {
  static char normalized[96];
  size_t n = strlen(input);
  if (n >= sizeof(normalized)) n = sizeof(normalized) - 1;
  for (size_t i = 0; i < n; i++) normalized[i] = static_cast<char>(tolower(static_cast<unsigned char>(input[i])));
  normalized[n] = '\0';

  if (strstr(normalized, "hello") || strstr(normalized, "hi") || strstr(normalized, "namaste") || strstr(normalized, "namaskar")) {
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
  if (strstr(normalized, "time")) return rtcReady ? "Open Clock to see the exact local time." : "Please connect the DS3231 clock module.";
  if (strstr(normalized, "name")) return "I am Lucy, your offline little companion.";
  if (strstr(normalized, "thank")) return "You are always welcome!";
  if (strstr(normalized, "bye")) return "See you soon! Take care.";
  if (language == Language::HINDI) return "Main abhi offline hoon. Thoda aur bataiye!";
  if (language == Language::MARATHI) return "Mi offline aahe. Jara adhik sanga!";
  return "I am here! Try hello, joke, time, or how are you.";
}

void handleRtcSetCommand(const char *line);

void handleSerialConsole() {
  while (Serial.available()) {
    char c = static_cast<char>(Serial.read());
    if (c == '\r') continue;
    if (c == '\n') {
      serialLine[serialLength] = '\0';
      if (serialLength) {
        if (strncmp(serialLine, "SETTIME ", 8) == 0) {
          handleRtcSetCommand(serialLine);
        } else {
          Serial.print("You: "); Serial.println(serialLine);
          Serial.print("Lucy: "); Serial.println(offlineReply(serialLine));
          if (screen == Screen::COMPANION) showCompanion(offlineReply(serialLine));
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

void setup() {
  Serial.begin(115200);
  delay(150);
  Serial.println();
  Serial.println("Lucy Offline OS v0.1 booting — no network / no cloud AI");

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  lastInteraction = millis();

  Wire.begin(21, 22);
  display.begin();
  display.setContrast(180);
  display.clearBuffer();
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
  Serial.println("Serial console ready. Type: hello, how are you, joke, time, name.");
  Serial.println("RTC setup command: SETTIME YYYY-MM-DD HH:MM:SS");
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
