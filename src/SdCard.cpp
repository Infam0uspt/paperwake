#include "SdCard.h"

#include <SD.h>
#include <SPI.h>
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>

#include "PinConfig.h"

namespace {
SPIClass sdSpi(HSPI);
SemaphoreHandle_t sdMutex = nullptr;
} // namespace

bool initSdCard() {
  sdMutex = xSemaphoreCreateRecursiveMutex();

  // Same "power pin high before touching the peripheral" pattern as
  // EPD_PWR in EpaperDisplay.cpp — polarity not yet physically
  // verified, flip to LOW here if the card fails to mount.
  pinMode(SD_PWR, OUTPUT);
  digitalWrite(SD_PWR, HIGH);
  delay(10);

  sdSpi.begin(SD_SCK, SD_MISO, SD_MOSI, SD_CS);
  // Was bumped to 20MHz to speed up uploads (see SoundUpload.cpp), but
  // that's a likely culprit for the WAV playback distortion/noise seen
  // afterward — this simple Arduino SD library doesn't necessarily
  // catch every bit error at speed, so "SD.begin() succeeds" and every
  // individual read()/write() "succeeding" doesn't guarantee the bytes
  // themselves are correct. Dropped back down to test that theory;
  // raise again (with real verification, e.g. a checksum comparison)
  // only if this turns out not to be it.
  bool ok = SD.begin(SD_CS, sdSpi, 4000000);
  Serial.printf("[SdCard] SD.begin()=%d\n", ok);
  return ok;
}

void sdLock() {
  if (sdMutex) xSemaphoreTakeRecursive(sdMutex, portMAX_DELAY);
}

void sdUnlock() {
  if (sdMutex) xSemaphoreGiveRecursive(sdMutex);
}

bool sdTryLock() {
  if (!sdMutex) return false;
  return xSemaphoreTakeRecursive(sdMutex, 0) == pdTRUE;
}
