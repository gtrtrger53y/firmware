// Shared engine for the Bruce games. See game_common.h.
//
// StickS3 notes:
//  * During play the two buttons are read directly. Bruce's menu mapping on
//    the StickS3 (side tap = Next after a 250 ms double-tap window, side hold
//    = Esc) is right for menus but too laggy for games and can't do "hold".
//  * Hold BOTH buttons to pause, since the side button is a game control.
//  * Frames are drawn off-screen and pushed in one go, so nothing flickers.
//  * Sound uses M5Unified's speaker directly because Bruce's _tone() waits
//    for the whole beep, which would stutter the game.

#include "game_common.h"
#include <Preferences.h>
#include <math.h>

#if defined(HAS_NS4168_SPKR) && __has_include(<M5Unified.h>)
#include <M5Unified.h>
#define GAMES_SOUND_M5 1
#elif defined(BUZZ_PIN)
#define GAMES_SOUND_BUZZ 1
#endif

namespace games {

// ===========================================================================
// Colors
// ===========================================================================
uint16_t accent() { return (uint16_t)bruceConfig.priColor; }

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    const int r = ar + ((br - ar) * t) / 255;
    const int g = ag + ((bg - ag) * t) / 255;
    const int bl = ab + ((bb - ab) * t) / 255;
    return (uint16_t)((r << 11) | (g << 5) | bl);
}

uint16_t hsv(int hue, uint8_t sat, uint8_t val) {
    hue %= 360;
    if (hue < 0) hue += 360;
    const int region = hue / 60;
    const int rem = (hue % 60) * 255 / 60;
    const uint8_t p = val * (255 - sat) / 255;
    const uint8_t q = val * (255 - (sat * rem) / 255) / 255;
    const uint8_t t = val * (255 - (sat * (255 - rem)) / 255) / 255;
    switch (region) {
        case 0: return rgb(val, t, p);
        case 1: return rgb(q, val, p);
        case 2: return rgb(p, val, t);
        case 3: return rgb(p, q, val);
        case 4: return rgb(t, p, val);
        default: return rgb(val, p, q);
    }
}

// ===========================================================================
// Settings
// ===========================================================================
namespace {
const char *kPrefsNs = "bruce_games";
const char *kBestKeys[GAME_COUNT] = {"snake", "breakout", "flappy", "dino", "stack", "pong", "blaster"};
int8_t gSwapCache = -1, gSoundCache = -1;

bool prefGetBool(const char *key, bool def) {
    Preferences p;
    bool v = def;
    if (p.begin(kPrefsNs, true)) {
        v = p.getBool(key, def);
        p.end();
    }
    return v;
}
void prefPutBool(const char *key, bool v) {
    Preferences p;
    if (p.begin(kPrefsNs, false)) {
        p.putBool(key, v);
        p.end();
    }
}
} // namespace

uint32_t loadBest(GameId g) {
    if (g >= GAME_COUNT) return 0;
    Preferences p;
    if (!p.begin(kPrefsNs, true)) return 0; // namespace not created yet
    uint32_t v = p.getUInt(kBestKeys[g], 0);
    p.end();
    return v;
}

bool saveBestIfHigher(GameId g, uint32_t score) {
    if (g >= GAME_COUNT || score == 0 || score <= loadBest(g)) return false;
    Preferences p;
    if (!p.begin(kPrefsNs, false)) return false;
    p.putUInt(kBestKeys[g], score);
    p.end();
    return true;
}

// ===========================================================================
// Input
// ===========================================================================
namespace {
#ifdef GAMES_DIRECT_BUTTONS
inline bool rawA() { return digitalRead(SEL_BTN) == BTN_ACT; } // front button
inline bool rawB() { return digitalRead(DW_BTN) == BTN_ACT; }  // side button

void waitRelease() {
    uint32_t t = millis();
    while ((rawA() || rawB()) && millis() - t < 3000) {
        soundTick();
        delay(5);
    }
    delay(25); // let contacts settle
}
#endif

constexpr uint32_t kPauseHoldMs = 400;

void clearBruceInput() {
    NextPress = false;
    PrevPress = false;
    UpPress = false;
    DownPress = false;
    SelPress = false;
    EscPress = false;
    NextPagePress = false;
    PrevPagePress = false;
    AnyKeyPress = false;
}

void mergeInput(Input &acc, const Input &in) {
    acc.left = in.left;
    acc.right = in.right;
    acc.leftHit |= in.leftHit;
    acc.rightHit |= in.rightHit;
    acc.action |= in.action;
    acc.pause |= in.pause;
}
} // namespace

const char *okName() {
#if defined(GAMES_DIRECT_BUTTONS) && defined(BTN_ALIAS)
    return BTN_ALIAS;
#elif defined(GAMES_DIRECT_BUTTONS)
    return "Front";
#else
    return "OK";
#endif
}
const char *backName() {
#ifdef GAMES_DIRECT_BUTTONS
    return "Side";
#else
    return "Esc";
#endif
}
const char *leftName() {
#ifdef GAMES_DIRECT_BUTTONS
    return gamesSwapButtons() ? okName() : backName();
#else
    return "Prev";
#endif
}
const char *rightName() {
#ifdef GAMES_DIRECT_BUTTONS
    return gamesSwapButtons() ? backName() : okName();
#else
    return "Next";
#endif
}

void Pad::begin() {
    swap_ = gamesSwapButtons();
#ifdef GAMES_DIRECT_BUTTONS
    prevA_ = rawA();
    prevB_ = rawB();
    bothSince_ = 0;
    pauseLatched_ = prevA_ && prevB_;
#else
    clearBruceInput();
#endif
}

Input Pad::poll() {
    Input in;
#ifdef GAMES_DIRECT_BUTTONS
    const bool a = rawA(), b = rawB();
    const bool aHit = a && !prevA_, bHit = b && !prevB_;
    prevA_ = a;
    prevB_ = b;

    const uint32_t now = millis();
    if (a && b) {
        if (!bothSince_) bothSince_ = now;
        if (!pauseLatched_ && now - bothSince_ >= kPauseHoldMs) {
            in.pause = true;
            pauseLatched_ = true;
        }
    } else {
        bothSince_ = 0;
        if (!a && !b) pauseLatched_ = false;
    }

    // Default: front (M5) = right, side = left. "Swap L/R" flips it.
    in.right = swap_ ? b : a;
    in.left = swap_ ? a : b;
    in.rightHit = swap_ ? bHit : aHit;
    in.leftHit = swap_ ? aHit : bHit;
    in.action = aHit || bHit;
#else
    in.leftHit = check(PrevPress);
    in.rightHit = check(NextPress);
    in.left = in.leftHit;
    in.right = in.rightHit;
    in.action = check(SelPress) || in.leftHit || in.rightHit;
    in.pause = check(EscPress);
#endif
    return in;
}

Input pollUntil(Pad &pad, uint32_t deadline) {
    Input acc = pad.poll();
    soundTick();
    while ((int32_t)(deadline - millis()) > 0) {
        delay(3);
        mergeInput(acc, pad.poll());
        soundTick();
    }
    return acc;
}

int waitChoice(const std::function<void(uint32_t)> &animate) {
    uint32_t lastAnim = 0;
    auto tick = [&]() {
        soundTick();
        if (animate && millis() - lastAnim >= 40) {
            lastAnim = millis();
            animate(lastAnim);
        }
    };
#ifdef GAMES_DIRECT_BUTTONS
    uint32_t t = millis();
    while ((rawA() || rawB()) && millis() - t < 3000) {
        tick();
        delay(5);
    }
    delay(25);
    while (true) {
        tick();
        if (rawA()) {
            sfx(SFX_TICK);
            waitRelease();
            return 1;
        }
        if (rawB()) {
            sfx(SFX_TICK);
            waitRelease();
            return 2;
        }
        delay(8);
    }
#else
    clearBruceInput();
    while (true) {
        tick();
        if (check(SelPress)) return 1;
        if (check(EscPress)) return 2;
        delay(8);
    }
#endif
}

void keepAwake() { previousMillis = millis(); }

void waitMs(uint32_t ms) {
    const uint32_t t0 = millis();
    while (millis() - t0 < ms) {
        soundTick();
        delay(3);
    }
}

void returnToBruce() {
#ifdef GAMES_DIRECT_BUTTONS
    waitRelease();
    // A side-button release becomes NextPress 250 ms later (double-tap
    // window), and Bruce keeps flags ~75 ms. Wait it out, then clear.
    uint32_t t = millis();
    while (millis() - t < 350) {
        soundTick();
        delay(5);
    }
#endif
    clearBruceInput();
    tft.fillScreen(bruceConfig.bgColor);
}

// ===========================================================================
// Drawing
// ===========================================================================
Canvas::Canvas() : spr(&tft) {}

Canvas::~Canvas() {
    if (ok) spr.deleteSprite();
}

bool Canvas::begin() {
    w = tftWidth;
    h = tftHeight;
    spr.setColorDepth(16);
    ok = spr.createSprite(w, h) != nullptr;
    if (!ok) { // low on RAM: 8-bit color still looks fine
        spr.setColorDepth(8);
        ok = spr.createSprite(w, h) != nullptr;
    }
    return ok;
}

void Canvas::shake(uint8_t frames, uint8_t px) {
    shakeFrames_ = frames;
    shakePx_ = px;
}

void Canvas::push() {
    int dx = 0, dy = 0;
    if (shakeFrames_ > 0 && shakePx_ > 0) {
        dx = random(-(long)shakePx_, (long)shakePx_ + 1);
        dy = random(-(long)shakePx_, (long)shakePx_ + 1);
        shakeFrames_--;
    }
    spr.pushSprite(dx, dy);
    // Clear the strips a shifted frame leaves uncovered.
    if (dx > 0) tft.fillRect(0, 0, dx, h, C_BLACK);
    else if (dx < 0) tft.fillRect(w + dx, 0, -dx, h, C_BLACK);
    if (dy > 0) tft.fillRect(0, 0, w, dy, C_BLACK);
    else if (dy < 0) tft.fillRect(0, h + dy, w, -dy, C_BLACK);
}

void text(tft_sprite &s, const char *str, int x, int y, uint8_t size, uint16_t color, Align al) {
    const int w = (int)strlen(str) * 6 * size;
    if (al == AL_CENTER) x -= w / 2;
    else if (al == AL_RIGHT) x -= w;
    s.setTextDatum((uint8_t)0);
    s.setTextSize(size);
    s.setTextColor(color);
    s.drawString(str, x, y);
}

void textShadow(tft_sprite &s, const char *str, int x, int y, uint8_t size, uint16_t color, Align al) {
    const int off = size >= 2 ? 2 : 1;
    text(s, str, x + off, y + off, size, C_BLACK, al);
    text(s, str, x, y, size, color, al);
}

void vGradient(tft_sprite &s, int x, int y, int w, int h, uint16_t top, uint16_t bottom) {
    if (h <= 0) return;
    for (int i = 0; i < h; i += 2) {
        const uint8_t t = (uint8_t)(i * 255 / (h > 1 ? h - 1 : 1));
        s.fillRect(x, y + i, w, (i + 2 <= h) ? 2 : 1, blend(top, bottom, t));
    }
}

void panel(Canvas &c, int pw, int ph, uint16_t edge) {
    const int x = (c.w - pw) / 2, y = (c.h - ph) / 2;
    const uint16_t fill = rgb(10, 12, 18);
    c.spr.fillRoundRect(x, y, pw, ph, 8, fill);
    c.spr.drawRoundRect(x, y, pw, ph, 8, edge);
    c.spr.drawRoundRect(x + 2, y + 2, pw - 4, ph - 4, 6, darker(edge, 150));
}

// ===========================================================================
// Effects
// ===========================================================================
void Particles::burst(float x, float y, int count, uint16_t color, float speed, uint8_t life) {
    for (int i = 0; i < count && n_ < kMax; i++) {
        const float ang = random(628) / 100.0f;
        const float sp = speed * (0.3f + random(70) / 100.0f);
        P &p = p_[n_++];
        p.x = x;
        p.y = y;
        p.vx = cosf(ang) * sp;
        p.vy = sinf(ang) * sp - speed * 0.25f;
        p.color = color;
        p.maxLife = (uint8_t)max(4, (int)(life * (0.6f + random(40) / 100.0f)));
        p.life = p.maxLife;
    }
}

void Particles::update(float gravity) {
    for (int i = 0; i < n_;) {
        P &p = p_[i];
        p.x += p.vx;
        p.y += p.vy;
        p.vy += gravity;
        p.vx *= 0.97f;
        if (--p.life == 0) {
            p_[i] = p_[--n_];
            continue;
        }
        i++;
    }
}

void Particles::draw(tft_sprite &s) const {
    for (int i = 0; i < n_; i++) {
        const P &p = p_[i];
        const uint8_t fade = (uint8_t)(255 - (p.life * 255) / p.maxLife);
        const uint16_t col = blend(p.color, C_BLACK, fade / 2);
        const int sz = p.life * 2 > p.maxLife ? 2 : 1;
        s.fillRect((int)p.x, (int)p.y, sz, sz, col);
    }
}

void Popups::add(int x, int y, const char *txt, uint16_t color) {
    if (n_ >= kMax) {
        for (int i = 1; i < kMax; i++) p_[i - 1] = p_[i];
        n_ = kMax - 1;
    }
    P &p = p_[n_++];
    p.x = x;
    p.y = y;
    strncpy(p.txt, txt, sizeof(p.txt) - 1);
    p.txt[sizeof(p.txt) - 1] = 0;
    p.color = color;
    p.life = 30;
}

void Popups::update() {
    for (int i = 0; i < n_;) {
        p_[i].y -= 0.6f;
        if (--p_[i].life == 0) {
            p_[i] = p_[--n_];
            continue;
        }
        i++;
    }
}

void Popups::draw(tft_sprite &s) const {
    for (int i = 0; i < n_; i++) {
        if (p_[i].life < 4 && (p_[i].life & 1)) continue; // flicker out
        // Keep the label on screen even when it pops up at an edge.
        const int half = (int)strlen(p_[i].txt) * 3 + 2;
        const int x = constrain((int)p_[i].x, half, (int)s.width() - half);
        textShadow(s, p_[i].txt, x, (int)p_[i].y, 1, p_[i].color, AL_CENTER);
    }
}

// ===========================================================================
// Sound
// ===========================================================================
namespace {
struct Note {
    uint16_t f, ms;
};
Note gQueue[6];
uint8_t gQLen = 0, gQPos = 0;
uint32_t gNextNote = 0;

#if defined(GAMES_SOUND_M5) || defined(GAMES_SOUND_BUZZ)
void playNote(uint16_t f, uint16_t ms) {
    if (f == 0) return; // rest
#if defined(GAMES_SOUND_M5)
    const int vol = constrain(bruceConfig.soundVolume, 0, 100) * 150 / 100; // a bit quieter than UI
    M5.Speaker.setVolume((uint8_t)vol);
    M5.Speaker.tone(f, ms);
#else
    tone(BUZZ_PIN, f, ms);
#endif
}
#endif
} // namespace

void sfx(Sfx s) {
#if defined(GAMES_SOUND_M5) || defined(GAMES_SOUND_BUZZ)
    if (!gamesSoundOn() || !bruceConfig.soundEnabled) return;
    gQLen = 0;
    gQPos = 0;
    auto add = [](uint16_t f, uint16_t ms) {
        if (gQLen < 6) gQueue[gQLen++] = {f, ms};
    };
    switch (s) {
        case SFX_TICK: add(1500, 12); break;
        case SFX_EAT: add(880, 30); add(1320, 40); break;
        case SFX_BONUS: add(1047, 40); add(1319, 40); add(1568, 70); break;
        case SFX_BOUNCE: add(660, 18); break;
        case SFX_BRICK: add((uint16_t)(900 + random(400)), 25); break;
        case SFX_FLAP: add(520, 25); break;
        case SFX_SCORE: add(1568, 30); add(2093, 50); break;
        case SFX_HIT: add(330, 60); add(220, 90); break;
        case SFX_DIE: add(523, 90); add(392, 90); add(330, 90); add(262, 200); break;
        case SFX_POWER: add(660, 40); add(880, 40); add(1320, 70); break;
        case SFX_LEVEL: add(523, 70); add(659, 70); add(784, 70); add(1047, 160); break;
        case SFX_SHOOT: add(1700, 12); break;
        case SFX_BOOM: add(180, 50); add(120, 70); break;
        case SFX_PERFECT: add(1319, 35); add(1760, 60); break;
        case SFX_JUMP: add(600, 20); add(900, 30); break;
    }
    gNextNote = millis();
    soundTick();
#else
    (void)s;
#endif
}

void soundTick() {
#if defined(GAMES_SOUND_M5) || defined(GAMES_SOUND_BUZZ)
    if (gQPos < gQLen && (int32_t)(millis() - gNextNote) >= 0) {
        const Note n = gQueue[gQPos++];
        playNote(n.f, n.ms);
        gNextNote = millis() + n.ms;
    }
#endif
}

void soundStop() {
    gQLen = 0;
    gQPos = 0;
#if defined(GAMES_SOUND_M5)
    M5.Speaker.stop();
#elif defined(GAMES_SOUND_BUZZ)
    noTone(BUZZ_PIN);
#endif
}

// ===========================================================================
// Screens
// ===========================================================================
namespace {
bool titleScreen(Canvas &c, const GameInfo &info) {
    char l1[40] = "", l2[40] = "", buf[48];
    info.controls(l1, l2, sizeof(l1));
    const uint32_t best = loadBest(info.id);
    const int box = 64;
    const int tx = 8 + box + 10;

    auto draw = [&](uint32_t t) {
        tft_sprite &s = c.spr;
        vGradient(s, 0, 0, c.w, c.h, blend(info.color, C_BLACK, 190), C_BLACK);

        s.fillRoundRect(8, 10, box, box, 10, rgb(8, 10, 16));
        s.drawRoundRect(8, 10, box, box, 10, info.color);
        info.icon(s, 8 + box / 2, 10 + box / 2, t);

        textShadow(s, info.title, tx, 10, 2, info.color);
        text(s, info.goal, tx, 32, 1, C_WHITE);
        text(s, l1, tx, 46, 1, rgb(200, 200, 200));
        if (l2[0]) text(s, l2, tx, 56, 1, rgb(200, 200, 200));
#ifdef GAMES_DIRECT_BUTTONS
        text(s, "Hold both: pause", tx, 68, 1, C_GREY);
#else
        text(s, "Esc: pause", tx, 68, 1, C_GREY);
#endif
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)best);
        textShadow(s, buf, tx, 84, 1, C_GOLD);

        // Pulsing play prompt.
        snprintf(buf, sizeof(buf), "%s: play    %s: back", okName(), backName());
        const int pw = (int)strlen(buf) * 6 + 16, py = c.h - 20;
        const uint8_t pulse = (uint8_t)(127 + 127 * sinf(t / 180.0f));
        s.fillRoundRect((c.w - pw) / 2, py, pw, 15, 7, blend(rgb(20, 22, 30), info.color, pulse / 4));
        text(s, buf, c.w / 2, py + 4, 1, blend(C_GREY, C_WHITE, pulse), AL_CENTER);
        c.push();
    };
    draw(millis());
    return waitChoice(draw) == 1;
}

bool gameOverScreen(Canvas &c, const GameInfo &info, const GameResult &r) {
    const bool newBest = saveBestIfHigher(info.id, r.score);
    const uint32_t best = loadBest(info.id);
    char buf[40];
    c.shake(0, 0);

    const int pw = min(180, c.w - 16), ph = 112;
    const int y0 = (c.h - ph) / 2, cx = c.w / 2;
    auto draw = [&](uint32_t t) {
        tft_sprite &s = c.spr;
        panel(c, pw, ph, info.color);
        textShadow(s, r.won ? "YOU WIN!" : "GAME OVER", cx, y0 + 8, 2, r.won ? C_GOLD : C_RED, AL_CENTER);
        text(s, "SCORE", cx, y0 + 30, 1, C_GREY, AL_CENTER);
        snprintf(buf, sizeof(buf), "%lu", (unsigned long)r.score);
        textShadow(s, buf, cx, y0 + 40, 3, C_WHITE, AL_CENTER);
        if (newBest) {
            textShadow(s, "NEW BEST!", cx, y0 + 68, 1, hsv((int)(t / 4)), AL_CENTER);
        } else {
            snprintf(buf, sizeof(buf), "Best %lu", (unsigned long)best);
            text(s, buf, cx, y0 + 68, 1, C_GOLD, AL_CENTER);
        }
        if (r.note) text(s, r.note, cx, y0 + 80, 1, r.noteColor, AL_CENTER);
        snprintf(buf, sizeof(buf), "%s: again   %s: back", okName(), backName());
        text(s, buf, cx, y0 + ph - 14, 1, C_GREY, AL_CENTER);
        c.push();
    };
    draw(millis());
    if (newBest) sfx(SFX_LEVEL);
    // A press that was already on its way shouldn't count.
    const uint32_t t0 = millis();
    while (millis() - t0 < 500) {
        soundTick();
        delay(5);
    }
    return waitChoice(newBest ? std::function<void(uint32_t)>(draw) : nullptr) == 1;
}
} // namespace

bool pauseScreen(Canvas &c) {
    char buf[32];
    c.shake(0, 0);
    panel(c, 150, 60, accent());
    textShadow(c.spr, "PAUSED", c.w / 2, c.h / 2 - 22, 2, accent(), AL_CENTER);
    snprintf(buf, sizeof(buf), "%s: resume", okName());
    text(c.spr, buf, c.w / 2, c.h / 2 + 2, 1, C_WHITE, AL_CENTER);
    snprintf(buf, sizeof(buf), "%s: quit", backName());
    text(c.spr, buf, c.w / 2, c.h / 2 + 14, 1, C_GREY, AL_CENTER);
    c.push();
    soundStop();
    return waitChoice() == 1;
}

void runGame(const GameInfo &info) {
    Canvas canvas;
    if (!canvas.begin()) {
        returnToBruce();
        displayError("Not enough memory", true);
        return;
    }
    Pad pad;
    if (titleScreen(canvas, info)) {
        while (true) {
            pad.begin();
            const GameResult r = info.run(canvas, pad);
            if (r.quit) break;
            if (!gameOverScreen(canvas, info, r)) break;
        }
    }
    returnToBruce();
}

} // namespace games

// ===========================================================================
// Public API
// ===========================================================================
namespace {
const games::GameInfo *const kGames[GAME_COUNT] = {
    &games::kSnake,
    &games::kBreakout,
    &games::kFlappy,
    &games::kDino,
    &games::kStack,
    &games::kPong,
    &games::kBlaster,
};
const char *const kNames[GAME_COUNT] = {"Snake", "Breakout", "Flappy", "Dino Run", "Stack", "Pong", "Blaster"};
} // namespace

void playGame(GameId id) {
    if (id < GAME_COUNT) games::runGame(*kGames[id]);
}

const char *gameName(GameId id) { return id < GAME_COUNT ? kNames[id] : ""; }

uint32_t gamesBestScore(GameId game) { return games::loadBest(game); }

bool gamesHasDirectButtons() {
#ifdef GAMES_DIRECT_BUTTONS
    return true;
#else
    return false;
#endif
}

bool gamesSwapButtons() {
    if (games::gSwapCache < 0) games::gSwapCache = games::prefGetBool("swap", false) ? 1 : 0;
    return games::gSwapCache == 1;
}

void gamesSetSwapButtons(bool swap) {
    games::gSwapCache = swap ? 1 : 0;
    games::prefPutBool("swap", swap);
}

bool gamesSoundOn() {
    if (games::gSoundCache < 0) games::gSoundCache = games::prefGetBool("sound", true) ? 1 : 0;
    return games::gSoundCache == 1;
}

void gamesSetSoundOn(bool on) {
    games::gSoundCache = on ? 1 : 0;
    games::prefPutBool("sound", on);
}
