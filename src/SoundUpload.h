#pragma once

#include <Arduino.h>

// "Upload sound" (System tab, Action-type item): a tiny web page,
// served over the existing home-WiFi connection (no separate access
// point), for uploading a .wav file that overwrites /alarm.wav on the
// SD card. Sound.cpp plays that file during ringing if present,
// falling back to the generated tone otherwise.

// Starts the web server. Call once when entering Mode::UPLOAD.
void beginSoundUpload();

// Call on every loop() iteration while in Mode::UPLOAD.
void handleSoundUpload();

// Stops the web server. Call when leaving Mode::UPLOAD (EXIT pressed).
void endSoundUpload();

// For the upload screen: the URL to browse to, and a short status
// line ("Waiting for a file..." / "Uploading" / "Upload complete").
String getSoundUploadUrl();
String getSoundUploadStatusLine();

// True from the moment a file starts uploading until it finishes (or
// fails) — drives the progress bar shown under the status line on the
// upload screen (EpaperDisplay.cpp).
bool isSoundUploadInProgress();

// 0..1 — how far the current upload has progressed, estimated from the
// request's Content-Length header vs. bytes received so far. 0 if
// unknown (e.g. the browser didn't send Content-Length).
float getSoundUploadProgress();
