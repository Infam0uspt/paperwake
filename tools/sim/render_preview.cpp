// Offline preview of the e-paper clock face. Mirrors the drawing
// logic in src/EpaperDisplay.cpp::drawFullClockFace() using the same
// ClockLayout.h constants and the same font/icon files, so the PNG
// this produces matches what the real hardware will show.
//
// Build: tools/sim/build.sh
// Usage: render_preview [HH] [MM] [alarmHH] [alarmMM] [on|off] out.ppm

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "canvas.h"

#include "ClockLayout.h"
#include "WideFont.h"
#include "Icons.h"
#include "WakeSubtitles.h"
#include "Fonts/GoogleSansCodeSemiBoldClock.h"
#include "Fonts/GoogleSansFlexRegularSmall.h"
#include "Fonts/GoogleSansFlexBoldSmall.h"
#include "Fonts/GoogleSansCodeMediumDayNumber.h"
#include "Fonts/GoogleSansFlexRegularAlarmTime.h"
#include "Fonts/GoogleSansFlexRegularAlarmCountdown.h"
#include "Fonts/GoogleSansFlexSemiBoldWakeTitle.h"
#include "Fonts/GoogleSansFlexMediumWakeSubtitle.h"
#include "Fonts/GoogleSansCodeMediumWakeTime.h"
#include "Fonts/GoogleSansFlexSemiBoldSettingsTitle.h"
#include "Fonts/GoogleSansFlexSemiBoldSettingsTab.h"
#include "Fonts/GoogleSansFlexRegularSettingsTab.h"
#include "Fonts/GoogleSansFlexRegularSettingsRow.h"

using namespace ClockLayout;

namespace {

std::string toUpper(std::string s) {
  for (auto &ch : s) ch = toupper(static_cast<unsigned char>(ch));
  return s;
}

// Mirrors EpaperDisplay.cpp's drawBigTimeText() exactly.
void drawBigTimeText(Canvas &c, const std::string &hourBuf, const std::string &minuteBuf) {
  int16_t hourEndX = drawWideText(c, kHourTextX, kClockTimeBaselineY, hourBuf.c_str(),
                                   GoogleSansCode_SemiBold105pt7b, true);

  int16_t sepCenterX = hourEndX + kSeparatorGapWidth / 2;
  c.fillCircle(sepCenterX, kSeparatorCenterY - kSeparatorDotSpacing, kSeparatorDotRadius, true);
  c.fillCircle(sepCenterX, kSeparatorCenterY + kSeparatorDotSpacing, kSeparatorDotRadius, true);

  drawWideText(c, hourEndX + kSeparatorGapWidth, kClockTimeBaselineY, minuteBuf.c_str(),
               GoogleSansCode_SemiBold105pt7b, true);
}

const char *wakeGreeting(int hour) {
  if (hour >= 5 && hour < 12) return "Good morning!";
  if (hour >= 12 && hour < 18) return "Good afternoon!";
  if (hour >= 18) return "Good evening!";
  return "Goodnight!";
}

void renderWakeScreen(Canvas &c, int hour, const std::string &timeStr, int snoozeCount, int subtitleIndex) {
  if (subtitleIndex < 0) subtitleIndex = rand() % kWakeSubtitlesCount;
  subtitleIndex %= kWakeSubtitlesCount;

  drawWideText(c, kWakeTextX, kWakeTitleBaselineY, wakeGreeting(hour),
               GoogleSansFlex_SemiBold33pt7b, true);
  drawWideText(c, kWakeTextX, kWakeSubtitleBaselineY, kWakeSubtitles[subtitleIndex],
               GoogleSansFlex_Medium21pt7b, true);
  drawWideText(c, kWakeTextX, kWakeTimeBaselineY, timeStr.c_str(),
               GoogleSansCode_Medium31pt7b, true);
  int count = snoozeCount < kWakeSleepIconMaxCount ? snoozeCount : kWakeSleepIconMaxCount;
  for (int i = 0; i < count; i++) {
    c.drawBitmap(kWakeSleepIconX + i * kWakeSleepIconPitch, kWakeSleepIconY,
                 kSleepIconBitmap, kWakeSleepIconSize, kWakeSleepIconSize, true);
  }
}

// Mirrors src/SettingsMenu.cpp's kItems exactly (duplicated here since
// that file pulls in the ESP32-only Preferences library and can't be
// linked into this desktop tool).
enum class SettingCategory { Alarm, Light, System };
enum class SettingType { FunctionalInt, Placeholder };
struct SettingItem {
  const char *label;
  SettingCategory category;
  SettingType type;
  const char *unit;
  bool showSign;
};
const SettingItem kSettingsItems[] = {
    {"Snooze duration", SettingCategory::Alarm, SettingType::FunctionalInt, "min", false},
    {"Auto-off", SettingCategory::Alarm, SettingType::FunctionalInt, "min", false},
    {"Volume", SettingCategory::Alarm, SettingType::Placeholder, "", false},
    {"Sound", SettingCategory::Alarm, SettingType::Placeholder, "", false},
    {"Light on/off", SettingCategory::Light, SettingType::Placeholder, "", false},
    {"Intensity", SettingCategory::Light, SettingType::Placeholder, "", false},
    {"Duration", SettingCategory::Light, SettingType::Placeholder, "", false},
    {"Timezone", SettingCategory::System, SettingType::FunctionalInt, "", true},
    {"Upload sound", SettingCategory::System, SettingType::Placeholder, "", false},
    {"WiFi", SettingCategory::System, SettingType::Placeholder, "", false},
};
constexpr int kSettingsItemCount = sizeof(kSettingsItems) / sizeof(kSettingsItems[0]);
constexpr int16_t kSettingsPillPaddingX = 10;

int16_t settingsRowBoxTopY(int row) { return kSettingsFirstRowBoxTopY + row * kSettingsRowPitch; }
int16_t settingsRowBaseline(int row) { return kSettingsRowBaselineY + row * kSettingsRowPitch; }

// Position of an item within its own category (0-based) — only the
// current category's items are ever shown at once (max 4, for Alarm;
// 10 items at the full row pitch would overflow the 272px screen).
int settingsCategoryRow(int index) {
  SettingCategory cat = kSettingsItems[index].category;
  int row = 0;
  for (int i = 0; i < index; i++) {
    if (kSettingsItems[i].category == cat) row++;
  }
  return row;
}

const WideFont &settingsTabFont(bool selected) {
  return selected ? GoogleSansFlex_SemiBold18pt7b : GoogleSansFlex_Regular17pt7b;
}

// Mirrors src/EpaperDisplay.cpp::drawSettingsScreen()/drawSettingsRow() exactly.
void renderSettingsScreen(Canvas &c, int selected, bool editing, int draftValue, int snoozeValue,
                           int autoOffValue, int utcOffsetValue) {
  drawWideText(c, kSettingsTitleX, kSettingsTitleBaselineY, "Settings", GoogleSansFlex_SemiBold25pt7b, true);

  SettingCategory current = kSettingsItems[selected].category;
  drawWideText(c, kSettingsTabAlarmX, kSettingsTabBaselineY, "Alarm",
               settingsTabFont(current == SettingCategory::Alarm), true);
  drawWideText(c, kSettingsTabLightX, kSettingsTabBaselineY, "Light",
               settingsTabFont(current == SettingCategory::Light), true);
  drawWideText(c, kSettingsTabSystemX, kSettingsTabBaselineY, "System",
               settingsTabFont(current == SettingCategory::System), true);

  for (int i = 0; i < kSettingsItemCount; i++) {
    if (kSettingsItems[i].category != current) continue;
    const SettingItem &item = kSettingsItems[i];
    int row = settingsCategoryRow(i);
    int16_t baseline = settingsRowBaseline(row);
    bool isSelected = (i == selected);
    bool editingThis = isSelected && editing;

    if (isSelected) {
      int16_t iconCx = kSettingsIconX + kSettingsIconRadius;
      int16_t iconCy = baseline + kSettingsTextCenterYOffsetFromBaseline;
      if (editingThis) {
        c.drawCircle(iconCx, iconCy, kSettingsIconRadius, true);
        c.drawCircle(iconCx, iconCy, kSettingsIconRadius - 1, true);
      } else {
        c.fillCircle(iconCx, iconCy, kSettingsIconRadius, true);
      }
    }

    drawWideText(c, kSettingsRowLabelX, baseline, item.label, GoogleSansFlex_Regular13pt7b, true);

    char valueBuf[24];
    if (item.type == SettingType::Placeholder) {
      snprintf(valueBuf, sizeof(valueBuf), "XXX");
    } else {
      int currentValue = (i == 0) ? snoozeValue : (i == 1) ? autoOffValue : utcOffsetValue;
      int value = editingThis ? draftValue : currentValue;
      if (item.showSign) {
        snprintf(valueBuf, sizeof(valueBuf), "UTC%+d", value);
      } else {
        snprintf(valueBuf, sizeof(valueBuf), "%d %s", value, item.unit);
      }
    }

    int16_t valueWidth = measureWideText(valueBuf, GoogleSansFlex_Regular13pt7b);
    int16_t valueX = kSettingsRowValueRightX - valueWidth;

    if (editingThis) {
      int16_t pillW = valueWidth + 2 * kSettingsPillPaddingX;
      int16_t pillX = valueX - kSettingsPillPaddingX; // centered on the text, not anchored to its right edge
      int16_t pillCenterY = baseline + kSettingsTextCenterYOffsetFromBaseline;
      int16_t pillY = pillCenterY - kSettingsPillHeight / 2;
      c.fillRoundRect(pillX, pillY, pillW, kSettingsPillHeight, kSettingsPillCornerRadius, true);
      drawWideText(c, valueX, baseline, valueBuf, GoogleSansFlex_Regular13pt7b, false);
    } else {
      drawWideText(c, valueX, baseline, valueBuf, GoogleSansFlex_Regular13pt7b, true);
    }
  }
}

} // namespace

int main(int argc, char **argv) {
  if (argc > 1 && std::string(argv[1]) == "settings") {
    // Usage: render_preview settings selectedIndex editing(0|1) draftValue [snoozeValue] [autoOffValue] [utcOffsetValue] out.ppm
    int selected = argc > 2 ? atoi(argv[2]) : 0;
    bool editing = argc > 3 ? atoi(argv[3]) != 0 : false;
    int draftValue = argc > 4 ? atoi(argv[4]) : 0;
    int snoozeValue = argc > 5 ? atoi(argv[5]) : 9;
    int autoOffValue = argc > 6 ? atoi(argv[6]) : 10;
    int utcOffsetValue = argc > 7 ? atoi(argv[7]) : 1;
    std::string outPath = argc > 8 ? argv[8] : "preview_settings.ppm";

    Canvas canvas(kScreenW, kScreenH);
    canvas.fillScreen(false);
    renderSettingsScreen(canvas, selected, editing, draftValue, snoozeValue, autoOffValue, utcOffsetValue);

    if (!canvas.writePPM(outPath.c_str())) {
      fprintf(stderr, "Failed to write %s\n", outPath.c_str());
      return 1;
    }
    printf("Wrote %s (%dx%d)\n", outPath.c_str(), kScreenW, kScreenH);
    return 0;
  }

  if (argc > 1 && std::string(argv[1]) == "wake") {
    // Usage: render_preview wake HH MM snoozeCount [subtitleIndex] out.ppm
    // subtitleIndex -1 (default) picks a random line from WakeSubtitles.h.
    int hour = argc > 2 ? atoi(argv[2]) : 7;
    int minute = argc > 3 ? atoi(argv[3]) : 0;
    int snoozeCount = argc > 4 ? atoi(argv[4]) : 0;
    int subtitleIndex = argc > 5 ? atoi(argv[5]) : -1;
    std::string outPath = argc > 6 ? argv[6] : "preview_wake.ppm";

    char timeBuf[6];
    snprintf(timeBuf, sizeof(timeBuf), "%02d:%02d", hour, minute);

    Canvas canvas(kScreenW, kScreenH);
    canvas.fillScreen(false);
    renderWakeScreen(canvas, hour, timeBuf, snoozeCount, subtitleIndex);

    if (!canvas.writePPM(outPath.c_str())) {
      fprintf(stderr, "Failed to write %s\n", outPath.c_str());
      return 1;
    }
    printf("Wrote %s (%dx%d)\n", outPath.c_str(), kScreenW, kScreenH);
    return 0;
  }

  int hour = 8, minute = 8, alarmHour = 7, alarmMinute = 0;
  bool alarmEnabled = true;
  bool editing = false;
  std::string outPath = "preview.ppm";

  if (argc > 1) hour = atoi(argv[1]);
  if (argc > 2) minute = atoi(argv[2]);
  if (argc > 3) alarmHour = atoi(argv[3]);
  if (argc > 4) alarmMinute = atoi(argv[4]);
  if (argc > 5) alarmEnabled = std::string(argv[5]) != "off";
  int pathArgIndex = 6;
  if (argc > 6 && std::string(argv[6]) == "edit") {
    editing = true;
    pathArgIndex = 7;
  }
  if (argc > pathArgIndex) outPath = argv[pathArgIndex];

  struct tm now = {};
  now.tm_hour = hour;
  now.tm_min = minute;
  now.tm_mon = 8;   // September, matching the Figma mock's "SEPTEMBER 21"
  now.tm_mday = 21;

  char hourBuf[3], minuteBuf[3], monthBuf[10], dayBuf[3], alarmBuf[6];
  strftime(hourBuf, sizeof(hourBuf), "%H", &now);
  strftime(minuteBuf, sizeof(minuteBuf), "%M", &now);
  strftime(monthBuf, sizeof(monthBuf), "%B", &now);
  strftime(dayBuf, sizeof(dayBuf), "%d", &now);
  std::string month = toUpper(monthBuf);
  snprintf(alarmBuf, sizeof(alarmBuf), "%02d:%02d", alarmHour, alarmMinute);

  Canvas canvas(kScreenW, kScreenH);
  canvas.fillScreen(false); // white

  drawBigTimeText(canvas, hourBuf, minuteBuf);

  canvas.drawBitmap(kBellIconX, kBellIconY,
                     alarmEnabled ? kBellIconBitmap : kBellDisabledIconBitmap,
                     kBellIconSize, kBellIconSize, true);
  drawWideText(canvas, kAlarmTextX, kAlarmTextBaselineY, alarmBuf,
               GoogleSansFlex_Regular14pt7b, true);

  if (editing) {
    int nowMinutes = hour * 60 + minute;
    int targetMinutes = alarmHour * 60 + alarmMinute;
    int delta = targetMinutes - nowMinutes;
    if (delta <= 0) delta += 24 * 60;
    char countdownBuf[24];
    int hours = delta / 60;
    int minutes = delta % 60;
    if (hours > 0 && minutes > 0) {
      snprintf(countdownBuf, sizeof(countdownBuf), "%dh %dmin from now", hours, minutes);
    } else if (hours > 0) {
      snprintf(countdownBuf, sizeof(countdownBuf), "%dh from now", hours);
    } else {
      snprintf(countdownBuf, sizeof(countdownBuf), "%dmin from now", minutes);
    }
    drawWideText(canvas, kAlarmCountdownX, kAlarmCountdownBaselineY, countdownBuf,
                 GoogleSansFlex_Regular9pt7b, true);
  }

  drawWideText(canvas, kDateMonthX, kDateLabelBaselineY, month.c_str(),
               GoogleSansFlex_Regular12pt7b, true);
  drawWideText(canvas, kDateDayX, kDateDayBaselineY, dayBuf,
               GoogleSansCode_Medium44pt7b, true);

  if (!canvas.writePPM(outPath.c_str())) {
    fprintf(stderr, "Failed to write %s\n", outPath.c_str());
    return 1;
  }
  printf("Wrote %s (%dx%d)\n", outPath.c_str(), kScreenW, kScreenH);
  return 0;
}
