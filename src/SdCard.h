#pragma once

// Mounts the CrowPanel's built-in microSD slot once at boot. After
// this, both Sound.cpp (reading an uploaded alarm sound) and
// SoundUpload.cpp (writing one) just use the SD library's global `SD`
// object directly — no need to pass anything around.
//
// Uses its own dedicated SPI bus (separate from the default `SPI`
// object, which the e-paper display already claims in
// EpaperDisplay.cpp's initDisplay() — reusing it for the SD card would
// conflict with the display's pin configuration).
bool initSdCard();

// Guards direct SD/File API calls made from more than one FreeRTOS
// task. Needed since Sound.cpp's background audio-preload task can now
// touch the SD card at any time playback isn't actively ringing (not
// just during a ringing session, which was the old, now-outdated
// assumption) — meaning it's no longer guaranteed mutually exclusive
// in time with main.cpp's own SD use (SettingsMenu.cpp's SoundChoice
// directory scan, SoundUpload.cpp's transfers/deletes). The Arduino SD
// library isn't safe to call concurrently from two tasks. Call
// sdLock() immediately before and sdUnlock() immediately after any
// block of direct SD/File calls — keep the locked region as short as
// possible, since the audio task's own smoothness depends on never
// waiting long for this. Backed by a *recursive* mutex specifically so
// a function that already holds the lock can safely call another
// function that also locks (e.g. SoundUpload.cpp's handleFileUpload()
// and handleRename() both eventually call uniqueTargetPath(), which
// locks on its own) without deadlocking itself — same task re-locking
// is fine as long as sdUnlock() is called the same number of times.
void sdLock();
void sdUnlock();

// Non-blocking variant: returns true and takes the lock (caller must
// still sdUnlock() it) if it was immediately free, or false without
// waiting at all if it wasn't. Used where a caller wants to know
// *whether* it would have to wait, e.g. to show a loading indicator
// only when a wait is actually happening (see main.cpp's Settings
// "Sound" row handling) rather than on every entry.
bool sdTryLock();
