#ifndef __CLOCK_FACE_H__
#define __CLOCK_FACE_H__

// Styled clock face for Bruce: custom-drawn fonts, colors, backgrounds and
// seconds styles. Settings are saved on the device ("bruce_clock").

#include <time.h>

using ClockNowFn = struct tm (*)();

// Runs the clock until the user leaves. Same exits as Bruce's clock:
// OK/M5 returns to the caller (or the main menu if showMenuHint is false),
// Esc/hold side goes to the main menu. Next = next color, Prev = next font.
// Returns false (and draws nothing) if there isn't enough memory, so the
// caller can fall back to the plain clock.
bool clockFaceRun(bool showMenuHint, ClockNowFn now);

// Menu to pick font, colors, seconds style, date and battery display.
void clockStyleMenu();

#endif
