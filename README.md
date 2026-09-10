# PaperWake

> **Aviso:** Este código ainda não foi testado em hardware real. As funcionalidades abaixo foram implementadas e compilam, mas precisam de validação física.

PaperWake is a simple, distraction-free E-ink alarm clock. No apps, no subscriptions, just a clock that wakes you up without distractions.

## Features

- **5.79" E-ink screen** - Pleasant for the eyes.
- **Physical controls** - Rotate the knob to change the alarm time, press the button behind it to activate/deactivate the alarm. Simple.
- **Wake-up light** - Integrated in the base of the enclosure. Also doubles as a nightlight.
- **Custom wake-up sounds** - Select built-in wake-up sounds, or upload your own via WiFi.
- **Frontlight** - Illuminates just the screen in the dark. Stays on when adjusting settings or alarm time.
- **Customizable** - Adjust the settings (volume, snooze duration, alarm sound, brightness levels...) directly on the device. No app required.
- **Web admin portal** - Configure alarmes, settings, firmware (OTA), and backup remotely at `http://paperwake.local/`.
- **Multi-alarm with weekdays** - Up to 8 alarmes, each with custom dias da semana.
- **Portuguese language** - Full UI and wake-up subtitles in PT with accents.
- **Battery powered** (optional) - Deep sleep mode extends battery life to weeks.
- **3D-printed enclosure** - Designed with repairability in mind, without compromising on an elegant, timeless aesthetic. 
- **Open-source software** - Make it your own!

## Hardware

- **Display + MCU:** Elecrow CrowPanel ESP32-S3 5.79" e-paper HMI Display
- **Audio:** MAX98357A I2S amplifier  + 3W speaker
- **Lighting:** SK6812 LEDs
- **Inputs:** EC11 rotary encoder + NO-pushbuttons

The print files for the enclosure can be found on my [Printables page](https://www.printables.com/model/1829351-paperwake-an-esp32-powered-e-ink-alarm-clock)

## Building

Requires [PlatformIO](https://platformio.org/).

```bash
cp include/secrets.h.example include/secrets.h
# fill in WIFI_SSID and WIFI_PASSWORD in include/secrets.h

pio run -e esp32-s3-devkitc-1 -t upload
```

See [WIRING.md](WIRING.md) for the full GPIO/pinout reference.

## New Features

### mDNS Access
On WiFi, the device registers as `paperwake.local`. Access the web admin at `http://paperwake.local/`. mDNS restarts on WiFi network switch and stops on suspend.

### Battery Mode
Enable with build flag `-DENABLE_BATTERY` or use the `battery` environment. Toggle "Battery" in System settings. When enabled, power-loss is detected and a one-time warning is shown on the e-paper before entering deep sleep.

### RTC (DS3231)
Enable with build flag `-DENABLE_RTC` or use the `rtc` environment. Toggle "RTC" in System settings. Connect DS3231 to I2C (GPIO 21=SDA, GPIO 38=SCL). Time is maintained without WiFi; NTP syncs when available.

## Session Changes (2026-09-02)

### mDNS Support
- `paperwake.local` is registered on WiFi connect
- mDNS restarts on WiFi network switch
- mDNS stops on suspend/deep sleep

### Battery Mode
- New build flag: `-DENABLE_BATTERY`
- New PlatformIO environment: `battery`
- New setting in System tab: "Battery" (On/Off), index 15
- Power-loss detection: transition from mains→battery sets a flag
- E-paper shows "Power loss" warning once, then clears after next full clock draw

### DS3231 RTC Support
- New build flag: `-DENABLE_RTC`
- New PlatformIO environment: `rtc`
- New build environment: `battery-rtc` (both features)
- New setting in System tab: "RTC" (On/Off), index 16
- DS3231 via I2C (GPIO 21=SDA, GPIO 3=SCL, address 0x68)
- NTP syncs RTC when WiFi is available
- RTC used as fallback when WiFi/NTP is unavailable

### Files Added/Modified
- `src/Rtc.h`, `src/Rtc.cpp` — DS3231 driver
- `src/Power.h`, `src/Power.cpp` — battery/power-loss logic
- `src/WebAdmin.cpp` — mDNS start/stop/restart
- `src/SettingsMenu.cpp` — Battery/RTC settings entries
- `src/SettingsMenu.h` — `getBatteryEnabled()`, `getRtcEnabled()`
- `src/main.cpp` — power-loss flag, RTC init, clock draw updates
- `src/EpaperDisplay.cpp`, `src/EpaperDisplay.h` — `powerLossWarning` parameter
- `src/TimeSync.cpp`, `src/TimeSync.h` — `initRtc()`, RTC fallback
- `platformio.ini` — added `battery`, `rtc`, `battery-rtc` environments
- `wokwi/` — Wokwi simulation scaffold (ESP32-S3 + ST7735 + encoder + buttons)

### Build Verification
All 6 PlatformIO environments build successfully:
- `esp32-s3-devkitc-1`
- `settings-shifted`
- `led_strip_test`
- `battery`
- `rtc`
- `battery-rtc`

## Status

This is a personal project and still a work in progress... The first prototype is being tested and refined.

## Future improvements

- **Frontlight placement**: The current enclosure causes the frontlight to cast a shadow over part of the screen, making it difficult to read in the dark.
- **Breakout PCB**: Currently, all external components are wired to the development board's GPIO expander using pin headers. A future breakout PCB could integrate the amplifier and provide JST connectors for each external component.
- **Assembly**: Improve mounting system for the (recessed) physical buttons, which are currently challenging to assemble.

### Implemented

- **Power outage indication**: Now handled via the battery/deep sleep module (Fase 4) — power-loss detection draws a warning before entering deep sleep.
- **WiFi-independent time**: Now handled by the optional RTC module (Fase 6) — DS3231 provides time without WiFi; NTP corrects it when available.
- **Portuguese accents**: Fontes regeneradas com range Latin-1; acentos PT renderizam no e-paper.

## A note on AI

I used Claude quite extensively while developing the firmware for PaperWake. I designed the architecture, UI and overall functionality myself, but relied on AI for a lot of the actual code generation, debugging and iteration.

I’m sharing this because I think it’s useful context when looking at the code. I’m not a professional software developer, and AI made it possible for me to build a GUI and firmware that’s way beyond what I could have written from scratch.
