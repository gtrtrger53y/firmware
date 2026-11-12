#ifndef __BRUCE_GAMES_H__
#define __BRUCE_GAMES_H__

// Bruce "Games" module. Pure entertainment, no radios or HID.
// Tuned for the M5StickS3 (and pin-compatible clones), works on other boards
// with Bruce's normal buttons.

#include <Arduino.h>

enum GameId : uint8_t {
    GAME_SNAKE = 0,
    GAME_BREAKOUT,
    GAME_FLAPPY,
    GAME_DINO,
    GAME_STACK,
    GAME_PONG,
    GAME_BLASTER,
    GAME_COUNT
};

void playGame(GameId id);
const char *gameName(GameId id);
uint32_t gamesBestScore(GameId game);

// True when the games read the two StickS3 buttons directly (hold + instant
// response). False on boards that fall back to Bruce's Prev/Next/Sel/Esc.
bool gamesHasDirectButtons();

// Which physical button means "left/up" in the games.
bool gamesSwapButtons();
void gamesSetSwapButtons(bool swap);

// Game sound effects (also muted when Bruce's own sound is off).
bool gamesSoundOn();
void gamesSetSoundOn(bool on);

#endif
