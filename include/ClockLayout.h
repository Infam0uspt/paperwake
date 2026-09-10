#pragma once
#include <stdint.h>

// Shared between the firmware (src/EpaperDisplay.cpp) and the
// offline layout preview tool (tools/sim), so both always draw the
// clock face at the exact same coordinates.
//
// Coordinates below were measured directly off the Figma design
// (file 4etbWYxM7MBUU1JTfIyK6K, node 1:2/1:48, v2 — monospace
// "Google Sans Code" for the time) — ink bounding boxes pixel-measured
// from a 1:1 (792x272) screenshot via a row-by-row scan (not a coarse
// bounding-box scan: an earlier pass used overlapping scan windows and
// mismeasured the day-number's height as 87px when it's actually
// ~66px — that mistake is why the day number used to collide with the
// MON/MAR row above it). Each custom WideFont's fontconvert "size" was
// then tuned until its own glyph metrics reproduced these measured ink
// dimensions to within ~1-2px.
namespace ClockLayout {

constexpr int16_t kScreenW = 792;
constexpr int16_t kScreenH = 272;

// Hour/minute digits (GoogleSansCode_SemiBold105pt7b — a true
// monospace font, MONO=1 variable axis instance: every digit has the
// identical xAdvance=123, so the separator/minute position no longer
// shifts around depending on which digits are shown, unlike the
// previous proportional Reddit Sans Condensed attempt). Baseline
// derived so digit ink is vertically centered on screen (measured ink
// y: 57-206 for "11", 57-210 for "15"; center ~136).
constexpr int16_t kHourTextX = 18;
constexpr int16_t kClockTimeBaselineY = 209;

// Gap from the hour text's end (a fixed 246px for this monospace
// font's 2 digits) to the minute text's start — matches Figma's
// measured separator center (x=277) and its minute-block left edge
// (x=308) exactly: 0+246+62/2=277, 246+62=308.
constexpr int16_t kSeparatorGapWidth = 62;

// Partial-refresh region covering both digit groups + the separator,
// used when only the minute changes.
constexpr int16_t kClockTimeBoxX = 0;
constexpr int16_t kClockTimeBoxY = 40;
constexpr int16_t kClockTimeBoxW = 580;
constexpr int16_t kClockTimeBoxH = 190;

// Round separator dots between HH and MM (Figma "Group 1": two
// r~18 circles in a 37x91 box). X is computed at draw time (see
// drawBigTimeText()); only Y/size are fixed.
constexpr int16_t kSeparatorCenterY = 136;
constexpr int16_t kSeparatorDotRadius = 18;
constexpr int16_t kSeparatorDotSpacing = 28; // from center to each dot's center

// Alarm row: bell icon (30x30, Figma "bell 1" / "disabled 1") + time
// (GoogleSansFlex_Regular14pt7b — bumped 2pt from the original 12pt
// per user request, to more closely match Figma's measured ink
// height). Measured ink y:59-77, center 68.
constexpr int16_t kBellIconX = 618;
constexpr int16_t kBellIconY = 54;
constexpr int16_t kAlarmTextX = 658;
constexpr int16_t kAlarmTextBaselineY = 77;

// "Time until alarm" countdown line (e.g. "8h 5min from now" /
// "45min from now"), shown only while the alarm time is being edited
// — same 5s window that governs the bold alarm-time text — directly
// below the alarm row. No Figma node for this yet; X and the target
// vertical center came from the user's Inspect-panel readout
// (X=618, Y=88, H=19 -> center 97.5); baseline solved from that
// center against this font's own glyph metrics (font size itself is
// still an estimate, sized so the longest case "23h 59min from now"
// still fits before the screen edge — not yet ink-measured against a
// real Figma render).
constexpr int16_t kAlarmCountdownX = kBellIconX;
constexpr int16_t kAlarmCountdownBaselineY = 103;

// Underline drawn beneath whichever field (hour or minute) of the
// alarm time the encoder currently adjusts while editing (see
// main.cpp's editingAlarmHour). No Figma node for this either (a new
// interaction, not in the original design) — a small gap below the
// text baseline and a thin bar, sized to eyeball-verify on real
// hardware rather than derived from any spec.
constexpr int16_t kAlarmUnderlineOffsetY = 4; // below kAlarmTextBaselineY
constexpr int16_t kAlarmUnderlineThickness = 2;

// Partial-refresh region covering the bell icon + alarm time + the
// countdown line below it, used when the alarm is toggled on/off
// (MENU button) or its time is adjusted with the rotary encoder —
// none of that needs a full-screen refresh.
constexpr int16_t kAlarmIconBoxX = kBellIconX;
constexpr int16_t kAlarmIconBoxY = kBellIconY;
constexpr int16_t kAlarmIconBoxW = 174; // reaches the screen's right edge (618+174=792)
constexpr int16_t kAlarmIconBoxH = 58;

// Date row (v3 Figma revision): full month name only, no weekday
// (same small font, measured ink y:116-133, center 124.5) + big day
// number in GoogleSansCode_Medium44pt7b — same monospace family as
// the clock, medium weight — (measured ink y:143-208, center 175.5).
constexpr int16_t kDateMonthX = 618;
constexpr int16_t kDateLabelBaselineY = 132;
constexpr int16_t kDateDayX = 613;
constexpr int16_t kDateDayBaselineY = 206;

// "No wifi" icon (40x40, see Icons.h), bottom-right corner of the
// clock face — position given directly by the user, not derived from
// a Figma measurement like the rest of this file. Only shown while
// isWifiConnected() (TimeSync.h) is false.
constexpr int16_t kNoWifiIconX = 742;
constexpr int16_t kNoWifiIconY = 222;

// Wake-up ("alarm ringing") screen (Figma "Wake up screen", node
// 11:41). Stays on screen for the whole ringing session, through any
// snoozes, until dismissed — only the current time and the ZZZ icon
// row change after the initial draw. Baselines derived the same way
// as the rest of this file: measured ink center of a cap letter/digit
// (no descenders) solved against each generated font's own metrics.
constexpr int16_t kWakeTextX = 39;
constexpr int16_t kWakeTitleBaselineY = 72;      // "Good morning!"
constexpr int16_t kWakeSubtitleBaselineY = 122;  // "Today is a fresh start."
constexpr int16_t kWakeTimeBaselineY = 221;      // ticking current time

// Partial-refresh region for just the current-time text, updated once
// a minute without touching the rest of the wake screen.
constexpr int16_t kWakeTimeBoxX = 0;
constexpr int16_t kWakeTimeBoxY = 170;
constexpr int16_t kWakeTimeBoxW = 230;
constexpr int16_t kWakeTimeBoxH = 60;

// ZZZ snooze-count icons (Figma "sleep 1"/"sleep 2": two 50x50 boxes
// sitting flush against each other, no gap between them). Drawn in a
// row, one per snooze so far this session.
constexpr int16_t kWakeSleepIconSize = 50;
constexpr int16_t kWakeSleepIconY = 177;
constexpr int16_t kWakeSleepIconX = 227;       // first icon's left edge
constexpr int16_t kWakeSleepIconPitch = 50;    // boxes touch, no gap
constexpr int16_t kWakeSleepIconMaxCount = 10; // safety bound, plenty of screen width to spare

// Partial-refresh region covering all possible icon slots, used when
// the snooze count changes without redrawing the whole wake screen.
constexpr int16_t kWakeSleepIconBoxX = kWakeSleepIconX;
constexpr int16_t kWakeSleepIconBoxY = kWakeSleepIconY;
constexpr int16_t kWakeSleepIconBoxW = kWakeSleepIconPitch * kWakeSleepIconMaxCount;
constexpr int16_t kWakeSleepIconBoxH = kWakeSleepIconSize;

// Rightward shift applied to every settings-screen X coordinate below
// — 0 by default (the real, Figma-matched layout). Only ever non-zero
// in the "settings-shifted" PlatformIO env (build flag
// SETTINGS_X_SHIFT), a dev-only build for the currently-damaged test
// unit whose left edge is unreadable post-180°-rotation — see
// project_paperwake_hardware_setup.md. The real, undamaged screen
// (whenever the main firmware runs on it) always gets X_SHIFT=0.
#ifndef SETTINGS_X_SHIFT
#define SETTINGS_X_SHIFT 0
#endif
constexpr int16_t kSettingsXShift = SETTINGS_X_SHIFT;

// Settings menu (Figma redesign, tabs: Alarm/Light/System). v2 sizing
// (2026-07-25 update — everything got bigger + labels moved to
// English): title 40->48px, tabs 24->32px, rows 20->24px, icon
// 14->16px (now a plain filled/outline circle, not a bitmap — see
// EpaperDisplay.cpp). Baselines solved the usual way — target vertical
// center taken from the user's Inspect-panel box (Y, H), baseline
// computed against each generated font's own glyph metrics for a
// representative cap letter.
constexpr int16_t kSettingsTitleX = 48 + kSettingsXShift;
constexpr int16_t kSettingsTitleBaselineY = 51;

// Tabs: fixed X regardless of which is bold — the small width
// difference between SemiBold/Regular at this size isn't worth
// dynamically re-centering for. Original 3-tab spacing (Alarm=47,
// Light=165, System=265) reverse-engineered via measureWideText(): each
// tab's start = previous tab's X + previous tab's *selected* (bold)
// width + a fixed 16px gap — reproduces both original gaps to within
// 1px, so that's the real underlying rule, not eyeballed per-tab.
// "Sound" (added 2026-08-01, no Figma node) applies the same formula,
// which is why Light/System shifted right from their original X.
constexpr int16_t kSettingsTabBaselineY = 101;
constexpr int16_t kSettingsTabAlarmX = 47 + kSettingsXShift;
constexpr int16_t kSettingsTabSoundX = 164 + kSettingsXShift;  // 47 + "Alarm" selected width (101) + 16
constexpr int16_t kSettingsTabLightX = 287 + kSettingsXShift;  // 164 + "Sound" selected width (107) + 16
constexpr int16_t kSettingsTabSystemX = 388 + kSettingsXShift; // 287 + "Light" selected width (85) + 16

// Rows: label left-aligned at X=47, value right-aligned. Right edge
// set to match the "System" tab's own right edge in its regular
// (unselected) weight — X=265 + measured width 110 at
// GoogleSansFlex_Regular17pt7b — since that's the tab's resting state
// whenever an Alarm/Light row (i.e. most rows) is actually on screen.
// First row's box top Y=125, 35px pitch between rows (row 2 measured
// at Y=160).
constexpr int16_t kSettingsRowLabelX = 47 + kSettingsXShift;
constexpr int16_t kSettingsRowValueRightX = 375 + kSettingsXShift;
constexpr int16_t kSettingsFirstRowBoxTopY = 125;
constexpr int16_t kSettingsRowPitch = 35;
constexpr int16_t kSettingsRowBaselineY = 148; // first row; add i*kSettingsRowPitch per row

// Selector icon: a plain circle (drawn with fillCircle/drawCircle, no
// bitmap), 18px diameter (bumped 2px per user feedback — the Figma
// spec's 16px read as slightly small once flashed), X=18 fixed (left
// edge). Its vertical center — and the editing pill's below — is tied
// directly to the row text's own visual center (see
// kSettingsTextCenterYOffsetFromBaseline) rather than a fixed offset
// from the row box, so both stay exactly centered on the text
// regardless of icon/pill size tweaks.
constexpr int16_t kSettingsIconX = 18 + kSettingsXShift;
constexpr int16_t kSettingsIconDiameter = 18;
constexpr int16_t kSettingsIconRadius = kSettingsIconDiameter / 2;

// Vertical offset from a row's text baseline to that text's own ink
// center — derived from this font's digit/cap glyph metrics (height
// ~19-20, yOffset ~-18, so center = yOffset + height/2 =~ -8).
constexpr int16_t kSettingsTextCenterYOffsetFromBaseline = -8;

// Value "pill" shown while editing: black rounded-rect (corner radius
// 10) that hugs the actual rendered value text, not a fixed box —
// sized/padded so it still fits values of other widths (e.g. "-12",
// "UTC+12"), and vertically centered on the text (see above) rather
// than Figma's single measured sample, which was ~1-2px off-center.
constexpr int16_t kSettingsPillHeight = 30;
constexpr int16_t kSettingsPillCornerRadius = 10;

// "Connect to wifi" (AP-provisioning) screen — same plain functional
// layout style as the "Upload sound" screen (title + a few text lines
// + a QR code on the right; no Figma node for this), see
// EpaperDisplay.cpp's kUpload* constants for the sibling screen this
// mirrors. One extra line vs. the upload screen (SSID + password
// instead of just a URL), so the pitch between lines is a bit tighter.
constexpr int16_t kWifiSetupTextX = 48; // matches kUploadTextX/kSettingsTitleX
constexpr int16_t kWifiSetupTitleBaselineY = 51; // matches kUploadTitleBaselineY
constexpr int16_t kWifiSetupSsidBaselineY = 95;
constexpr int16_t kWifiSetupPasswordBaselineY = 130;
constexpr int16_t kWifiSetupStatusBaselineY = 180; // matches kUploadStatusBaselineY

// Step-by-step first-setup instructions, drawn between the status line
// and the bottom edge — the area left free under the left text column
// (the QR code occupies the right side). The EXIT hint is right-aligned
// under the QR code to keep this area clear.
constexpr int16_t kWifiSetupStep1BaselineY = 203;
constexpr int16_t kWifiSetupStep2BaselineY = 226;

// Battery icon (24x24) — top-right corner of the clock face, next to
// the date row. Only drawn when running on battery (see Power.h).
constexpr int16_t kBatteryIconX = 742;
constexpr int16_t kBatteryIconY = 34;

} // namespace ClockLayout
