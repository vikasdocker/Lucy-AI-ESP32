#pragma once
#include "cat_animation.h"

namespace companion_ui {
constexpr uint8_t SETTINGS_COUNT = 7;
static const char *const SETTINGS[] = {
  "Brightness", "Language", "Speaker volume", "Wi-Fi", "Access point",
  "Info", "Apps"
};
// The production display and native render harness use these same functions.
template<class D> void centered(D &d, const char *text, int y) {
  d.drawStr((128 - d.getStrWidth(text)) / 2, y, text);
}
template<class D> void boot(D &d, uint8_t step, uint32_t elapsed, const uint8_t *font) {
  d.clearBuffer();
  cat::tiny(d, 64 + cat::wave(elapsed, 1200, 2), 17, (elapsed / 300) % 2);
  d.setFont(font);
  centered(d, "Hi! I'm your", 38);
  centered(d, "virtual friend.", 48);
  d.drawRFrame(24, 51, 80, 3, 1);
  if (step) d.drawHLine(26, 52, 76 * (step > 5 ? 5 : step) / 5);
  centered(d, "Lucy is booting", 61);
}
template<class D> void home(D &d, uint8_t clip, uint32_t elapsed,
                           int battery, bool connected, int rssi, bool ap) {
  d.clearBuffer();
  cat::network(d, connected, rssi, ap);
  cat::battery(d, battery);
  cat::draw(d, clip, elapsed);
}
template<class D> void settings(D &d, uint8_t index, const uint8_t *font,
                               const uint8_t *titleFont, bool singleButton) {
  d.clearBuffer();
  d.setFont(titleFont);
  d.drawStr(3, 9, "SETTINGS");
  d.setFont(font);
  char page[] = "1/7";
  page[0] += index;
  d.drawStr(109, 9, page);
  for (int row = 0; row < 3; row++) {
    const int item = (index / 3) * 3 + row;
    const int y = 15 + row * 13;
    if (item >= SETTINGS_COUNT) break;
    if (item == index) d.drawRBox(1, y, 126, 12, 3);
    d.setDrawColor(item == index ? 0 : 1);
    d.drawStr(5, y + 10, SETTINGS[item]);
    d.setDrawColor(1);
  }
  d.setFont(font);
  centered(d, singleButton ? "Tap next  Hold open" : "UP next  SEL open", 61);
}
} // namespace companion_ui
