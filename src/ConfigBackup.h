#pragma once

#include <Arduino.h>

// Backup/restore of the full persisted configuration (all settings +
// all alarms + the selected sound) as a single JSON document — the web
// admin portal's export/import feature. Versioned via a "schema" field
// so future importers can detect/refuse incompatible exports.

// Human-readable JSON (pretty-ish, small enough to serve directly).
String exportConfigJson();

// Applies a previously exported document. `summaryOut` receives a
// short human-readable result ("Applied N settings, M alarms").
// Returns false on parse/schema errors, with the reason in summaryOut.
bool applyConfigJson(const String &json, String &summaryOut);
