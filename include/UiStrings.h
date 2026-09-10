#pragma once

#include <Arduino.h>
#include <Preferences.h>

#include "WakeSubtitles.h"

// Central i18n: every user-facing e-paper string comes from here, in
// the language chosen in the Settings menu (persisted in NVS).
//
// The font headers in include/Fonts/ were regenerated with the
// Latin-1 range (0x20-0xFF) via tools/regenerate_fonts.py, so accented
// Portuguese characters (Ç Ã Á À É Ê Í Ó Ô Õ Ú Ü) now render correctly
// on the e-paper.

enum class Lang { EN = 0, PT = 1 };

struct Strings {
  // Settings screen
  const char *settingsTitle;
  const char *tabAlarm, *tabSound, *tabLight, *tabSystem;
  // FunctionalInt value styles
  const char *valOn, *valOff;
  const char *valManual, *valAuto;
    const char *valContinuous, *valAutoOff;
  // Sleep refresh value styles (0=Off, -1=Static, 30/60 min)
  const char *valSleepOff, *valSleepStatic, *valSleep30min, *valSleep1h;
  const char *loading;
  // Shared bottom hint of the upload + wifi-setup screens
  const char *exitHint;
  // WiFi-setup screen
  const char *wifiSetupTitle, *wifiStep1, *wifiStep2;
  const char *wifiWaiting, *wifiConnected, *wifiFailed;
  const char *wifiConnectingPrefix; // SSID and "..." appended after it
  // Upload screen
  const char *uploadTitle;
  const char *uploadWaiting;
  const char *uploadNoFile, *uploadUploading, *uploadComplete, *uploadFailed;
  const char *uploadDeletedPrefix;    // filename appended
  const char *uploadNotFound, *uploadNameUnchanged, *uploadRenameFailed;
  const char *uploadRenamedToPrefix;  // new name appended
  const char *uploadSelectedPrefix;   // filename appended ("Selected X")
  const char *uploadSelectedTone;
  // Wake screen
  const char *greetings[4]; // morning(5-11), afternoon(12-17), evening(18+), night(0-4)
  const char *months[12];   // uppercase, replaces strftime("%B") (which is locale-stuck EN)
  const char *const *wakeSubtitles;
  int wakeSubtitleCount;
  // The Language setting's own value labels ("English"/"Portugues")
  const char *langName;
};

constexpr Strings kStringsEn = {
    "Settings",
    "Alarm", "Sound", "Light", "System",
    "On", "Off",
    "Manual", "Auto",
    "Continuous", "Auto-off",
    "Off", "Static", "30 min", "1 h",
    "Loading...",
    "Press snooze to exit",
    "Connect to wifi",
    "1. Connect your phone to the network above",
    "2. Choose your home WiFi on the page that opens",
    "Waiting for connection", "Connected!", "Connection failed",
    "Connecting to ",
    "Upload sound",
    "Waiting for a file...",
    "No file selected", "Uploading", "Upload complete", "Upload failed, try again",
    "Deleted ", "File not found", "Name unchanged", "Rename failed", "Renamed to ",
    "Selected ", "Selected Tone",
    {"Good morning!", "Good afternoon!", "Good evening!", "Goodnight!"},
    {"JANUARY", "FEBRUARY", "MARCH", "APRIL", "MAY", "JUNE",
     "JULY", "AUGUST", "SEPTEMBER", "OCTOBER", "NOVEMBER", "DECEMBER"},
    kWakeSubtitles,
    kWakeSubtitlesCount,
    "English",
};

constexpr Strings kStringsPt = {
    "Ajustes",
    "Alarme", "Som", "Luz", "Sistema",
    "Ligado", "Desligado",
    "Manual", "Auto",
    "Contínuo", "Auto-desligar",
    "Desligado", "Estático", "30 min", "1 h",
    "A carregar...",
    "Prima snooze para sair",
    "Ligar ao Wi-Fi",
    "1. Ligue o telefone à rede acima",
    "2. Escolha a sua rede na página que abre",
    "A esperar ligação", "Ligado!", "Ligação falhou",
    "A ligar a ",
    "Enviar som",
    "A esperar por um ficheiro...",
    "Nenhum ficheiro", "A enviar", "Envio completo", "Envio falhou, tente de novo",
    "Apagado ", "Ficheiro não encontrado", "Nome igual", "Falha ao renomear", "Renomeado para ",
    "Selecionado ", "Seleccionado Tone",
    {"Bom dia!", "Boa tarde!", "Boa noite!", "Boa noite!"},
    {"JANEIRO", "FEVEREIRO", "MARÇO", "ABRIL", "MAIO", "JUNHO",
     "JULHO", "AGOSTO", "SETEMBRO", "OUTUBRO", "NOVEMBRO", "DEZEMBRO"},
    kWakeSubtitlesPt,
    kWakeSubtitlesPtCount,
    "Português",
};

// Language choice — persisted in the shared "paperwake" NVS namespace
// (key "lang"). Read from NVS on every call (a single byte — cheap).
inline Lang getLanguage() {
  Preferences prefs;
  prefs.begin("paperwake", true);
  return static_cast<Lang>(prefs.getUChar("lang", 0));
}

inline void setLanguage(Lang lang) {
  Preferences prefs;
  prefs.begin("paperwake", false);
  prefs.putUChar("lang", static_cast<uint8_t>(lang));
}

// Convenience accessor for the active table.
inline const Strings &strings() { return getLanguage() == Lang::PT ? kStringsPt : kStringsEn; }
