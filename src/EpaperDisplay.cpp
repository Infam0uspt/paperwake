#include "EpaperDisplay.h"

#include <ctype.h>

#include <GxEPD2_BW.h>
#include <qrcode.h>
#include <Fonts/GoogleSansCodeSemiBoldClock.h>
#include <Fonts/GoogleSansFlexRegularSmall.h>
#include <Fonts/GoogleSansFlexBoldSmall.h>
#include <Fonts/GoogleSansFlexRegularAlarmTime.h>
#include <Fonts/GoogleSansFlexBoldAlarmTime.h>
#include <Fonts/GoogleSansFlexRegularAlarmCountdown.h>
#include <Fonts/GoogleSansCodeMediumDayNumber.h>
#include <Fonts/GoogleSansFlexSemiBoldWakeTitle.h>
#include <Fonts/GoogleSansFlexMediumWakeSubtitle.h>
#include <Fonts/GoogleSansCodeMediumWakeTime.h>
#include <Fonts/GoogleSansFlexSemiBoldSettingsTitle.h>
#include <Fonts/GoogleSansFlexSemiBoldSettingsTab.h>
#include <Fonts/GoogleSansFlexRegularSettingsTab.h>
#include <Fonts/GoogleSansFlexRegularSettingsRow.h>

#include "PinConfig.h"
#include "ClockLayout.h"
#include "WideFont.h"
#include "Icons.h"
#include "WakeSubtitles.h"
#include "SettingsMenu.h"
#include "UiStrings.h"
#include "SoundUpload.h"
#include "TimeSync.h"
#include "WifiSetup.h"
#include "Power.h"

using namespace ClockLayout;

namespace {

GxEPD2_BW<GxEPD2_579_GDEY0579T93, GxEPD2_579_GDEY0579T93::HEIGHT> display(
    GxEPD2_579_GDEY0579T93(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

char timeBuf[6];    // "HH:MM", used by the alarm-ringing screen
char hourBuf[3];    // "HH"
char minuteBuf[3];  // "MM"
char monthBuf[12];  // "SETEMBRO"/"FEVEREIRO" (PT can need 10 chars >old 10 buf)
char dayBuf[3];     // "21"
char alarmBuf[6];   // "HH:MM"
char countdownBuf[24]; // "23h 59min from now"

// Minutes from `now` until the next occurrence of hour:minute (today
// if still upcoming, tomorrow if already passed or exactly now).
int minutesUntil(const struct tm &now, int hour, int minute) {
  int nowMinutes = now.tm_hour * 60 + now.tm_min;
  int targetMinutes = hour * 60 + minute;
  int delta = targetMinutes - nowMinutes;
  if (delta <= 0) delta += 24 * 60;
  return delta;
}

void formatCountdown(char *buf, size_t bufSize, const struct tm &now, int alarmHour, int alarmMinute) {
  int delta = minutesUntil(now, alarmHour, alarmMinute);
  int hours = delta / 60;
  int minutes = delta % 60;
  if (hours > 0 && minutes > 0) {
    snprintf(buf, bufSize, "%dh %dmin from now", hours, minutes);
  } else if (hours > 0) {
    snprintf(buf, bufSize, "%dh from now", hours);
  } else {
    snprintf(buf, bufSize, "%dmin from now", minutes);
  }
}

// Draws HH, then a hand-drawn round separator, then MM — rather than
// printing a font's own ':' glyph, so the dots are reliably round and
// centered regardless of any particular font's colon metrics. The
// separator/minute position is computed from where the hour text ends
// rather than hardcoded — harmless now that the time font is
// monospace (every digit has the same xAdvance), but keeps this
// correct if a proportional font is ever used again.
void drawBigTimeText() {
  int16_t hourEndX = drawWideText(display, kHourTextX, kClockTimeBaselineY, hourBuf,
                                   GoogleSansCode_SemiBold105pt7b, GxEPD_BLACK);

  int16_t sepCenterX = hourEndX + kSeparatorGapWidth / 2;
  display.fillCircle(sepCenterX, kSeparatorCenterY - kSeparatorDotSpacing, kSeparatorDotRadius, GxEPD_BLACK);
  display.fillCircle(sepCenterX, kSeparatorCenterY + kSeparatorDotSpacing, kSeparatorDotRadius, GxEPD_BLACK);

  drawWideText(display, hourEndX + kSeparatorGapWidth, kClockTimeBaselineY, minuteBuf,
               GoogleSansCode_SemiBold105pt7b, GxEPD_BLACK);
}

void toUpper(char *s) {
  for (; *s; ++s) *s = toupper(static_cast<unsigned char>(*s));
}

} // namespace

void initDisplay() {
  pinMode(EPD_PWR, OUTPUT);
  digitalWrite(EPD_PWR, HIGH);
  delay(100);

  SPI.begin(EPD_SCK, -1, EPD_MOSI, EPD_CS);
  display.init(115200);
  display.setRotation(2); // 180°
}

void drawBootLogo() {
  int16_t x = (kScreenW - kBootLogoWidth) / 2;
  int16_t y = (kScreenH - kBootLogoHeight) / 2;

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(x, y, kBootLogoBitmap, kBootLogoWidth, kBootLogoHeight, GxEPD_BLACK);
  } while (display.nextPage());
}

void drawFullClockFace(const struct tm &now, int alarmHour, int alarmMinute, bool alarmEnabled, bool powerLossWarning) {
  strftime(hourBuf, sizeof(hourBuf), "%H", &now);
  strftime(minuteBuf, sizeof(minuteBuf), "%M", &now);
  // Locale-independent month name from Strings.h — strftime("%B") is
  // stuck to the C locale's English names.
  strncpy(monthBuf, strings().months[now.tm_mon], sizeof(monthBuf) - 1);
  monthBuf[sizeof(monthBuf) - 1] = '\0';
  strftime(dayBuf, sizeof(dayBuf), "%d", &now);
  toUpper(monthBuf);
  snprintf(alarmBuf, sizeof(alarmBuf), "%02d:%02d", alarmHour, alarmMinute);

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    if (powerLossWarning) {
      drawWideText(display, 18, 30, "Power loss", GoogleSansFlex_Bold14pt7b, GxEPD_BLACK);
    }

    drawBigTimeText();

    display.drawBitmap(kBellIconX, kBellIconY,
                        alarmEnabled ? kBellIconBitmap : kBellDisabledIconBitmap,
                        kBellIconSize, kBellIconSize, GxEPD_BLACK);
    drawWideText(display, kAlarmTextX, kAlarmTextBaselineY, alarmBuf,
                 GoogleSansFlex_Regular14pt7b, GxEPD_BLACK);

    drawWideText(display, kDateMonthX, kDateLabelBaselineY, monthBuf,
                 GoogleSansFlex_Regular12pt7b, GxEPD_BLACK);
    drawWideText(display, kDateDayX, kDateDayBaselineY, dayBuf,
                                   GoogleSansCode_Medium44pt7b, GxEPD_BLACK);

    if (isOnBattery()) {
      // Battery icon with percentage bar fill inside the shell
      display.drawBitmap(kBatteryIconX, kBatteryIconY, kBatteryIconBitmap,
                         kBatteryIconSize, kBatteryIconSize, GxEPD_BLACK);
      int pct = getBatteryPercent();
      int fillHeight = (kBatteryIconSize * pct) / 100;
      if (fillHeight > 0) {
        int innerTop = kBatteryIconY + kBatteryIconSize - fillHeight;
        display.fillRect(kBatteryIconX + 2, innerTop,
                         kBatteryIconSize - 4, fillHeight, GxEPD_BLACK);
      }
    }

    if (!isWifiConnected()) {
      display.drawBitmap(kNoWifiIconX, kNoWifiIconY, kNoWifiIconBitmap,
                          kNoWifiIconSize, kNoWifiIconSize, GxEPD_BLACK);
    }
  } while (display.nextPage());
}

void updateTimePartial(const struct tm &now) {
  strftime(hourBuf, sizeof(hourBuf), "%H", &now);
  strftime(minuteBuf, sizeof(minuteBuf), "%M", &now);

  display.setPartialWindow(kClockTimeBoxX, kClockTimeBoxY, kClockTimeBoxW, kClockTimeBoxH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawBigTimeText();
  } while (display.nextPage());
}

void updateAlarmIconPartial(const struct tm &now, int alarmHour, int alarmMinute, bool alarmEnabled, bool bold,
                             bool showCountdown, bool underlineHour) {
  snprintf(alarmBuf, sizeof(alarmBuf), "%02d:%02d", alarmHour, alarmMinute);
  // A disabled alarm has no meaningful "time until" it — gated here,
  // centrally, rather than trusting every call site to only pass
  // showCountdown=true when alarmEnabled is also true.
  bool drawCountdown = showCountdown && alarmEnabled;
  if (drawCountdown) formatCountdown(countdownBuf, sizeof(countdownBuf), now, alarmHour, alarmMinute);

  // Bold glyphs in this font are a bit wider than regular at the same
  // nominal size, which would otherwise make the text visibly hop
  // sideways the moment it turns bold. Center it on the same slot the
  // regular text would occupy. Vertically the two weights' glyph
  // metrics are already symmetric around kAlarmTextBaselineY (see
  // ClockLayout.h), so no separate baseline nudge is needed.
  const WideFont &font = bold ? GoogleSansFlex_Bold14pt7b : GoogleSansFlex_Regular14pt7b;
  int16_t regularWidth = measureWideText(alarmBuf, GoogleSansFlex_Regular14pt7b);
  int16_t actualWidth = measureWideText(alarmBuf, font);
  int16_t startX = kAlarmTextX + (regularWidth - actualWidth) / 2;

  display.setPartialWindow(kAlarmIconBoxX, kAlarmIconBoxY, kAlarmIconBoxW, kAlarmIconBoxH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.drawBitmap(kBellIconX, kBellIconY,
                        alarmEnabled ? kBellIconBitmap : kBellDisabledIconBitmap,
                        kBellIconSize, kBellIconSize, GxEPD_BLACK);
    drawWideText(display, startX, kAlarmTextBaselineY, alarmBuf, font, GxEPD_BLACK);
    if (bold) {
      // Editing: underline whichever field (hour or minute) rotating
      // the encoder currently adjusts, so it's clear at a glance which
      // mode a prior encoder-button press left it in. "HH:" measured
      // as one string (rather than "HH" + a separately-measured ":")
      // to get the minute field's start position exactly right
      // regardless of any inter-glyph spacing this font applies.
      char hourPart[3];
      snprintf(hourPart, sizeof(hourPart), "%02d", alarmHour);
      char hourAndColon[4];
      snprintf(hourAndColon, sizeof(hourAndColon), "%02d:", alarmHour);
      int16_t hourWidth = measureWideText(hourPart, font);
      int16_t hourAndColonWidth = measureWideText(hourAndColon, font);
      int16_t underlineX = underlineHour ? startX : startX + hourAndColonWidth;
      int16_t underlineW = underlineHour ? hourWidth : (actualWidth - hourAndColonWidth);
      display.fillRect(underlineX, kAlarmTextBaselineY + kAlarmUnderlineOffsetY, underlineW,
                        kAlarmUnderlineThickness, GxEPD_BLACK);
    }
    if (drawCountdown) {
      drawWideText(display, kAlarmCountdownX, kAlarmCountdownBaselineY, countdownBuf,
                   GoogleSansFlex_Regular9pt7b, GxEPD_BLACK);
    }
  } while (display.nextPage());
}

namespace {
void drawWakeSleepIcons(int snoozeCount) {
  int count = snoozeCount < kWakeSleepIconMaxCount ? snoozeCount : kWakeSleepIconMaxCount;
  for (int i = 0; i < count; i++) {
    display.drawBitmap(kWakeSleepIconX + i * kWakeSleepIconPitch, kWakeSleepIconY,
                        kSleepIconBitmap, kWakeSleepIconSize, kWakeSleepIconSize, GxEPD_BLACK);
  }
}

// Fixed for the whole ringing session (evaluated once, when the alarm
// first triggers) — it does not re-evaluate as the ticking clock
// crosses a boundary mid-session.
const char *wakeGreeting(int hour) {
  if (hour >= 5 && hour < 12) return strings().greetings[0];
  if (hour >= 12 && hour < 18) return strings().greetings[1];
  if (hour >= 18) return strings().greetings[2];
  return strings().greetings[3];
}
} // namespace

void drawAlarmScreen(const struct tm &now, int snoozeCount) {
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &now);

  // The LED stand-in for sound is driven by main.cpp, not here.

  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);

    drawWideText(display, kWakeTextX, kWakeTitleBaselineY, wakeGreeting(now.tm_hour),
                 GoogleSansFlex_SemiBold33pt7b, GxEPD_BLACK);
    drawWideText(display, kWakeTextX, kWakeSubtitleBaselineY,
                 strings().wakeSubtitles[random(strings().wakeSubtitleCount)],
                 GoogleSansFlex_Medium21pt7b, GxEPD_BLACK);
    drawWideText(display, kWakeTextX, kWakeTimeBaselineY, timeBuf,
                 GoogleSansCode_Medium31pt7b, GxEPD_BLACK);
    drawWakeSleepIcons(snoozeCount);
  } while (display.nextPage());
}

void updateWakeTimePartial(const struct tm &now) {
  strftime(timeBuf, sizeof(timeBuf), "%H:%M", &now);

  display.setPartialWindow(kWakeTimeBoxX, kWakeTimeBoxY, kWakeTimeBoxW, kWakeTimeBoxH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWideText(display, kWakeTextX, kWakeTimeBaselineY, timeBuf,
                 GoogleSansCode_Medium31pt7b, GxEPD_BLACK);
  } while (display.nextPage());
}

void updateWakeSnoozeIconsPartial(int snoozeCount) {
  display.setPartialWindow(kWakeSleepIconBoxX, kWakeSleepIconBoxY, kWakeSleepIconBoxW, kWakeSleepIconBoxH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWakeSleepIcons(snoozeCount);
  } while (display.nextPage());
}

void updateWifiIconPartial(bool connected) {
  display.setPartialWindow(kNoWifiIconX, kNoWifiIconY, kNoWifiIconSize, kNoWifiIconSize);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    if (!connected) {
      display.drawBitmap(kNoWifiIconX, kNoWifiIconY, kNoWifiIconBitmap,
                          kNoWifiIconSize, kNoWifiIconSize, GxEPD_BLACK);
    }
  } while (display.nextPage());
}

namespace {
// Derived from Figma's measured pill sample (77x30 around "9 min" at
// this font) — see ClockLayout.h.
constexpr int16_t kSettingsPillPaddingX = 10;

// Only the current category's rows are ever visible (max 4, for
// Alarm) — 10 items at the full row pitch would overflow the 272px
// screen entirely. This box comfortably covers up to 4 rows + the
// editing pill's slight overhang, without needing to know exactly how
// many rows the current category has.
constexpr int16_t kSettingsRowsAreaY = 122;
constexpr int16_t kSettingsRowsAreaH = 145;

int16_t settingsRowBoxTopY(int row) { return kSettingsFirstRowBoxTopY + row * kSettingsRowPitch; }
int16_t settingsRowBaseline(int row) { return kSettingsRowBaselineY + row * kSettingsRowPitch; }

// Position of an item within its own category (0-based) — this is
// what actually determines its row's Y position on screen, since only
// one category's items are shown at a time.
int settingsCategoryRow(int index) {
  SettingCategory cat = getSettingsItem(index).category;
  int row = 0;
  for (int i = 0; i < index; i++) {
    if (getSettingsItem(i).category == cat) row++;
  }
  return row;
}

const WideFont &settingsTabFont(bool selected) {
  return selected ? GoogleSansFlex_SemiBold18pt7b : GoogleSansFlex_Regular17pt7b;
}

void drawSettingsTabs() {
  SettingCategory current = getSettingsItem(getSettingsSelectedIndex()).category;
  bool alarmSelected = current == SettingCategory::Alarm;
  bool soundSelected = current == SettingCategory::Sound;
  bool lightSelected = current == SettingCategory::Light;
  bool systemSelected = current == SettingCategory::System;
  drawWideText(display, kSettingsTabAlarmX, kSettingsTabBaselineY, strings().tabAlarm, settingsTabFont(alarmSelected),
               GxEPD_BLACK);
  drawWideText(display, kSettingsTabSoundX, kSettingsTabBaselineY, strings().tabSound, settingsTabFont(soundSelected),
               GxEPD_BLACK);
  drawWideText(display, kSettingsTabLightX, kSettingsTabBaselineY, strings().tabLight, settingsTabFont(lightSelected),
               GxEPD_BLACK);
  drawWideText(display, kSettingsTabSystemX, kSettingsTabBaselineY, strings().tabSystem, settingsTabFont(systemSelected),
               GxEPD_BLACK);
}

void drawSettingsRow(int index) {
  const SettingItem &item = getSettingsItem(index);
  int row = settingsCategoryRow(index);
  int16_t baseline = settingsRowBaseline(row);
  bool isSelected = (index == getSettingsSelectedIndex());
  bool editingThis = isSelected && isSettingsEditing();

  // The selector icon only appears on the currently selected row — a
  // plain circle, filled while browsing, outline while editing (the
  // emphasis shifts to the value's black pill once editing starts);
  // other rows show no icon at all, matching the Figma mock. Centered
  // on the row text's own vertical center, not a fixed row-relative
  // offset, so it stays centered regardless of the icon's size.
  if (isSelected) {
    int16_t iconCx = kSettingsIconX + kSettingsIconRadius;
    int16_t iconCy = baseline + kSettingsTextCenterYOffsetFromBaseline;
    if (editingThis) {
      // display.drawCircle() is a 1px ring; draw two concentric ones
      // for a visibly thicker (~2px) outline.
      display.drawCircle(iconCx, iconCy, kSettingsIconRadius, GxEPD_BLACK);
      display.drawCircle(iconCx, iconCy, kSettingsIconRadius - 1, GxEPD_BLACK);
    } else {
      display.fillCircle(iconCx, iconCy, kSettingsIconRadius, GxEPD_BLACK);
    }
  }

  drawWideText(display, kSettingsRowLabelX, baseline, getSettingsItemLabel(index), GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);

  char valueBuf[24];
  if (item.type == SettingType::Placeholder) {
    snprintf(valueBuf, sizeof(valueBuf), "XXX");
  } else if (item.type == SettingType::Action) {
    valueBuf[0] = '\0'; // no trailing indicator — just the row label ("Upload sound"/"WiFi") — per explicit request
  } else if (item.type == SettingType::SoundChoice) {
    const char *label = editingThis ? getSoundChoiceLabel(getSettingsDraftValue()) : getSelectedSoundLabel();
    snprintf(valueBuf, sizeof(valueBuf), "%s", label);
  } else {
    int value = editingThis ? getSettingsDraftValue() : getSettingsCurrentValue(index);
    if (item.onOffStyle) {
      snprintf(valueBuf, sizeof(valueBuf), "%s", value ? strings().valOn : strings().valOff);
    } else if (item.autoManualStyle) {
      snprintf(valueBuf, sizeof(valueBuf), "%s", value ? strings().valManual : strings().valAuto);
    } else if (item.ringModeStyle) {
      snprintf(valueBuf, sizeof(valueBuf), "%s", value ? strings().valContinuous : strings().valAutoOff);
    } else if (item.sleepRefreshStyle) {
      const char* label = strings().valSleepOff;
      if (value == 1) label = strings().valSleepStatic;
      else if (value == 30) label = strings().valSleep30min;
      else if (value == 60) label = strings().valSleep1h;
      snprintf(valueBuf, sizeof(valueBuf), "%s", label);
    } else if (item.languageStyle) {
      // The Language row shows each option in its own language.
      snprintf(valueBuf, sizeof(valueBuf), "%s", value ? kStringsPt.langName : kStringsEn.langName);
    } else if (item.zeroIsOff && value == 0) {
      snprintf(valueBuf, sizeof(valueBuf), "%s", strings().valOff);
    } else if (item.showSign) {
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
    int16_t pillY = pillCenterY - kSettingsPillHeight / 2; // centered on the text, not Figma's single sample
    display.fillRoundRect(pillX, pillY, pillW, kSettingsPillHeight, kSettingsPillCornerRadius, GxEPD_BLACK);
    drawWideText(display, valueX, baseline, valueBuf, GoogleSansFlex_Regular13pt7b, GxEPD_WHITE);
    if (item.type == SettingType::SoundChoice) {
      // Drawn outside the pill (not baked into valueBuf) and only
      // while editing, per explicit feedback — a plain running count
      // of how many sounds are available and where the current one
      // sits, without needing to cycle through them all to find out.
      const char *counter = getSoundChoiceCounter(getSettingsDraftValue());
      int16_t counterX = pillX + pillW + kSettingsPillPaddingX;
      drawWideText(display, counterX, baseline, counter, GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
    }
  } else {
    drawWideText(display, valueX, baseline, valueBuf, GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
  }
}

void drawSettingsRows() {
  SettingCategory current = getSettingsItem(getSettingsSelectedIndex()).category;
  int count = getSettingsItemCount();
  for (int i = 0; i < count; i++) {
    if (getSettingsItem(i).category == current) drawSettingsRow(i);
  }
}
} // namespace

void drawSettingsScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWideText(display, kSettingsTitleX, kSettingsTitleBaselineY, strings().settingsTitle, GoogleSansFlex_SemiBold25pt7b,
                 GxEPD_BLACK);
    drawSettingsTabs();
    drawSettingsRows();
  } while (display.nextPage());
}

void updateSettingsTabsPartial() {
  // Generously sized (covers ascenders/descenders like "System"'s 'y')
  // since this is already a small, cheap partial refresh.
  display.setPartialWindow(0, 65, kScreenW, 50);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSettingsTabs();
  } while (display.nextPage());
}

void updateSettingsRowsAreaPartial() {
  // Used when navigation crosses into a different category: which
  // rows are visible at all changes (not just which one is selected),
  // so the whole row area is cleared and redrawn rather than just the
  // 1-2 rows a same-category move would touch.
  display.setPartialWindow(0, kSettingsRowsAreaY, kScreenW, kSettingsRowsAreaH);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSettingsRows();
  } while (display.nextPage());
}

void updateSettingsTwoRowsPartial(int indexA, int indexB) {
  // Used for normal up/down navigation within a category: the old
  // row's icon needs erasing and the new row's icon needs drawing.
  // Doing that as one combined refresh (instead of two separate
  // updateSettingsRowPartial() calls) halves the visible flash count
  // per step — e-paper can't animate a smooth slide, but one flash
  // instead of two reads as noticeably more fluid.
  int16_t topA = settingsRowBoxTopY(settingsCategoryRow(indexA));
  int16_t topB = settingsRowBoxTopY(settingsCategoryRow(indexB));
  int16_t boxTop = (topA < topB ? topA : topB) - 3;
  int16_t boxBottom = (topA > topB ? topA : topB) + kSettingsRowPitch + 1;
  display.setPartialWindow(0, boxTop, kScreenW, boxBottom - boxTop);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSettingsRow(indexA);
    drawSettingsRow(indexB);
  } while (display.nextPage());
}

void updateSettingsRowPartial(int index) {
  // A partial-refresh window's update time scales with its area, not
  // just its refresh mode — refreshing all rows at once was barely
  // faster than a full refresh and still blocked loop() long enough to
  // misfire the button auto-repeat logic. Refreshing exactly one row
  // at a time keeps every update small and fast regardless of how many
  // rows the list has. The box starts a few px above the row (to cover
  // the editing pill, which sits slightly above the row's own top) and
  // spans the full row pitch downward.
  int16_t boxY = settingsRowBoxTopY(settingsCategoryRow(index)) - 3;
  display.setPartialWindow(0, boxY, kScreenW, kSettingsRowPitch + 1);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawSettingsRow(index);
  } while (display.nextPage());
}

void updateSettingsRowLoadingPartial(int index) {
  int16_t boxY = settingsRowBoxTopY(settingsCategoryRow(index)) - 3;
  display.setPartialWindow(0, boxY, kScreenW, kSettingsRowPitch + 1);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    // Draws the row in its normal (not-yet-editing) state — editing
    // hasn't actually started yet, since this is called while still
    // waiting for the SD lock settingsBeginEdit() needs — then appends
    // "Loading..." in the same slot the "i/N" counter occupies once
    // editing does start, so it reads as "this row is about to become
    // editable" rather than a value in its own right.
    drawSettingsRow(index);
    int16_t baseline = settingsRowBaseline(settingsCategoryRow(index));
    char valueBuf[24];
    snprintf(valueBuf, sizeof(valueBuf), "%s", getSelectedSoundLabel());
    int16_t valueWidth = measureWideText(valueBuf, GoogleSansFlex_Regular13pt7b);
    int16_t valueX = kSettingsRowValueRightX - valueWidth;
    int16_t loadingX = valueX + valueWidth + kSettingsPillPaddingX;
    drawWideText(display, loadingX, baseline, strings().loading, GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
  } while (display.nextPage());
}

namespace {
// Title text now comes from Strings.h (i18n); accessor instead of the
// old constexpr so the draw/measure sites below stay unchanged in shape.
const char *uploadTitleText() { return strings().uploadTitle; }
constexpr int16_t kUploadTitleBaselineY = 51; // matches kSettingsTitleBaselineY
constexpr int16_t kUploadUrlBaselineY = 130;
constexpr int16_t kUploadStatusBaselineY = 180;
constexpr int16_t kUploadTextX = 48; // matches kSettingsTitleX/kSettingsRowLabelX
constexpr int16_t kUploadProgressBarY = kUploadStatusBaselineY + 15; // just below the status line's baseline
constexpr int16_t kUploadProgressBarH = 16; // 2x the original 8px

void drawUploadStatusArea() {
  drawWideText(display, kUploadTextX, kUploadStatusBaselineY, getSoundUploadStatusLine().c_str(),
               GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
  if (isSoundUploadInProgress()) {
    // Bar width matches the title's width (not the status text's own,
    // which changes) — a stable-width track for the fill to move
    // through. Outline is the full track; the fill inside it reflects
    // getSoundUploadProgress() (0..1, estimated from the upload
    // request's Content-Length vs. bytes received so far).
    int16_t barW = measureWideText(uploadTitleText(), GoogleSansFlex_SemiBold25pt7b);
    // Two nested drawRect() calls for a ~2px-thick outline — same
    // trick used for the settings menu's editing-circle icon, since
    // Adafruit_GFX/GxEPD2 has no built-in stroke-width option.
    display.drawRect(kUploadTextX, kUploadProgressBarY, barW, kUploadProgressBarH, GxEPD_BLACK);
    display.drawRect(kUploadTextX + 1, kUploadProgressBarY + 1, barW - 2, kUploadProgressBarH - 2, GxEPD_BLACK);
    int16_t fillW = (int16_t)(barW * getSoundUploadProgress());
    if (fillW > 0) {
      display.fillRect(kUploadTextX, kUploadProgressBarY, fillW, kUploadProgressBarH, GxEPD_BLACK);
    }
  }
}

// The gap between the screen's top edge and the visual top of the
// "Upload sound" title (not its baseline, and not kUploadTextX, which
// is the *left* margin) — the tallest ascenders/descenders actually
// used in that string ('U'/'l'/'d') have yOffset -34 in this font (see
// GoogleSansFlexSemiBoldSettingsTitle.h), so the title's own visible
// top sits at kUploadTitleBaselineY - 34. Matching the QR code's
// margin to that value, per explicit feedback, rather than the left
// margin used before.
constexpr int16_t kQrMargin = kUploadTitleBaselineY - 34;
constexpr uint8_t kQrVersion = 3; // 29x29 modules — comfortable headroom for a short "http://<ip>" URL
constexpr uint8_t kQrModules = 4 * kQrVersion + 17; // deterministic for a fixed (non-auto) version — 29 here
constexpr int16_t kQrAreaSize = kScreenH - 2 * kQrMargin;
constexpr int16_t kQrModuleScale = kQrAreaSize / kQrModules; // floor
constexpr int16_t kQrRenderedSize = kQrModules * kQrModuleScale;
constexpr int16_t kQrX = kScreenW - kQrMargin - kQrRenderedSize;
constexpr int16_t kQrY = kQrMargin + (kQrAreaSize - kQrRenderedSize) / 2;

const char *exitHintText() { return strings().exitHint; }
// Bottom-aligned with the QR code's own bottom edge, left-aligned with
// the rest of this screen's text (kUploadTextX) — explicit request.
// The string has no descenders, so its baseline can sit right at the
// QR's bottom pixel row without looking visually low.
constexpr int16_t kUploadExitHintBaselineY = kQrY + kQrRenderedSize - 5; // nudged up 5px per live feedback
// Drawn with GoogleSansFlex_Regular13pt7b, not the smaller-looking
// GoogleSansFlex_Regular12pt7b used elsewhere on this screen — that
// font's glyph table only covers 0x20-0x5A (space..'Z', no lowercase:
// it was generated for the clock face's month name, always rendered
// UPPERCASE via toUpper()). Using it here silently dropped every
// lowercase letter, leaving only "P" visible — confirmed live.
// 13pt7b's 0x20-0x7A range covers the full lowercase alphabet.

// Doesn't change during an upload session (the URL is fixed), so this
// is only ever called once from drawUploadScreen()'s full refresh, not
// from updateUploadStatusPartial()'s partial one.
void drawUploadQrCode() {
  uint8_t qrcodeData[128]; // version 3 needs ~106 bytes per the library's own sizing formula
  QRCode qrcode;
  int8_t result = qrcode_initText(&qrcode, qrcodeData, kQrVersion, ECC_LOW, getSoundUploadUrl().c_str());
  if (result != 0) {
    Serial.printf("[EpaperDisplay] QR code generation failed (%d)\n", result);
    return;
  }

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(kQrX + x * kQrModuleScale, kQrY + y * kQrModuleScale, kQrModuleScale, kQrModuleScale,
                          GxEPD_BLACK);
      }
    }
  }
}
} // namespace

void drawUploadScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWideText(display, kUploadTextX, kUploadTitleBaselineY, uploadTitleText(), GoogleSansFlex_SemiBold25pt7b,
                 GxEPD_BLACK);
    drawWideText(display, kUploadTextX, kUploadUrlBaselineY, getSoundUploadUrl().c_str(),
                 GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
    drawUploadStatusArea();
    drawWideText(display, kUploadTextX, kUploadExitHintBaselineY, exitHintText(), GoogleSansFlex_Regular13pt7b,
                 GxEPD_BLACK);
    drawUploadQrCode();
  } while (display.nextPage());
}

void updateUploadStatusPartial() {
  // Width stops short of the QR code (kQrX), not the full screen width
  // — a full-width window's fillScreen(WHITE) was wiping out the
  // horizontal slice of the QR code that falls within this same
  // vertical range every time the status/bar redrew.
  int16_t areaW = kQrX - 10; // small safety gap short of the QR code's left edge
  display.setPartialWindow(0, kUploadStatusBaselineY - 30, areaW, 70); // taller bar (16px) needs a bit more room
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawUploadStatusArea();
  } while (display.nextPage());
}

namespace {
const char *wifiSetupTitleText() { return strings().wifiSetupTitle; }

// Same margin-derivation technique as kQrMargin above (title's own
// visual top, not a fixed constant), re-solved for this string's own
// tallest glyph rather than reusing the upload screen's already-solved
// -34: 'i' has yOffset -36 in this font (taller than 'C'/'f' at -35),
// the most negative or any letter in "Connect to wifi" — see
// GoogleSansFlexSemiBoldSettingsTitle.h.
constexpr int16_t kWifiSetupQrMargin = kWifiSetupTitleBaselineY - 36;
constexpr uint8_t kWifiSetupQrVersion = 3; // 29x29 — the join-network payload (~41 bytes) fits version 3's ECC_LOW capacity
constexpr uint8_t kWifiSetupQrModules = 4 * kWifiSetupQrVersion + 17;
constexpr int16_t kWifiSetupQrAreaSize = kScreenH - 2 * kWifiSetupQrMargin;
constexpr int16_t kWifiSetupQrModuleScale = kWifiSetupQrAreaSize / kWifiSetupQrModules;
constexpr int16_t kWifiSetupQrRenderedSize = kWifiSetupQrModules * kWifiSetupQrModuleScale;
constexpr int16_t kWifiSetupQrX = kScreenW - kWifiSetupQrMargin - kWifiSetupQrRenderedSize;
constexpr int16_t kWifiSetupQrY = kWifiSetupQrMargin + (kWifiSetupQrAreaSize - kWifiSetupQrRenderedSize) / 2;
// Bottom-aligned with the QR code, same reasoning as kUploadExitHintBaselineY above.
constexpr int16_t kWifiSetupExitHintBaselineY = kWifiSetupQrY + kWifiSetupQrRenderedSize - 5; // nudged up 5px per live feedback

void drawWifiSetupStatusArea() {
  drawWideText(display, kWifiSetupTextX, kWifiSetupStatusBaselineY, getWifiSetupStatusLine().c_str(),
               GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
}

// Doesn't change while the screen is up (AP credentials are fixed) —
// only ever called once, from drawWifiSetupScreen()'s full refresh.
void drawWifiSetupQrCode() {
  uint8_t qrcodeData[128];
  QRCode qrcode;
  int8_t result = qrcode_initText(&qrcode, qrcodeData, kWifiSetupQrVersion, ECC_LOW,
                                   getWifiSetupApQrPayload().c_str());
  if (result != 0) {
    Serial.printf("[EpaperDisplay] WiFi-setup QR code generation failed (%d)\n", result);
    return;
  }

  for (uint8_t y = 0; y < qrcode.size; y++) {
    for (uint8_t x = 0; x < qrcode.size; x++) {
      if (qrcode_getModule(&qrcode, x, y)) {
        display.fillRect(kWifiSetupQrX + x * kWifiSetupQrModuleScale, kWifiSetupQrY + y * kWifiSetupQrModuleScale,
                          kWifiSetupQrModuleScale, kWifiSetupQrModuleScale, GxEPD_BLACK);
      }
    }
  }
}
} // namespace

void drawWifiSetupScreen() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWideText(display, kWifiSetupTextX, kWifiSetupTitleBaselineY, wifiSetupTitleText(),
                 GoogleSansFlex_SemiBold25pt7b, GxEPD_BLACK);
    String ssidLine = String("SSID: ") + kWifiSetupApSsid;
    String passwordLine = String("Password: ") + kWifiSetupApPassword;
    drawWideText(display, kWifiSetupTextX, kWifiSetupSsidBaselineY, ssidLine.c_str(),
                 GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
    drawWideText(display, kWifiSetupTextX, kWifiSetupPasswordBaselineY, passwordLine.c_str(),
                 GoogleSansFlex_Regular13pt7b, GxEPD_BLACK);
    drawWifiSetupStatusArea();
    // First-setup step-by-step instructions (drawWifiSetupScreen() only —
    // these never change while the screen is up). The exit hint below
    // moved right (under the QR code) to leave room for them.
    drawWideText(display, kWifiSetupTextX, kWifiSetupStep1BaselineY,
                 strings().wifiStep1, GoogleSansFlex_Regular12pt7b, GxEPD_BLACK);
    drawWideText(display, kWifiSetupTextX, kWifiSetupStep2BaselineY,
                 strings().wifiStep2, GoogleSansFlex_Regular12pt7b, GxEPD_BLACK);
    // Exit hint, right-aligned under the QR code (was left-aligned with
    // the text column; moved so the step instructions above fit).
    int16_t hintWidth = measureWideText(exitHintText(), GoogleSansFlex_Regular13pt7b);
    int16_t hintX = kWifiSetupQrX + (kWifiSetupQrRenderedSize - hintWidth) / 2;
    if (hintX < kWifiSetupTextX) hintX = kWifiSetupTextX; // degenerate-QR safety net
    drawWideText(display, hintX, kWifiSetupExitHintBaselineY, exitHintText(), GoogleSansFlex_Regular13pt7b,
                 GxEPD_BLACK);
    drawWifiSetupQrCode();
  } while (display.nextPage());
}

void updateWifiSetupStatusPartial() {
  // Width stops short of the QR code, same reasoning as
  // updateUploadStatusPartial() above (a full-width partial window
  // would wipe out a slice of the QR code on every status change).
  int16_t areaW = kWifiSetupQrX - 10;
  display.setPartialWindow(0, kWifiSetupStatusBaselineY - 30, areaW, 60);
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    drawWifiSetupStatusArea();
  } while (display.nextPage());
}

