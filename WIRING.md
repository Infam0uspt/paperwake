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
| DS3231 RTC SCL | 38 |

## Still-free pins (expansion header)

- GPIO 3 — deliberately avoided, boot-strapping pin
