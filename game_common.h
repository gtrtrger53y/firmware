#ifndef __BRUCE_GAME_COMMON_H__
#define __BRUCE_GAME_COMMON_H__

// Shared engine for the Bruce games: input, drawing, effects, sound, screens.

#include "core/display.h"
#include "games.h"
#include <functional>
#include <globals.h>

#if defined(SEL_BTN) && defined(DW_BTN) && defined(BTN_ACT)
#define GAMES_DIRECT_BUTTONS 1
#endif

namespace games {

// ---------------------------------------------------------------------------
// Colors (RGB565). Always uint16_t: on M5GFX boards the sprite's text and
// outline calls go straight to LovyanGFX, which picks the color format from
// the C++ type.
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BLACK = 0x0000;
constexpr uint16_t C_WHITE = 0xFFFF;
constexpr uint16_t C_GREY = rgb(130, 130, 130);
constexpr uint16_t C_DGREY = rgb(50, 52, 56);
constexpr uint16_t C_RED = rgb(255, 40, 40);
constexpr uint16_t C_ORANGE = rgb(255, 150, 0);
constexpr uint16_t C_YELLOW = rgb(255, 230, 0);
constexpr uint16_t C_GREEN = rgb(40, 230, 60);
constexpr uint16_t C_CYAN = rgb(0, 220, 255);
constexpr uint16_t C_BLUE = rgb(50, 110, 255);
constexpr uint16_t C_PURPLE = rgb(170, 70, 255);
constexpr uint16_t C_PINK = rgb(255, 90, 180);
constexpr uint16_t C_GOLD = rgb(255, 200, 40);

uint16_t accent();                                  // Bruce theme color
uint16_t blend(uint16_t a, uint16_t b, uint8_t t);  // t=0 -> a, t=255 -> b
uint16_t hsv(int hue, uint8_t sat = 255, uint8_t val = 255);
inline uint16_t darker(uint16_t c, uint8_t amt = 110) { return blend(c, C_BLACK, amt); }
inline uint16_t lighter(uint16_t c, uint8_t amt = 90) { return blend(c, C_WHITE, amt); }

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------
struct Input {
    bool left = false, right = false;       // held right now
    bool leftHit = false, rightHit = false; // newly pressed
    bool action = false;                    // any button newly pressed
    bool pause = false;                     // pause gesture
};

class Pad {
public:
    void begin();
    Input poll();

private:
    bool swap_ = false;
    bool prevA_ = false, prevB_ = false;
    bool pauseLatched_ = false;
    uint32_t bothSince_ = 0;
};

// Wait until `deadline` while sampling buttons (and the sound queue) every
// few ms, so short taps between frames are never lost.
Input pollUntil(Pad &pad, uint32_t deadline);

// Waits for a fresh press: 1 = OK (front / Sel), 2 = Back (side / Esc).
// `animate` (optional) is called about every 40 ms with millis().
int waitChoice(const std::function<void(uint32_t)> &animate = nullptr);

void keepAwake();      // stop the screen dimming mid-game
void waitMs(uint32_t ms); // delay that keeps queued sounds playing
void returnToBruce();  // hand screen + buttons back to Bruce's menus

const char *okName();    // "M5" on the StickS3
const char *backName();  // "Side"
const char *leftName();  // honours Swap L/R
const char *rightName();

// ---------------------------------------------------------------------------
// Drawing
// ---------------------------------------------------------------------------
class Canvas {
public:
    Canvas();
    ~Canvas();
    bool begin();
    void push();                          // to screen, with shake if active
    void shake(uint8_t frames, uint8_t px);

    tft_sprite spr;
    int w = 0, h = 0;
    bool ok = false;

private:
    uint8_t shakeFrames_ = 0, shakePx_ = 0;
};

enum Align : uint8_t { AL_LEFT, AL_CENTER, AL_RIGHT };

// Built-in 6x8 font, placed by hand (datum numbers differ between backends).
void text(tft_sprite &s, const char *str, int x, int y, uint8_t size, uint16_t color, Align al = AL_LEFT);
void textShadow(tft_sprite &s, const char *str, int x, int y, uint8_t size, uint16_t color, Align al = AL_LEFT);
void vGradient(tft_sprite &s, int x, int y, int w, int h, uint16_t top, uint16_t bottom);
void panel(Canvas &c, int pw, int ph, uint16_t edge);

// ---------------------------------------------------------------------------
// Effects
// ---------------------------------------------------------------------------
class Particles {
public:
    void clear() { n_ = 0; }
    void burst(float x, float y, int count, uint16_t color, float speed = 2.0f, uint8_t life = 20);
    void update(float gravity = 0.08f);
    void draw(tft_sprite &s) const;
    bool active() const { return n_ > 0; }

private:
    struct P {
        float x, y, vx, vy;
        uint16_t color;
        uint8_t life, maxLife;
    };
    static constexpr int kMax = 96;
    P p_[kMax];
    int n_ = 0;
};

// Floating "+10" style labels.
class Popups {
public:
    void clear() { n_ = 0; }
    void add(int x, int y, const char *txt, uint16_t color);
    void update();
    void draw(tft_sprite &s) const;

private:
    struct P {
        float x, y;
        char txt[12];
        uint16_t color;
        uint8_t life;
    };
    static constexpr int kMax = 6;
    P p_[kMax];
    int n_ = 0;
};

// ---------------------------------------------------------------------------
// Sound (non-blocking: notes are queued and played by soundTick()).
// ---------------------------------------------------------------------------
enum Sfx : uint8_t {
    SFX_TICK,
    SFX_EAT,
    SFX_BONUS,
    SFX_BOUNCE,
    SFX_BRICK,
    SFX_FLAP,
    SFX_SCORE,
    SFX_HIT,
    SFX_DIE,
    SFX_POWER,
    SFX_LEVEL,
    SFX_SHOOT,
    SFX_BOOM,
    SFX_PERFECT,
    SFX_JUMP,
};
void sfx(Sfx s);
void soundTick();
void soundStop();

// ---------------------------------------------------------------------------
// Scores + running a game
// ---------------------------------------------------------------------------
uint32_t loadBest(GameId g);
bool saveBestIfHigher(GameId g, uint32_t score);

struct GameResult {
    uint32_t score = 0;
    bool quit = false;           // left from the pause screen
    bool won = false;
    const char *note = nullptr;  // extra line on the game-over card
    uint16_t noteColor = C_YELLOW;
};

struct GameInfo {
    GameId id;
    const char *title;
    uint16_t color;
    const char *goal;                                          // one short line
    void (*controls)(char *l1, char *l2, size_t n);            // two control lines
    void (*icon)(tft_sprite &s, int cx, int cy, uint32_t t);   // animated title icon
    GameResult (*run)(Canvas &c, Pad &pad);
};

void runGame(const GameInfo &info);

// Pause overlay. Returns false if the player chose to quit.
bool pauseScreen(Canvas &c);

// Games, each in its own file.
extern const GameInfo kSnake, kBreakout, kFlappy, kDino, kStack, kPong, kBlaster;

} // namespace games

#endif
