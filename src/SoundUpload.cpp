#include "SoundUpload.h"

#include <SD.h>
#include <WebServer.h>
#include <WiFi.h>

#include "EpaperDisplay.h"
#include "SdCard.h"
#include "UiStrings.h"
#include "SettingsMenu.h"
#include "WebPortalStyle.h"

namespace {

constexpr const char *kSoundsDir = "/sounds";

WebServer server(80);
File uploadFile;
String statusLine = strings().uploadWaiting;
bool uploadInProgress = false;
bool handlersRegistered = false;
unsigned long uploadStartMs = 0;
unsigned long sdWriteTotalMs = 0; // cumulative time spent inside File::write() this upload
String uploadTargetPath; // "/sounds/<name>", decided at UPLOAD_FILE_START
size_t uploadExpectedBytes = 0; // from the request's Content-Length header; 0 = unknown
float uploadProgress = 0.0f; // 0..1, see getSoundUploadProgress()

// Keeps filenames filesystem- and display-safe: only alnum/./-/_,
// anything else becomes '_', capped at a reasonable length (the
// original name is what's shown both here and in the settings menu's
// "Sound" row, which has limited width).
String sanitizeFilename(const String &original) {
  String name = original;
  int lastSlash = name.lastIndexOf('/');
  if (lastSlash >= 0) name = name.substring(lastSlash + 1); // strip any path the browser sent
  if (name.length() > 40) name = name.substring(name.length() - 40); // keep the extension end
  String out;
  for (size_t i = 0; i < name.length(); i++) {
    char c = name[i];
    bool ok = isalnum((unsigned char)c) || c == '.' || c == '-' || c == '_';
    out += ok ? c : '_';
  }
  if (out.length() == 0) out = "sound.wav";
  return out;
}

// Finds a free path under /sounds/ for `sanitizedName`, appending
// "_1", "_2", ... before the extension if that name is already taken
// — never overwrites an existing file. Since this always returns a
// name that doesn't exist yet, nothing existing is ever at risk from
// a new upload, successful or not (a failed/aborted one just cleans
// up its own never-before-existing partial file, same as any other).
String uniqueTargetPath(const String &sanitizedName) {
  sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
  String path = String(kSoundsDir) + "/" + sanitizedName;
  if (SD.exists(path)) {
    int dot = sanitizedName.lastIndexOf('.');
    String base = dot >= 0 ? sanitizedName.substring(0, dot) : sanitizedName;
    String ext = dot >= 0 ? sanitizedName.substring(dot) : "";
    bool found = false;
    for (int n = 1; n < 1000; n++) {
      String candidate = String(kSoundsDir) + "/" + base + "_" + String(n) + ext;
      if (!SD.exists(candidate)) {
        path = candidate;
        found = true;
        break;
      }
    }
    // else: pathological fallback (1000 same-named files) — just overwrite `path` as-is
    (void)found;
  }
  sdUnlock();
  return path;
}

// Renders the sound list as one big radio-button form (selects the
// active alarm sound, POSTs to /select) — "Tone" (the generated-tone
// fallback) is always included as a selectable option, same as in the
// physical Settings menu's Sound picker. Rename/Delete can't be nested
// forms inside that big one (invalid HTML), so each row's Rename/
// Delete buttons are plain type='button's that submit a small,
// separate, hidden form placed after the big form's closing tag,
// referenced by id. Rename asks for the new name via a JS prompt()
// rather than an inline text field, per explicit feedback.
void appendSoundListHtml(String &html) {
  String currentSelection = getSelectedSoundFile();
  String hiddenForms;

  html += "<form method='POST' action='/select'><ul>";
  html += "<li><span><input type='radio' name='file' value=''";
  if (currentSelection.length() == 0) html += " checked";
  html += "> Tone</span></li>";

  sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
  File dir = SD.open(kSoundsDir);
  int idx = 0;
  if (dir) {
    File entry = dir.openNextFile();
    while (entry) {
      if (!entry.isDirectory()) {
        String name = entry.name(); // just the filename, SD.open(kSoundsDir) yields relative entries
        String i = String(idx);
        html += "<li><span><input type='radio' name='file' value='" + name + "'";
        if (name == currentSelection) html += " checked";
        html += "> " + name + "</span><span>" +
                "<button type='button' onclick=\"var n=prompt('New name:','" + name +
                "'); if(n){document.getElementById('rn_" + i + "').value=n;"
                "document.getElementById('rf_" + i + "').submit();}\">Rename</button>"
                " <button type='button' onclick=\"document.getElementById('del_" + i +
                "').submit()\">Delete</button></span></li>";
        hiddenForms += "<form id='rf_" + i +
                        "' style='display:none' method='POST' action='/rename'>"
                        "<input type='hidden' name='oldName' value='" +
                        name + "'><input type='hidden' name='newName' id='rn_" + i + "'></form>";
        hiddenForms += "<form id='del_" + i +
                        "' style='display:none' method='POST' action='/delete'>"
                        "<input type='hidden' name='file' value='" +
                        name + "'></form>";
        idx++;
      }
      entry = dir.openNextFile();
    }
    dir.close();
  }
  sdUnlock();
  html += "</ul><input type='submit' value='Set as alarm sound'></form>";
  html += hiddenForms;
}

void handleRoot() {
  String html = String("<html><head>") + kWebPortalHead + "</head><body><h3>PaperWake - Upload sound</h3>";
  appendSoundListHtml(html);
  html +=
      "<form method='POST' action='/upload' enctype='multipart/form-data'>"
      "<input type='file' name='file' accept='.wav,.mp3' "
      "onchange=\"document.getElementById('uploadBtn').disabled = !this.files.length\">"
      // Server already rejects an empty/no-file submission (see
      // UPLOAD_FILE_START in handleFileUpload()) — this is the
      // client-side half, so the button simply can't be clicked in
      // the first place until a file is actually chosen.
      "<input type='submit' id='uploadBtn' value='Upload' disabled>"
      "</form></body></html>";
  server.send(200, "text/html", html);
}

void handleUploadDone() { server.send(200, "text/plain", "Upload complete"); }

void handleFileUpload() {
  HTTPUpload &upload = server.upload();
  if (upload.status == UPLOAD_FILE_START) {
    if (upload.filename.length() == 0) {
      // Form submitted with no file actually chosen — previously this
      // still created a file (sanitizeFilename("") falls back to
      // "sound.wav"), a 0-byte placeholder with a misleading name.
      // Skip it entirely instead.
      uploadTargetPath = "";
      statusLine = strings().uploadNoFile;
      updateUploadStatusPartial();
      return;
    }
    uploadTargetPath = uniqueTargetPath(sanitizeFilename(upload.filename));
    sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
    uploadFile = SD.open(uploadTargetPath, FILE_WRITE);
    sdUnlock();
    statusLine = strings().uploadUploading;
    uploadInProgress = true;
    uploadStartMs = millis();
    sdWriteTotalMs = 0;
    // Content-Length is the whole multipart request body (a little
    // larger than the file itself, due to the multipart boundaries/
    // headers around it) — close enough for a progress estimate.
    // Requires collectHeaders() to have been called once, in
    // beginSoundUpload() below, or header() always returns "".
    String contentLength = server.header("Content-Length");
    uploadExpectedBytes = contentLength.length() ? (size_t)contentLength.toInt() : 0;
    uploadProgress = 0.0f;
    // WebServer::handleClient() processes an entire upload inside one
    // blocking call — main.cpp's loop() (and its usual "status changed,
    // redraw" polling) never regains control until the whole transfer
    // finishes, so it can never observe this status on its own. Drawn
    // directly from here instead, the only place with control while a
    // transfer is actually in progress.
    updateUploadStatusPartial();
  } else if (upload.status == UPLOAD_FILE_WRITE) {
    if (uploadTargetPath.length() == 0) return; // no file was selected — see UPLOAD_FILE_START above
    if (uploadFile) {
      sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
      unsigned long writeStart = millis();
      uploadFile.write(upload.buf, upload.currentSize);
      sdWriteTotalMs += millis() - writeStart;
      sdUnlock();
    }
    // Logged here (not just once at the end) specifically to answer
    // "is this progressing at all, or truly stuck?" — a fully silent
    // multi-minute upload with nothing printed here would point at
    // handleFileUpload() itself not being called repeatedly, rather
    // than just slow SD writes.
    static uint32_t chunkCount = 0;
    static unsigned long lastProgressLogMs = 0;
    chunkCount++;
    if (uploadExpectedBytes > 0) {
      uploadProgress = (float)upload.totalSize / (float)uploadExpectedBytes;
      if (uploadProgress > 1.0f) uploadProgress = 1.0f; // Content-Length includes multipart overhead the file part won't reach
    }
    if (millis() - lastProgressLogMs >= 1000) {
      lastProgressLogMs = millis();
      Serial.printf("[SoundUpload] ...%u bytes so far (%u chunks, %lums total SD write time, %.0f%%)\n",
                    (unsigned)(upload.totalSize), (unsigned)chunkCount, sdWriteTotalMs, uploadProgress * 100.0f);
      // Throttled to ~1/sec, same as the log above — a partial e-paper
      // refresh takes real time itself, so redrawing on every chunk
      // would slow the transfer down rather than just reflect it.
      updateUploadStatusPartial();
    }
  } else if (upload.status == UPLOAD_FILE_END) {
    if (uploadTargetPath.length() == 0) return; // no file was selected — see UPLOAD_FILE_START above
    if (uploadFile) {
      sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
      uploadFile.close();
      sdUnlock();
    }
    statusLine = strings().uploadComplete;
    uploadInProgress = false;
    updateUploadStatusPartial(); // see the UPLOAD_FILE_START branch above for why this can't wait for loop()
    unsigned long totalMs = millis() - uploadStartMs;
    Serial.printf("[SoundUpload] Saved %s, %u bytes in %lums (SD write time: %lums, %.1f KB/s overall)\n",
                  uploadTargetPath.c_str(), (unsigned)upload.totalSize, totalMs, sdWriteTotalMs,
                  totalMs > 0 ? (upload.totalSize / 1024.0f) / (totalMs / 1000.0f) : 0.0f);
  } else if (upload.status == UPLOAD_FILE_ABORTED) {
    if (uploadTargetPath.length() == 0) return; // no file was selected — see UPLOAD_FILE_START above
    sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
    if (uploadFile) uploadFile.close();
    // Don't leave a truncated/corrupt file behind — it would otherwise
    // sit in the /sounds/ list as if it were a real, selectable sound.
    if (SD.exists(uploadTargetPath)) SD.remove(uploadTargetPath);
    sdUnlock();
    statusLine = strings().uploadFailed;
    uploadInProgress = false;
    Serial.printf("[SoundUpload] Upload aborted, removed partial %s\n", uploadTargetPath.c_str());
    updateUploadStatusPartial();
  }
}

void handleDelete() {
  if (server.hasArg("file")) {
    String path = String(kSoundsDir) + "/" + server.arg("file");
    sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
    bool existed = SD.exists(path);
    if (existed) SD.remove(path);
    sdUnlock();
    if (existed) {
      statusLine = String(strings().uploadDeletedPrefix) + server.arg("file");
      Serial.printf("[SoundUpload] Deleted %s\n", path.c_str());
    }
  }
  server.sendHeader("Location", "/");
  server.send(303); // redirect back to the (now refreshed) file list
}

void handleRename() {
  if (server.hasArg("oldName") && server.hasArg("newName") && server.arg("newName").length() > 0) {
    String oldName = server.arg("oldName");
    String oldPath = String(kSoundsDir) + "/" + oldName;
    String sanitizedNew = sanitizeFilename(server.arg("newName"));
    // The user isn't expected to type an extension — keep the
    // original one unless they typed a name that already has one.
    if (sanitizedNew.indexOf('.') < 0) {
      int oldDot = oldName.lastIndexOf('.');
      if (oldDot >= 0) sanitizedNew += oldName.substring(oldDot);
    }
    sdLock(); // see SdCard.h — shared with Sound.cpp's background preload; uniqueTargetPath() below re-locks (recursive) safely
    bool oldExists = SD.exists(oldPath);
    sdUnlock();
    if (!oldExists) {
      statusLine = strings().uploadNotFound; 
    } else if (sanitizedNew == oldName) {
      statusLine = strings().uploadNameUnchanged;
    } else {
      // uniqueTargetPath() avoids colliding with a *different*
      // existing file, same as a fresh upload would — not relevant
      // for a no-op rename to the same name, already handled above.
      String newPath = uniqueTargetPath(sanitizedNew);
      sdLock();
      bool renamed = SD.rename(oldPath, newPath);
      sdUnlock();
      if (renamed) {
        String newName = newPath.substring(strlen(kSoundsDir) + 1);
        statusLine = String(strings().uploadRenamedToPrefix) + newName;
        Serial.printf("[SoundUpload] Renamed %s -> %s\n", oldPath.c_str(), newPath.c_str());
        // Keep the active-alarm-sound selection pointed at the same
        // file if it was the one just renamed — selection is stored
        // by filename (SettingsMenu.h), so without this a rename of
        // the currently-selected sound would silently fall back to
        // "Tone" the next time the alarm rings.
        if (getSelectedSoundFile() == oldName) {
          setSelectedSoundFile(newName);
        }
      } else {
        statusLine = strings().uploadRenameFailed;
        Serial.printf("[SoundUpload] Rename failed: %s -> %s\n", oldPath.c_str(), newPath.c_str());
      }
    }
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

void handleSelect() {
  if (server.hasArg("file")) {
    String file = server.arg("file");
    setSelectedSoundFile(file);
    statusLine = file.length() ? (String(strings().uploadSelectedPrefix) + file) : String(strings().uploadSelectedTone);
    Serial.printf("[SoundUpload] Selected sound: %s\n", file.length() ? file.c_str() : "(Tone)");
  }
  server.sendHeader("Location", "/");
  server.send(303);
}

} // namespace

void beginSoundUpload() {
  statusLine = "Waiting for a file...";
  uploadInProgress = false;
  uploadProgress = 0.0f;
  sdLock(); // see SdCard.h — shared with Sound.cpp's background preload
  if (!SD.exists(kSoundsDir)) SD.mkdir(kSoundsDir);
  sdUnlock();
  if (!handlersRegistered) {
    server.on("/", HTTP_GET, handleRoot);
    server.on("/upload", HTTP_POST, handleUploadDone, handleFileUpload);
    server.on("/delete", HTTP_POST, handleDelete);
    server.on("/rename", HTTP_POST, handleRename);
    server.on("/select", HTTP_POST, handleSelect);
    // Needed for server.header("Content-Length") in handleFileUpload()
    // — WebServer only exposes headers named here, not arbitrary ones.
    static const char *kHeadersToCollect[] = {"Content-Length"};
    server.collectHeaders(kHeadersToCollect, 1);
    handlersRegistered = true;
  }
  server.begin();
  Serial.printf("[SoundUpload] Server started at http://%s\n", WiFi.localIP().toString().c_str());
}

void handleSoundUpload() { server.handleClient(); }

void endSoundUpload() { server.stop(); }

String getSoundUploadUrl() { return "http://" + WiFi.localIP().toString(); }

String getSoundUploadStatusLine() { return statusLine; }

bool isSoundUploadInProgress() { return uploadInProgress; }

float getSoundUploadProgress() { return uploadProgress; }
