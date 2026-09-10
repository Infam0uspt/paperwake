# PaperWake Wokwi Simulation

This directory contains a minimal Wokwi simulation of the PaperWake clock firmware.

## What it simulates

- Real-time clock ticking every second
- Alarm at 07:30 with "WAKE UP!" screen
- Rotary encoder navigation in Settings menu
- Pushbutton controls (encoder button, Up, Down)
- Snooze logic with 5-minute delay
- Serial debug output at 115200 baud

## Hardware mapping

- Display: ST7735 128x160 (Wokwi-supported TFT used as a visual proxy for the real 5.79" e-paper)
- Rotary encoder CLK/DT/SW: GPIO 15/16/17
- Buttons: GPIO 18 (Up), GPIO 19 (Down), GPIO 17 (Encoder press)
- Display SPI: GPIO 12 (SCK), GPIO 11 (MOSI), GPIO 10 (CS), GPIO 9 (DC), GPIO 8 (RST), GPIO 7 (BL)

## Running in Wokwi

1. Open this folder in Wokwi (https://wokwi.com) or the Wokwi VS Code extension.
2. Press **Start Simulation**.
3. Use the rotary encoder and pushbuttons in the diagram to interact:
   - Rotate encoder to navigate settings rows
   - Press encoder button to enter Settings or snooze alarm
   - Press Up button in Settings to toggle Alarm On/Off
   - Press Down button (future use)

## Notes

- This is a stripped-down simulation, not the full PaperWake firmware.
- The real e-paper panel (GDEY0579T93) is not supported by Wokwi, so an ST7735 TFT is used instead.
- WiFi, audio, LED frontlight, and RTC/battery modules are omitted from this simulation.
