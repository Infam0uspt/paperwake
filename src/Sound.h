#pragma once

// Alarm sound via the MAX98357A I2S amp (Stap 2). Plays an uploaded
// /sounds/<file> (WAV/MP3, see SettingsMenu.h's "Sound" item) or, if
// "Tone" is selected (or the selected file is missing), a short
// embedded fallback clip (include/FallbackTone.h). See "Volume" for
// the amplitude control.
//
// The actual decode/feed work runs on its own dedicated FreeRTOS task
// (Sound.cpp's audioTaskFn(), same core as Arduino's loopTask, one
// priority level above it — see the comment at its
// xTaskCreatePinnedToCore() call for the full reasoning), independent
// of main.cpp's loop() — this is what lets alarm audio keep playing
// smoothly even while loop() is stuck inside a blocking e-paper
// refresh. That same task also keeps whatever's currently selected
// preloaded and paused in the background whenever nothing's ringing —
// see invalidatePreloadedSound() below — so starting an alarm never
// needs to touch the SD card on the critical path.

void initSound();

// Call on every loop() iteration, regardless of mode. Just flips a
// flag the audio task reads (cheap, non-blocking, near-instant) —
// actual playback start/stop happens on that task within a couple ms.
void updateAlarmSound(bool playing);

// True once the currently-starting/current playback session has real
// decoded audio in the I2S DMA queue (not just "a session object
// exists") — see Sound.cpp's audioTaskFn for why this distinction
// matters. Used by main.cpp's enterRinging() to avoid starting its own
// CPU-heavy screen refresh before sound has actually, audibly started.
bool isAudioAudible();

// Call whenever the selected sound (Settings' "Sound" item) or the set
// of files on the SD card (upload/delete) may have changed. Tells the
// background preload (see initSound()) to reload on its next idle
// cycle rather than keep serving a now-stale preloaded session.
void invalidatePreloadedSound();
