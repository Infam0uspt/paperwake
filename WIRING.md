# Wiring

## Already built into the CrowPanel board (no wiring needed)

| Component | GPIO |
|---|---|
| E-paper SCK | 12 |
| E-paper MOSI | 11 |
| E-paper RST | 47 |
| E-paper DC | 46 |
| E-paper CS | 45 |
| E-paper BUSY | 48 |
| E-paper power (EPD_PWR) | 7 |
| MENU button | 2 |
| EXIT button | 1 |
| Rotate switch UP | 6 |
| Rotate switch DOWN | 4 |
| Rotate switch CONF | 5 |
| TF card SCK | 39 |
| TF card MISO | 13 |
| TF card MOSI | 40 |
| TF card CS | 10 |
| TF card power | 42 |
| POWER_LED (temporary sound indicator) | 41 |

## External components

| Component | GPIO | Status |
|---|---|---|
| EC11 encoder A | 9 |
| EC11 encoder B | 16 |
| EC11 encoder push button | 15 |
| MAX98357A I2S BCLK | 8 |
| MAX98357A I2S LRC | 18 |
| MAX98357A I2S DIN | 17 |
| MAX98357A SD/GAIN | — |
| SK1612 LED strip DIN | 14 |
| External "Snooze" button | 20 |
| External "Alarm on/off" button | 19 |
| DS3231 RTC SDA | 21 |
| DS3231 RTC SCL | 3 |

## Still-free pins (expansion header)

- GPIO 38 — currently used for battery ADC; avoid if adding new peripherals

## Optional: Battery module (gated by `-DENABLE_BATTERY`)

| Component | GPIO / pin | Notes |
|---|---|---|
| Battery + (Li-Ion/LiPo) | GPIO 38 via 100k resistor | ADC1_CH0; voltage divider halves battery voltage |
| Battery - (GND) | GND | Common ground |
| VBUS / USB 5V sense | Optional ADC or digital input | Currently inferred from battery voltage threshold |

Wiring notes:
- Use a 2:1 voltage divider (2x 100k resistors) from battery + to GPIO 38.
- Build flag: `-DENABLE_BATTERY` or PlatformIO env `battery`.
- Setting "Battery" must be enabled in System tab for power-loss detection.

## Optional: DS3231 RTC module (gated by `-DENABLE_RTC`)

| DS3231 pin | ESP32-S3 pin | Notes |
|---|---|---|
| VCC | 3.3V | |
| GND | GND | |
| SDA | GPIO 21 | |
| SCL | GPIO 3 | Strapping pin; verify boot behavior with your module |

Wiring notes:
- I2C address: `0x68`.
- Build flag: `-DENABLE_RTC` or PlatformIO env `rtc`.
- Setting "RTC" must be enabled in System tab.
- NTP syncs the RTC when WiFi is available; RTC keeps time without WiFi.
