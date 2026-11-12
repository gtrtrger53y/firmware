// FLAPPY: any button flaps. The sky turns to sunset at 10 and night at 20.
// Medals: bronze 10, silver 20, gold 30, platinum 50.

#include "game_common.h"
#include <math.h>

namespace games {
namespace {

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "Tap either button: flap");
#else
    snprintf(l1, n, "Any button: flap");
#endif
    snprintf(l2, n, "Medals at 10/20/30/50");
}

void drawBird(tft_sprite &s, int x, int y, int r, bool wingUp) {
    s.fillCircle(x, y, r, C_YELLOW);
    s.fillCircle(x - 1, y + 1, r - 2, rgb(255, 200, 0));
    s.fillRect(x - r, wingUp ? y - 2 : y + 1, r, 2, C_ORANGE);
    s.fillCircle(x + r / 2, y - r / 3, r / 3 + 1, C_WHITE);
    s.drawPixel(x + r / 2 + 1, y - r / 3, C_BLACK);
    s.fillTriangle(x + r - 1, y, x + r + 4, y + 1, x + r - 1, y + 3, C_ORANGE);
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    s.fillRect(cx + 10, cy - 30, 12, 18, C_GREEN);
    s.fillRect(cx + 8, cy - 14, 16, 4, rgb(0, 120, 0));
    s.fillRect(cx + 10, cy + 12, 12, 18, C_GREEN);
    s.fillRect(cx + 8, cy + 12, 16, 4, rgb(0, 120, 0));
    const int y = cy + (int)(6 * sinf(t / 160.0f));
    drawBird(s, cx - 12, y, 6, (t / 120) % 2);
}

struct Sky {
    uint16_t top, bottom, hill;
};
const Sky kDay = {rgb(70, 165, 230), rgb(175, 225, 245), rgb(60, 150, 80)};
const Sky kSunset = {rgb(60, 50, 130), rgb(250, 140, 80), rgb(70, 60, 90)};
const Sky kNight = {rgb(4, 8, 30), rgb(30, 40, 90), rgb(15, 30, 45)};

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int GROUND = 10;
    const int PLAY_H = H - GROUND;
    const int PIPE_W = 18;
    const int SPACING = W / 2 + 12;
    const int BIRD_X = W / 4;
    const int BR = 5;
    const float GRAV = 0.25f, FLAP = -3.2f, MAX_FALL = 4.5f;
    const uint32_t FRAME_MS = 25;
    const uint16_t pipeBody = rgb(70, 190, 40), pipeDark = rgb(30, 110, 20), pipeLight = rgb(170, 240, 120);

    struct Pipe {
        float x;
        int gapY; // top of the gap
        bool scored;
    };
    Pipe pipes[3];
    float by = PLAY_H / 2.0f, vy = 0, scroll = 0;
    bool started = false, dead = false;
    uint32_t frame = 0;
    Sky sky = kDay;
    Particles fx;
    GameResult res;
    char buf[16];

    auto gapSize = [&]() {
        const int g = (int)(PLAY_H * 0.40f) - (int)(res.score / 3);
        return max(g, (int)(PLAY_H * 0.30f));
    };
    auto speed = [&]() { return 2.0f + min(res.score, (uint32_t)30) * 0.03f; };
    auto newGapY = [&]() { return (int)random(10, max(11, PLAY_H - gapSize() - 10)); };
    for (int i = 0; i < 3; i++) pipes[i] = {(float)(W + 30 + i * SPACING), newGapY(), false};

    auto render = [&]() {
        // Ease the sky toward the palette for the current score.
        const Sky &target = res.score >= 20 ? kNight : (res.score >= 10 ? kSunset : kDay);
        sky.top = blend(sky.top, target.top, 10);
        sky.bottom = blend(sky.bottom, target.bottom, 10);
        sky.hill = blend(sky.hill, target.hill, 10);
        vGradient(s, 0, 0, W, PLAY_H, sky.top, sky.bottom);

        if (res.score >= 18) { // stars come out
            for (int i = 0; i < 24; i++) {
                const int sx0 = (i * 97 + 13) % W, sy0 = (i * 53 + 7) % (PLAY_H / 2);
                if ((frame / 8 + i) % 7) s.drawPixel(sx0, sy0, C_WHITE);
            }
        } else {
            for (int i = 0; i < 2; i++) { // slow clouds
                const int span = W + 60;
                const int cxp = ((int)(i * W / 2 + 40 - scroll * 0.2f) % span + span) % span - 30;
                const int cyp = 16 + i * 20;
                const uint16_t cc = blend(C_WHITE, sky.top, 60);
                s.fillCircle(cxp, cyp, 7, cc);
                s.fillCircle(cxp + 9, cyp - 3, 9, cc);
                s.fillCircle(cxp + 19, cyp, 6, cc);
            }
        }

        // Far hills scroll slower than the pipes.
        for (int i = -1; i < W / 60 + 2; i++) {
            const int hx = i * 60 - ((int)(scroll * 0.3f) % 60);
            s.fillCircle(hx, PLAY_H + 14, 34, sky.hill);
        }

        const int gap = gapSize();
        for (const Pipe &p : pipes) {
            const int x = (int)p.x;
            if (x > W || x + PIPE_W < -4) continue;
            const int by2 = p.gapY + gap;
            s.fillRect(x, 0, PIPE_W, p.gapY, pipeBody);
            s.fillRect(x + 3, 0, 3, p.gapY, pipeLight);
            s.fillRect(x + PIPE_W - 3, 0, 3, p.gapY, pipeDark);
            s.fillRect(x - 2, p.gapY - 7, PIPE_W + 4, 7, pipeBody);
            s.drawRect(x - 2, p.gapY - 7, PIPE_W + 4, 7, pipeDark);
            s.fillRect(x, by2, PIPE_W, PLAY_H - by2, pipeBody);
            s.fillRect(x + 3, by2, 3, PLAY_H - by2, pipeLight);
            s.fillRect(x + PIPE_W - 3, by2, 3, PLAY_H - by2, pipeDark);
            s.fillRect(x - 2, by2, PIPE_W + 4, 7, pipeBody);
            s.drawRect(x - 2, by2, PIPE_W + 4, 7, pipeDark);
        }

        s.fillRect(0, PLAY_H, W, GROUND, rgb(220, 190, 110));
        s.fillRect(0, PLAY_H, W, 3, rgb(90, 200, 60));
        for (int x = -((int)scroll % 12); x < W; x += 12) s.fillRect(x, PLAY_H + 5, 6, 2, rgb(190, 150, 80));

        if (!dead || (frame / 2) % 2) drawBird(s, BIRD_X, (int)by, BR, vy < 0 && (frame / 3) % 2);
        fx.draw(s);

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)res.score);
        textShadow(s, buf, W / 2, 6, 3, C_WHITE, AL_CENTER);
        if (!started) textShadow(s, "Tap to flap", W / 2, PLAY_H / 2 + 20, 1, C_WHITE, AL_CENTER);
        c.push();
    };

    uint32_t frameStart = millis();
    while (true) {
        if (started) keepAwake(); // idle on "Tap to flap" may dim like any Bruce screen
        const Input in = pollUntil(pad, frameStart + FRAME_MS);
        frameStart = millis();
        frame++;

        if (in.pause) {
            if (!pauseScreen(c)) {
                res.quit = true;
                return res;
            }
            pad.begin();
            frameStart = millis();
            continue;
        }

        if (in.action) {
            started = true;
            vy = FLAP;
            sfx(SFX_FLAP);
        }

        if (!started) { // hover until the first flap
            by = PLAY_H / 2.0f + sinf(frame * 0.15f) * 3.0f;
            scroll += 1.0f;
            render();
            continue;
        }

        vy = fminf(vy + GRAV, MAX_FALL);
        by += vy;
        if (by - BR < 0) {
            by = BR;
            vy = 0;
        }

        const float sp = speed();
        scroll += sp;
        bool hit = by + BR >= PLAY_H;

        float furthest = 0;
        for (const Pipe &p : pipes) furthest = fmaxf(furthest, p.x);
        const int gap = gapSize();
        for (Pipe &p : pipes) {
            p.x -= sp;
            if (p.x + PIPE_W < -4) { // recycle off the left edge
                p.x = furthest + SPACING;
                p.gapY = newGapY();
                p.scored = false;
                furthest = p.x;
            }
            // Box test, slightly forgiving.
            const bool overlapX = BIRD_X + BR - 1 > p.x - 2 && BIRD_X - BR + 1 < p.x + PIPE_W + 2;
            if (overlapX && (by - BR + 2 < p.gapY || by + BR - 2 > p.gapY + gap)) hit = true;
            if (!p.scored && p.x + PIPE_W < BIRD_X - BR) {
                p.scored = true;
                res.score++;
                sfx(SFX_SCORE);
            }
        }
        fx.update(0.1f);
        render();
        if (hit) break;
    }

    // Crash: feathers, shake, then the bird drops to the ground.
    dead = true;
    sfx(SFX_DIE);
    c.shake(8, 3);
    fx.burst(BIRD_X, by, 14, C_YELLOW, 2.0f, 24);
    fx.burst(BIRD_X, by, 8, C_ORANGE, 1.6f, 24);
    vy = -1.5f;
    for (int i = 0; i < 28; i++) {
        frame++;
        vy = fminf(vy + 0.35f, 5.0f);
        by = fminf(by + vy, (float)(PLAY_H - BR));
        fx.update(0.1f);
        render();
        waitMs(28);
    }

    if (res.score >= 50) {
        res.note = "Platinum medal!";
        res.noteColor = C_CYAN;
    } else if (res.score >= 30) {
        res.note = "Gold medal!";
        res.noteColor = C_GOLD;
    } else if (res.score >= 20) {
        res.note = "Silver medal!";
        res.noteColor = rgb(200, 205, 215);
    } else if (res.score >= 10) {
        res.note = "Bronze medal!";
        res.noteColor = rgb(205, 127, 50);
    }
    return res;
}

} // namespace

const GameInfo kFlappy = {GAME_FLAPPY, "FLAPPY", C_YELLOW, "Fly through the pipes!", controls, icon, run};

} // namespace games
