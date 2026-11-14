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

// Lucy Offline OS v0.2 — classic ESP32 DevKit V1, SSD1306 128x64.
// Fork addition: WiFi AP web console for headless testing (mirrors the screen,
// injects button presses, chats with the offline brain). No internet, no cloud, no STA.
// v0.2.1: premium 1-bit UI, sans-serif (Helvetica) type system, glyph "emoji"
// plus SHOT/SCREEN serial hooks so the screen can be verified headlessly.

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

// ===========================================================================
// THEME — the 1-bit "premium" OLED look.
//
// The SSD1306 is a 128x64 monochrome panel driven by a fixed ASCII bitmap font:
// there is no Unicode/emoji glyph set on the device, so sending a real emoji
// character would render as nothing or garbage. Every "emoji" below is
// therefore *drawn*: six 8x8 hand-made glyphs (one bit per pixel) plus vector
// shapes for clock, sliders, sun, moon, wifi bars and toggles.
//
// Rules that make a 1-bit screen look deliberate instead of cramped:
//   * 11px status bar, dotted hairlines instead of solid rules
//   * 3px-radius rounded frames/pills for every container
//   * selection = filled pill, with glyph and label inverted (draw color 0)
//   * only two type sizes: 5x7 for meta, 6x10 for content
// ===========================================================================

// Type system. The old 5x7/6x10 bitmap faces are cramped and harsh on a 1-bit
// panel; U8g2's proportional Helvetica is smoother, spaces naturally, and still
// fits 128px. Only these faces are used anywhere in the firmware — change a
// macro here rather than a call site.
//
//   FONT_TITLE  bold sans — section headers and the selected row (weight = hierarchy)
//   FONT_BODY   regular sans — content lines, list rows
//   FONT_META   regular sans 8px — hints, badges, bubble text, secondary values
//   FONT_BIG    geometric sans display face for the clock digits
//
// Both body faces are 8px: with Helvetica, weight and letter-spacing carry the
// hierarchy instead of a second size, which keeps every baseline on the grid.
#define FONT_TITLE u8g2_font_helvB08_tf
#define FONT_BODY u8g2_font_helvR10_tf
#define FONT_META u8g2_font_helvR08_tf
#define FONT_BIG u8g2_font_logisoso20_tf

enum IconId : uint8_t {
  ICON_HEART = 0, ICON_DICE, ICON_INFO, ICON_CLOUD, ICON_SMILE, ICON_SPARK,
  ICON_CLOCK, ICON_SLIDERS, ICON_NONE
};

// 8x8 glyphs: one byte per row, bit i = pixel column i (LSB = leftmost).
static const uint8_t GLYPHS[6][8] = {
  { 0x66, 0xFF, 0xFF, 0xFF, 0x7E, 0x3C, 0x18, 0x00 }, // HEART
  { 0x7E, 0x81, 0x85, 0x89, 0x91, 0x81, 0x81, 0x7E }, // DICE
  { 0x3C, 0x42, 0x99, 0x91, 0x91, 0x99, 0x42, 0x3C }, // INFO
  { 0x18, 0x3C, 0x7E, 0xFF, 0xFF, 0xFF, 0x7E, 0x00 }, // CLOUD
  { 0x3C, 0x42, 0xA5, 0x81, 0xA5, 0x99, 0x42, 0x3C }, // SMILE
  { 0x00, 0x08, 0x1C, 0x7F, 0x1C, 0x08, 0x00, 0x00 }, // SPARK
};

void drawIcon(uint8_t id, int16_t x, int16_t y, uint8_t color = 1) {
  display.setDrawColor(color);
  if (id == ICON_CLOCK) {
    display.drawCircle(x + 3, y + 4, 3);
    display.drawLine(x + 3, y + 4, x + 3, y + 1);
    display.drawLine(x + 3, y + 4, x + 6, y + 4);
  } else if (id == ICON_SLIDERS) {
    display.drawHLine(x, y + 1, 8);
    display.drawHLine(x, y + 6, 8);
    display.drawDisc(x + 2, y + 1, 1);
    display.drawDisc(x + 5, y + 6, 1);
  } else if (id < 6) {
    for (uint8_t row = 0; row < 8; row++) {
      const uint8_t bits = GLYPHS[id][row];
      for (uint8_t col = 0; col < 8; col++) {
        if (bits & (1u << col)) display.drawPixel(x + col, y + row);
      }
    }
  }
  display.setDrawColor(1);
}

// Hairline made of single pixels: lighter than drawHLine on OLED, and it reads
// as "designed" rather than "default".
void drawDottedHLine(int16_t x, int16_t y, int16_t w) {
  for (int16_t i = 0; i < w; i += 2) display.drawPixel(x + i, y);
}

void drawStrFit(int16_t x, int16_t y, const char *text, int16_t maxW);

void drawCentered(const char *text, uint8_t y) {
  const int16_t w = display.getStrWidth(text);
  if (w > 126) {  // proportional widths: measure before centering, never clip
    drawStrFit(1, y, text, 126);
    return;
  }
  int16_t x = (128 - w) / 2;
  if (x < 0) x = 0;
  display.drawStr(x, y, text);
}

int16_t spacedWidth(const char *text, uint8_t track) {
  char one[2] = {0, 0};
  int16_t w = 0;
  for (const char *p = text; *p; p++) {
    one[0] = *p;
    w += display.getStrWidth(one);
    if (p[1]) w += track;
  }
  return w;
}

// Letter-spaced uppercase: the single cheapest "premium" cue on a small panel.
void drawSpaced(int16_t x, int16_t y, const char *text, uint8_t track) {
  char one[2] = {0, 0};
  for (const char *p = text; *p; p++) {
    one[0] = *p;
    display.drawStr(x, y, one);
    x += display.getStrWidth(one) + track;
  }
}

void drawSpacedCentered(int16_t y, const char *text, uint8_t track) {
  int16_t x = (128 - spacedWidth(text, track)) / 2;
  if (x < 0) x = 0;
  drawSpaced(x, y, text, track);
}

void drawHeader(const char *title) {
  display.setFont(FONT_TITLE);
  drawSpaced(3, 9, title, 1);
  drawDottedHLine(0, 12, 128);
}

// Single-line text that must never overrun its column: truncate with "..".
// Proportional fonts make widths data-dependent, so dynamic text (user lines,
// model titles, settings values) has to be measured, not assumed.
void drawStrFit(int16_t x, int16_t y, const char *text, int16_t maxW) {
  char buf[80];
  strncpy(buf, text, sizeof(buf) - 4);
  buf[sizeof(buf) - 4] = '\0';
  if (display.getStrWidth(buf) <= maxW) {
    display.drawStr(x, y, buf);
    return;
  }
  size_t n = strlen(buf);
  while (n > 0) {
    buf[n] = '\0';
    char probe[84];
    snprintf(probe, sizeof(probe), "%s..", buf);
    if (display.getStrWidth(probe) <= maxW) {
      display.drawStr(x, y, probe);
      return;
    }
    n--;
  }
}

// Left hint plus an optional right-aligned action. The left half is measured
// against the space the right half leaves, so the two can never collide.
// Measured on device: "BACK"=29, "UP next SEL open"=88, "UP lang SEL sound"=91.
void drawFooter(const char *left, const char *right = nullptr) {
  drawDottedHLine(0, 53, 128);
  display.setFont(FONT_META);
  int16_t avail = 122;
  if (right && *right) {
    const int16_t rw = display.getStrWidth(right);
    display.drawStr(126 - rw, 62, right);
    avail = 126 - rw - 5;
  }
  if (left && *left && avail > 12) drawStrFit(3, 62, left, avail);
}

// Signal bars driven by the real RSSI, so it is live information, not decoration.
void drawWifiBars(int16_t x, int16_t y, bool connected, int rssi) {
  uint8_t strength = 0;
  if (connected) {
    strength = rssi > -55 ? 4 : rssi > -67 ? 3 : rssi > -78 ? 2 : 1;
  }
  for (uint8_t i = 0; i < 4; i++) {
    const int16_t bx = x + i * 3;
    const int16_t bh = 2 + i * 2;
    if (i < strength) {
      display.drawBox(bx, y + 8 - bh, 2, bh);
    } else {
      display.drawPixel(bx, y + 7);
    }
  }
}

// AI bridge state: cloud = PC model reachable plan, chip = local-only brain.
void drawAiBadge(int16_t x, int16_t y) {
  if (aiEnabled) {
    drawIcon(ICON_CLOUD, x, y, 1);
  } else {
    display.drawRFrame(x + 1, y + 2, 6, 5, 1);
    display.drawLine(x, y + 1, x + 7, y + 7);
  }
}

void drawSun(int16_t cx, int16_t cy, uint8_t r) {
  display.drawDisc(cx, cy, r);
  for (uint8_t a = 0; a < 8; a++) {
    static const int8_t dx[8] = {1, 1, 0, -1, -1, -1, 0, 1};
    static const int8_t dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
    display.drawPixel(cx + dx[a] * (r + 2), cy + dy[a] * (r + 2));
  }
}

void drawMoon(int16_t cx, int16_t cy, uint8_t r) {
  display.drawDisc(cx, cy, r);
  display.setDrawColor(0);
  display.drawDisc(cx + 2, cy - 1, r);
  display.setDrawColor(1);
}

enum Mood : uint8_t { MOOD_HAPPY = 0, MOOD_CALM, MOOD_THINK, MOOD_WOW };

// The face is the brand: large soft eyes, mood-driven mouth, real blink.
//
// IMPORTANT: U8g2's rounded-box routine takes the radius as-is. If 2*r exceeds
// w or h, the inner rectangle underflows and the shape floods the screen, so
// every radius below is clamped by safeRadius().
uint8_t safeRadius(int16_t w, int16_t h, uint8_t r) {
  const int16_t limit = (w < h ? w : h) / 2;
  if (limit < 1) return 0;
  return (r > static_cast<uint8_t>(limit)) ? static_cast<uint8_t>(limit) : r;
}

void drawFace(int16_t cx, int16_t cy, uint8_t mood, bool blink) {
  const int16_t ex = 15;
  const int16_t ey = cy - 2;
  const uint8_t eyeH = (mood == MOOD_HAPPY) ? 12 : 16;
  const uint8_t radius = safeRadius(14, eyeH, 5);
  for (int8_t s = -1; s <= 1; s += 2) {
    const int16_t cxx = cx + s * ex;
    if (blink) {
      display.drawLine(cxx - 6, ey, cxx, ey + 3);
      display.drawLine(cxx, ey + 3, cxx + 6, ey);
      display.drawPixel(cxx, ey + 4);
    } else {
      // Solid eye with a 2x2 corner glint: reads as a face, not two rings.
      display.drawRBox(cxx - 7, ey - eyeH / 2, 14, eyeH, radius);
      display.setDrawColor(0);
      const int16_t px = (mood == MOOD_THINK) ? 1 : -3;
      const int16_t py = (mood == MOOD_THINK) ? -2 : -3;
      display.drawBox(cxx + px, ey + py, 2, 2);
      display.setDrawColor(1);
    }
  }
  const int16_t my = cy + 14;
  if (mood == MOOD_WOW) {
    display.drawCircle(cx, my - 1, 3);
  } else if (mood == MOOD_THINK) {
    display.drawDisc(cx + 3, my - 1, 2);
  } else if (mood == MOOD_CALM) {
    display.drawLine(cx - 6, my, cx + 6, my);
  } else {
    // Wide shallow parabola: a real smile at 1 bit, not a nose blob.
    // dy is deepest in the middle and 0 at the corners, so the ends lift.
    for (int16_t dx = -10; dx <= 10; dx++) {
      const int16_t dy = static_cast<int16_t>(2 - (dx * dx) / 50);  // 2..0
      display.drawPixel(cx + dx, my + dy);
      display.drawPixel(cx + dx, my + dy + 1);
    }
  }
}

void drawToggle(int16_t x, int16_t y, int16_t w, int16_t h, bool on) {
  display.drawRFrame(x, y, w, h, h / 2);
  const int16_t kd = h - 4;
  const int16_t kx = on ? (x + w - kd - 2) : (x + 2);
  display.drawDisc(kx + kd / 2, y + h / 2, kd / 2);
}

void drawScrollDots(int16_t x, int16_t y, uint8_t count, uint8_t index) {
  for (uint8_t i = 0; i < count; i++) {
    const int16_t dy = y + i * 6;
    if (i == index) display.drawRBox(x, dy, 4, 3, 1);
    else display.drawPixel(x + 1, dy + 1);
  }
}

// Greedy word wrap for the bubble text. Bounded output, never overflows.
uint8_t wrapText(const char *text, char out[][40], uint8_t maxLines, int16_t maxWidth) {
  uint8_t line = 0;
  int16_t w = 0;
  size_t oi = 0;
  out[0][0] = '\0';
  const char *p = text;
  bool truncated = false;
  while (*p && line < maxLines) {
    while (*p == ' ') p++;
    if (!*p) break;
    const char *ws = p;
    while (*p && *p != ' ') p++;
    size_t wl = static_cast<size_t>(p - ws);
    char word[40];
    if (wl > sizeof(word) - 1) wl = sizeof(word) - 1;
    memcpy(word, ws, wl);
    word[wl] = '\0';
    const int16_t ww = display.getStrWidth(word);
    const int16_t sep = oi ? display.getStrWidth(" ") : 0;
    if (oi > 0 && w + sep + ww > maxWidth) {
      out[line][oi] = '\0';
      if (line + 1 >= maxLines) {
        truncated = true;
        break;
      }
      line++;
      oi = 0;
      w = 0;
      out[line][0] = '\0';
    }
    if (oi > 0) {
      out[line][oi++] = ' ';
      w += sep;
    }
    for (size_t i = 0; i < wl && oi < 39; i++) out[line][oi++] = word[i];
    out[line][oi] = '\0';
    w += ww;
    if (line + 1 >= maxLines && *p) {
      // Peek: another word may still fit, the loop decides.
    }
  }
  if (truncated && oi + 3 < 39) {
    out[line][oi++] = '.';
    out[line][oi++] = '.';
    out[line][oi++] = '.';
    out[line][oi] = '\0';
  }
  return static_cast<uint8_t>(line + 1);
}

// Rounded message bubble. Returns its height so callers can stack them.
int16_t drawBubble(int16_t x, int16_t y, int16_t w, const char *text, uint8_t maxLines) {
  char lines[4][40];
  const uint8_t n = wrapText(text, lines, maxLines, w - 8);
  const int16_t h = n * 9 + 4;
  display.drawRFrame(x, y, w, h, 3);
  display.setFont(FONT_META);
  for (uint8_t i = 0; i < n; i++) display.drawStr(x + 4, y + 10 + i * 9, lines[i]);
  return h;
}

void drawStatusBar(bool withBrand, bool withTime) {
  const bool conn = (WiFi.status() == WL_CONNECTED);
  drawWifiBars(3, 1, conn, conn ? WiFi.RSSI() : 0);
  drawAiBadge(18, 1);
  if (withBrand) {
    display.setFont(FONT_META);
    drawSpaced(64 - spacedWidth("LUCY", 2) / 2, 8, "LUCY", 2);
  }
  if (withTime) {
    display.setFont(FONT_META);
    char t[8] = "--:--";
    if (rtcReady) {
      DateTime n = rtc.now();
      snprintf(t, sizeof(t), "%02d:%02d", n.hour(), n.minute());
    }
    display.drawStr(125 - display.getStrWidth(t), 8, t);
  }
}

// ---------------------------------------------------------------------------
// Companion chat state, mirrored by serial console, web console and buttons.
// ---------------------------------------------------------------------------
char chatUser[96] = "";
char chatReply[192] = "";
bool lastReplyFromModel = false;
uint8_t chatIcon = ICON_SPARK;

uint8_t iconForReply(const char *reply) {
  if (strstr(reply, "Hello") || strstr(reply, "Namaste") || strstr(reply, "Namaskar") ||
      strstr(reply, "great")) return ICON_SMILE;
  if (strstr(reply, "welcome")) return ICON_HEART;
  if (strstr(reply, "Clock") || strstr(reply, "time")) return ICON_CLOCK;
  if (strstr(reply, "game")) return ICON_DICE;
  if (strstr(reply, "Why did")) return ICON_SMILE;
  return ICON_SPARK;
}

void setChat(const char *user, const char *reply) {
  strncpy(chatUser, user ? user : "", sizeof(chatUser) - 1);
  chatUser[sizeof(chatUser) - 1] = '\0';
  strncpy(chatReply, reply ? reply : "", sizeof(chatReply) - 1);
  chatReply[sizeof(chatReply) - 1] = '\0';
  chatIcon = iconForReply(chatReply);
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

// Boot: logo, wordmark and a progress bar that tracks real init steps.
void showBoot(uint8_t step, uint8_t total) {
  display.clearBuffer();
  display.drawRFrame(0, 0, 128, 64, 5);
  drawFace(64, 17, MOOD_HAPPY, false);
  display.setFont(FONT_BODY);
  drawSpacedCentered(44, "LUCY", 3);
  display.setFont(FONT_META);
  drawCentered("OFFLINE COMPANION", 52);
  display.drawRFrame(34, 55, 60, 5, 2);
  const int16_t filled = static_cast<int16_t>((56 * step) / total);
  if (filled > 0) display.drawBox(36, 57, filled, 1);
  display.sendBuffer();
}

void showHome() {
  display.clearBuffer();
  drawStatusBar(true, true);
  drawDottedHLine(0, 11, 128);
  const bool blink = (millis() % 4700U) < 140U;
  drawFace(64, 24, MOOD_HAPPY, blink);
  display.setFont(FONT_META);
  const char *state = "LOCAL BRAIN";
  if (aiEnabled) state = (WiFi.status() == WL_CONNECTED) ? "AI ONLINE" : "AI READY";
  drawCentered(state, 51);
  drawDottedHLine(0, 53, 128);
  display.drawStr(3, 62, "UP chat");
  display.drawStr(74, 62, "SEL apps");
  display.sendBuffer();
}

void showMenu() {
  display.clearBuffer();
  drawHeader("APPS");
  display.setFont(FONT_META);
  char pos[12];
  snprintf(pos, sizeof(pos), "%u/%u", menuIndex + 1, MENU_COUNT);
  display.drawStr(125 - display.getStrWidth(pos), 8, pos);

  static const uint8_t icons[MENU_COUNT] = {
    ICON_HEART, ICON_CLOCK, ICON_DICE, ICON_SLIDERS, ICON_INFO
  };
  for (uint8_t i = 0; i < 3; i++) {
    const uint8_t item = static_cast<uint8_t>((menuIndex / 3) * 3 + i);
    if (item >= MENU_COUNT) break;
    const int16_t y = 14 + i * 13;
    const bool sel = (item == menuIndex);
    if (sel) display.drawRBox(1, y, 116, 12, 3);
    drawIcon(icons[item], 5, y + 2, sel ? 0 : 1);
    display.setFont(sel ? FONT_TITLE : FONT_BODY);
    display.setDrawColor(sel ? 0 : 1);
    display.drawStr(17, y + 10, menuItems[item]);
    display.setDrawColor(1);
  }
  drawFooter("UP next SEL open", "BACK");
  display.sendBuffer();
}

void showClock() {
  display.clearBuffer();
  drawHeader("CLOCK");
  if (rtcReady) {
    DateTime now = rtc.now();
    if (now.hour() >= 7 && now.hour() < 19) drawSun(114, 22, 3);
    else drawMoon(114, 22, 4);
    display.setFont(FONT_BIG);
    char timeText[8];
    snprintf(timeText, sizeof(timeText), "%02d:%02d", now.hour(), now.minute());
    const int16_t tw = display.getStrWidth(timeText);
    const int16_t tx = (128 - tw) / 2 - 7;
    display.drawStr(tx, 40, timeText);
    display.setFont(FONT_META);
    char secText[4];
    snprintf(secText, sizeof(secText), "%02d", now.second());
    display.drawStr(tx + tw + 2, 40, secText);
    display.setFont(FONT_BODY);
    static const char *weekday[] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
    char dateText[24];
    snprintf(dateText, sizeof(dateText), "%s  %04d-%02d-%02d", weekday[now.dayOfTheWeek()],
             now.year(), now.month(), now.day());
    drawCentered(dateText, 51);
  } else {
    // No DS3231 on the bus: keep the screen composed instead of half-empty.
    // The hint belongs in the footer row, which already has room for it.
    display.drawCircle(64, 24, 9);
    display.drawLine(64, 24, 64, 17);
    display.drawLine(64, 24, 71, 27);
    display.drawDisc(64, 24, 1);
    display.setFont(FONT_BODY);
    drawCentered("NO RTC", 44);
    drawFooter("SETTIME via serial", "BACK");
    display.sendBuffer();
    return;
  }
  drawFooter(nullptr, "BACK");
  display.sendBuffer();
}

void showCompanion(const char *message = nullptr) {
  display.clearBuffer();
  drawHeader("COMPANION");
  drawIcon(chatIcon, 116, 2, 1);
  const bool hasChat = (chatReply[0] != '\0');
  if (message) {
    display.setFont(FONT_BODY);
    drawCentered(message, 30);
    display.setFont(FONT_META);
    drawCentered("SEL for a demo reply", 44);
  } else if (!hasChat) {
    drawFace(64, 24, MOOD_HAPPY, (millis() % 4700U) < 140U);
    display.setFont(FONT_META);
    drawCentered("Say hello", 51);
  } else {
    // The reply is the hero; the question above it stays one clipped line.
    display.setFont(FONT_META);
    char you[72];
    snprintf(you, sizeof(you), "you: %s", chatUser);
    drawStrFit(3, 20, you, 122);
    drawIcon(lastReplyFromModel ? ICON_CLOUD : ICON_SMILE, 3, 23, 1);
    drawBubble(13, 22, 112, chatReply, 3);
  }
  display.setFont(FONT_META);
  if (!message && hasChat) {
    display.drawStr(3, 62, lastReplyFromModel ? "from PC model" : "offline brain");
  } else if (!message) {
    display.drawStr(3, 62, "UP lang SEL demo");
  }
  display.drawStr(125 - display.getStrWidth("BACK"), 62, "BACK");
  display.sendBuffer();
}

void showSettings() {
  display.clearBuffer();
  drawHeader("SETTINGS");
  char value[24];
  // Three rows, no interior rules: the air is what makes a 1-bit list readable.
  display.setFont(FONT_BODY);
  display.drawStr(4, 22, "Language");
  display.setFont(FONT_META);
  snprintf(value, sizeof(value), "%s", languageName(language));
  drawStrFit(122 - display.getStrWidth(value), 21, value, 120);

  display.setFont(FONT_BODY);
  display.drawStr(4, 35, "Sound");
  drawToggle(100, 27, 24, 10, soundEnabled);

  display.setFont(FONT_BODY);
  display.drawStr(4, 48, "AI bridge");
  drawToggle(100, 40, 24, 10, aiEnabled);
  drawFooter("UP lang SEL sound", "BACK");
  display.sendBuffer();
}

void showAbout() {
  display.clearBuffer();
  drawHeader("ABOUT");
  drawFace(64, 22, MOOD_HAPPY, false);
  display.setFont(FONT_TITLE);
  drawSpacedCentered(50, "LUCY OS", 2);
  display.setFont(FONT_META);
  drawCentered("v0.2.1 ESP32-D0WD-V3", 62);
  display.sendBuffer();
}

void showGames() {
  display.clearBuffer();
  drawHeader("GAMES");
  static const char *names[] = {"Snake", "Tic Tac Toe", "Memory Match"};
  for (uint8_t i = 0; i < 3; i++) {
    const int16_t y = 13 + i * 13;
    const bool sel = (i == gameIndex);
    if (sel) display.drawRBox(1, y, 116, 12, 3);
    display.setDrawColor(sel ? 0 : 1);
    display.drawRBox(5, y + 2, 9, 9, 2);
    display.setDrawColor(sel ? 1 : 0);
    display.setFont(FONT_META);
    char num[2] = {static_cast<char>('1' + i), 0};
    display.drawStr(8, y + 9, num);
    display.setDrawColor(sel ? 0 : 1);
    display.setFont(FONT_BODY);
    display.drawStr(19, y + 10, names[i]);
    display.setDrawColor(1);
  }
  drawFooter("UP next SEL play", "BACK");
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
  display.setFont(FONT_META);
  display.drawStr(3, 9, "SNAKE");
  drawIcon(ICON_HEART, 60, 1, 1);
  char score[8];
  snprintf(score, sizeof(score), "%u", snakeScore);
  display.drawStr(122 - display.getStrWidth(score), 9, score);
  display.drawRFrame(0, 12, 128, 43, 3);

  // Apple: a 4px disc with a leaf pixel above it.
  display.drawDisc(2 + foodX * 8, 15 + foodY * 5 + 2, 2);
  display.drawPixel(2 + foodX * 8, 12 + foodY * 5);

  for (uint8_t i = 0; i < snakeLength; i++) {
    const int16_t sx = snakeX[i] * 8;
    const int16_t sy = 17 + snakeY[i] * 5;
    if (i == 0) {
      display.drawRBox(sx, sy, 6, 4, 1);
      display.setDrawColor(0);
      display.drawPixel(sx + 2, sy + 1);
      display.drawPixel(sx + 4, sy + 1);
      display.setDrawColor(1);
    } else {
      display.drawBox(sx + 1, sy, 4, 4);
    }
  }

  if (snakeOver) {
    display.drawRBox(26, 22, 76, 22, 3);
    display.setDrawColor(0);
    display.setFont(FONT_BODY);
    drawCentered("GAME OVER", 31);
    display.setFont(FONT_META);
    drawCentered("SEL retry", 41);
    display.setDrawColor(1);
  }
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
  lastReplyFromModel = false;
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
    if (ai != nullptr) {
      lastReplyFromModel = true;
      return ai;
    }
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

// ---------------------------------------------------------------------------
// Headless visual test hooks (see SOP 5.2).
//   SHOT    hex-dumps the live 128x64 framebuffer so a PC can rebuild the
//           screen as a PNG without joining Lucy-Setup (which would steal the
//           uplink of the machine running the check).
//   SCREEN  jumps straight to a named screen for that capture.
// Both are read-only: they draw and report, never change settings.
// ---------------------------------------------------------------------------
void dumpFramebuffer() {
  const uint8_t *buf = display.getU8g2()->tile_buf_ptr;
  Serial.println("SHOT BEGIN 128 64");
  for (uint16_t i = 0; i < 1024; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
    if ((i & 31) == 31) Serial.println();
  }
  Serial.println("SHOT END");
}

void jumpToScreen(const char *name) {
  if (strcmp(name, "home") == 0) { screen = Screen::HOME; showHome(); }
  else if (strcmp(name, "menu") == 0) { screen = Screen::MENU; menuIndex = 0; showMenu(); }
  else if (strcmp(name, "clock") == 0) { screen = Screen::CLOCK; showClock(); }
  else if (strcmp(name, "companion") == 0) { screen = Screen::COMPANION; showCompanion(); }
  else if (strcmp(name, "settings") == 0) { screen = Screen::SETTINGS; showSettings(); }
  else if (strcmp(name, "about") == 0) { screen = Screen::ABOUT; showAbout(); }
  else if (strcmp(name, "games") == 0) { screen = Screen::GAMES; gameIndex = 0; showGames(); }
  else if (strcmp(name, "snake") == 0) { startSnake(); showSnake(); }
  else {
    Serial.println("SCREEN: home|menu|clock|companion|settings|about|games|snake");
    return;
  }
  lastInteraction = millis();
  Serial.print("SCREEN: ");
  Serial.println(name);
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
        } else if (strncmp(serialLine, "MEAS ", 5) == 0) {
          // Font-metric probe: proportional widths are data-dependent, so ask
          // the panel instead of guessing layout constants.
          const char *txt = serialLine + 5;
          display.setFont(FONT_META);
          const int16_t m = display.getStrWidth(txt);
          display.setFont(FONT_BODY);
          const int16_t b = display.getStrWidth(txt);
          display.setFont(FONT_TITLE);
          const int16_t t = display.getStrWidth(txt);
          Serial.print("MEAS meta=");
          Serial.print(m);
          Serial.print(" body=");
          Serial.print(b);
          Serial.print(" title=");
          Serial.println(t);
        } else if (strcmp(serialLine, "SHOT") == 0) {
          dumpFramebuffer();
        } else if (strncmp(serialLine, "SCREEN ", 7) == 0) {
          jumpToScreen(serialLine + 7);
        } else {
          Serial.print("You: "); Serial.println(serialLine);
          markovLearn(serialLine);           // every message teaches the generator
          const char *reply = offlineReply(serialLine);
          setChat(serialLine, reply);
          Serial.print("Lucy: "); Serial.println(reply);
          if (screen == Screen::COMPANION) showCompanion();
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
    case 2: screen = Screen::GAMES; gameIndex = 0; showGames(); break;
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
  const char *reply = offlineReply(text.c_str());
  setChat(text.c_str(), reply);
  webServer.send(200, "text/plain; charset=utf-8", reply);
  if (screen == Screen::COMPANION) showCompanion();
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
  Serial.println("Lucy Offline OS v0.2.1 booting — no internet / no cloud AI");

  pinMode(PIN_BTN_UP, INPUT_PULLUP);
  pinMode(PIN_BTN_SELECT, INPUT_PULLUP);
  pinMode(PIN_BTN_BACK, INPUT_PULLUP);
  lastInteraction = millis();

  Wire.begin(21, 22);
  display.begin();
  display.setContrast(180);
  for (uint8_t step = 0; step <= 4; step++) {
    showBoot(step, 4);
    delay(110);
  }

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
      else { showCompanion(gameIndex == 1 ? "Tic Tac Toe: soon" : "Memory: soon"); screen = Screen::COMPANION; }
    }
    if (take(btnBack)) { screen = Screen::MENU; showMenu(); }
    if (millis() - lastFrame > 250) { lastFrame = millis(); showGames(); }
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
