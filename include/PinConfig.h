#pragma once

// E-paper display pins (CrowPanel ESP32 5.79", GDEY0579T93 / SSD1683x2)
// Source: official Elecrow-RD GitHub repo for this board.
constexpr int EPD_SCK = 12;
constexpr int EPD_MOSI = 11;
constexpr int EPD_RST = 47;
constexpr int EPD_DC = 46;
constexpr int EPD_CS = 45;
constexpr int EPD_BUSY = 48;

// Screen power enable — must be driven HIGH before talking to the
// panel, or BUSY never releases and every refresh times out.
constexpr int EPD_PWR = 7;

// Built-in buttons and rotate switch (KB, TM_2024A). Source: official
// Elecrow Eagle schematic ("EXIT button / MENU button" and "rotate
// switch" blocks).
constexpr int BTN_MENU = 2;
constexpr int BTN_EXIT = 1;
constexpr int ROTATE_UP = 6;
constexpr int ROTATE_DOWN = 4;
constexpr int ROTATE_CONF = 5;

// TF card (separate HSPI bus + its own power enable, not EPD_PWR).
// Not used yet — reserved ahead of the sound feature.
constexpr int SD_SCK = 39;
constexpr int SD_MISO = 13;
constexpr int SD_MOSI = 40;
constexpr int SD_CS = 10;
constexpr int SD_PWR = 42;

// Onboard "POWER LED" (schematic net "IO41_LED"). Used as a stand-in
// for the alarm sound until the I2S amp/speaker (Fase 2) is wired up:
// lit while the alarm is ringing, off otherwise.
constexpr int POWER_LED = 41;

// Loose EC11 rotary encoder + push button (Stap 1) — a nicer physical
// dial alongside the built-in rotate switch above, which stays wired
// as a fallback rather than being replaced. Physically wired; picked
// from the free GPIO expansion header
// (8, 3, 14, 9, 16, 15, 18, 17, 20, 19, 38, 21 — confirmed via
// Elecrow's own example sketch, no overlap with the pins above).
// GPIO3 avoided on purpose (a boot-strapping pin).
constexpr int ENCODER_A = 9;
constexpr int ENCODER_B = 16;
constexpr int ENCODER_SW = 15;

// Loose MAX98357A I2S amp (Stap 2), also physically wired.
// Remaining free pins from the same expansion header as the encoder
// above. The module's SD/GAIN pin is wired directly to 3.3V (always
// enabled — it stays silent without a valid I2S clock/data stream
// anyway), so no GPIO is needed for it.
constexpr int I2S_BCLK = 8;
constexpr int I2S_LRC = 18;
constexpr int I2S_DIN = 17;

// 1x SK1612 (RGBW) LED strip, 17 pixels, single data line — swapped in
// for an originally-planned WS2805 (RGBCCT) strip. Light.cpp splits it
// into two segments: the first 11 pixels (closest to data-in) are the
// wake-up light (ramps up before the alarm, doubles as a manual night
// light via EXIT long-press); the next 6 are the frontlight (lights
// the e-paper panel, EXIT short-press for 30s). Picked from the same
// free expansion-header pins as the encoder/I2S amp above.
constexpr int LED_STRIP_DATA = 14;

// Dedicated external "Snooze" and "Alarm on/off" buttons — physically
// wired. Same pattern as the loose EC11 encoder (Stap 1):
// wired alongside, not instead of, the built-in EXIT/MENU buttons
// above, which stay as a fallback. Controls.cpp merges each external
// button with its built-in counterpart before main.cpp ever sees a
// press, so SNOOZE_BTN behaves exactly like BTN_EXIT (snooze while
// ringing; frontlight/night-light short/long press in Mode::CLOCK) and
// ALARM_TOGGLE_BTN exactly like BTN_MENU (toggle the alarm on/off).
// Last 2 free pins from the same expansion header as the pins above.
constexpr int SNOOZE_BTN = 20;
constexpr int ALARM_TOGGLE_BTN = 19;
