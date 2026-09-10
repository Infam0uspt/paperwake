#pragma once

void initControls();

// rotateConfPressed()/menuButtonPressed()/exitButtonPressed() are
// edge-triggered and consumed once per call: each returns true at most
// once per physical press, regardless of how often the function is
// polled.
bool rotateConfPressed();
bool menuButtonPressed();
bool exitButtonPressed();

// Net number of UP/DOWN steps accumulated since the last call (0 if
// none pending) — from either the rotate-switch (auto-repeats while
// held, one step at a time) or the loose EC11 encoder (can accumulate
// more than one step between two calls). Steps are counted the moment
// they physically happen — the encoder's quadrature decode runs in a
// hardware interrupt, unaffected by how long the caller takes between
// calls — and never dropped: if e.g. an e-paper partial refresh blocks
// the caller for a few hundred ms while the dial keeps turning, the
// next call simply returns however many steps piled up during that
// window instead of just the first one. Callers that apply each step
// at a fixed size (e.g. 1 minute) multiply by the returned count to
// apply the whole batch in one go, refreshing the display once with
// the final value rather than once per individual step.
int rotateUpSteps();
int rotateDownSteps();

// Fires once per physical hold, when EXIT has been held continuously
// past a long-press threshold (~800ms) — independent of, and in
// addition to, exitButtonPressed() (which already fired on the press
// edge, before it's known whether the hold will turn out to be long).
// Used in Mode::CLOCK to toggle the wake-up light as a night light.
bool exitButtonLongPressed();

// Same long-press mechanism as exitButtonLongPressed() above, for the
// MENU button (and its external "Alarm on/off" twin). Used in
// Mode::CLOCK to force the WiFi config portal (AP + captive portal)
// even when the clock already has a working connection — the way to
// e.g. move the clock to a new network without reflashing. Note the
// press edge (menuButtonPressed()) still fires first for the same
// physical hold, like EXIT's press edge does.
bool menuButtonLongPressed();

// Fires once on release, but ONLY if that hold never reached the
// long-press threshold above (i.e. mutually exclusive with
// exitButtonLongPressed() for the same physical press) — used in
// Mode::CLOCK to trigger the frontlight on a plain short tap. Waiting
// for release (rather than firing on the press edge, like
// exitButtonPressed() does) is what makes a short tap and a long hold
// never both fire for the same press.
bool exitButtonShortReleased();

// Valid only right after rotateUpSteps()/rotateDownSteps() just
// returned nonzero: was the most recently folded-in step a "fast" one
// (sustained rotate-switch hold, or a quickly-turned encoder detent)?
// Currently unused — superseded by rotateUpSteps()/rotateDownSteps()'s
// own returned step count, which is a more direct signal of how fast
// the dial is turning than this binary fast/slow flag ever was. Left
// in place rather than removed (deliberately not touching more
// encoder-adjacent code than necessary — see git history for past bugs
// here).
bool rotateStepWasFast();
