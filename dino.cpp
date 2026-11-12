// DINO RUN: right button jumps (hold to jump higher), left button ducks.
// Jump the cacti and low birds, duck under the middle birds.
// It gets faster, and turns to night every 700 points.

#include "game_common.h"
#include <math.h>

namespace games {
namespace {

// 12x12 dino, '#' = pixel, drawn 2x. Rows 10-11 are the legs.
const char *const kDinoBody[10] = {
    "......######", ".....##.####", ".....#######", ".....####...", ".....######.",
    "#...#####...", "#..########.", "##########..", ".#########..", "..#######...",
};
const char *const kLegsA[2] = {"...##..#....", "...#...##..."};
const char *const kLegsB[2] = {"...#...##...", "...##..#...."};
const char *const kLegsJump[2] = {"...#...#....", "...##..##..."};
// 16x8 ducking dino.
const char *const kDuck[8] = {
    "...........#####", "#.........##.###", "##.....#########", "################",
    ".##############.", "..###########...", "...##....#......", "...#.....##.....",
};
const char *const kDuckLegsB[2] = {"...#.....##.....", "...##....#......"};

void blit(tft_sprite &s, const char *const *rows, int nRows, int x, int y, int scale, uint16_t col) {
    for (int r = 0; r < nRows; r++) {
        const char *row = rows[r];
        for (int c = 0; row[c];) {
            if (row[c] != '#') {
                c++;
                continue;
            }
            int run = 0;
            while (row[c + run] == '#') run++;
            s.fillRect(x + c * scale, y + r * scale, run * scale, scale, col);
            c += run;
        }
    }
}

void drawDino(tft_sprite &s, int x, int y, bool ducking, bool inAir, uint32_t frame, uint16_t col) {
    if (ducking) {
        blit(s, kDuck, 6, x, y, 2, col);
        blit(s, ((frame / 4) % 2) ? kDuckLegsB : kDuck + 6, 2, x, y + 12, 2, col);
        return;
    }
    blit(s, kDinoBody, 10, x, y, 2, col);
    const char *const *legs = inAir ? kLegsJump : (((frame / 4) % 2) ? kLegsA : kLegsB);
    blit(s, legs, 2, x, y + 20, 2, col);
}

void drawCactus(tft_sprite &s, int x, int baseY, int w, int h, uint16_t col) {
    s.fillRoundRect(x + w / 2 - w / 4, baseY - h, w / 2 + 1, h, 2, col);
    if (h >= 18) {
        s.fillRoundRect(x, baseY - h + 6, 3, h / 3, 1, col);
        s.fillRect(x, baseY - h + 6 + h / 3 - 3, w / 2, 3, col);
        s.fillRoundRect(x + w - 3, baseY - h + 3, 3, h / 3, 1, col);
        s.fillRect(x + w / 2, baseY - h + 3 + h / 3 - 3, w / 2, 3, col);
    } else {
        s.fillRect(x, baseY - h + 4, 2, 5, col);
        s.fillRect(x + w - 2, baseY - h + 3, 2, 5, col);
    }
}

void drawBirdy(tft_sprite &s, int x, int y, bool wingUp, uint16_t col) {
    s.fillRoundRect(x + 4, y + 5, 12, 4, 2, col);             // body
    s.fillTriangle(x, y + 6, x + 5, y + 4, x + 5, y + 8, col); // beak/head
    if (wingUp) s.fillTriangle(x + 7, y + 5, x + 13, y + 5, x + 9, y - 2, col);
    else s.fillTriangle(x + 7, y + 8, x + 13, y + 8, x + 9, y + 15, col);
    s.fillRect(x + 15, y + 6, 4, 2, col);                      // tail
}

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "%s: jump (hold=higher)", rightName());
    snprintf(l2, n, "Hold %s: duck", leftName());
#else
    snprintf(l1, n, "%s or OK: jump", rightName());
    snprintf(l2, n, "%s: duck", leftName());
#endif
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    const int hop = (int)(10 * fabsf(sinf(t / 250.0f)));
    drawDino(s, cx - 20, cy - 4 - hop, false, hop > 2, t / 25, rgb(200, 200, 200));
    drawCactus(s, cx + 8, cy + 20, 12, 22, C_GREEN);
    s.fillRect(cx - 28, cy + 20, 56, 1, C_GREY);
}

enum ObType : uint8_t { OB_CACTUS, OB_BIRD };

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int GY = H - 16;          // ground line
    const int DX = 22;              // dino x
    const int STAND_H = 24, DUCK_H = 16;
    const float GRAV = 0.30f, GRAV_HELD = 0.19f, JUMP_V = -5.4f;
    const uint32_t FRAME_MS = 25;

    struct Ob {
        float x;
        int y, w, h;
        uint8_t type;
        bool alive;
    } obs[5] = {};
    float dinoY = GY - STAND_H, vy = 0, speed = 3.2f, dist = 0, scroll = 0, nextGap = 120;
    bool onGround = true, ducking = false;
#ifndef GAMES_DIRECT_BUTTONS
    int duckFrames = 0; // menu-button fallback: a tap ducks briefly
#endif
    uint32_t frame = 0, lastHundred = 0, hundreds = 0;
    int nightT = 0; // 0 = day, 255 = night
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];
    const uint32_t best = loadBest(GAME_DINO);

    auto spawn = [&]() {
        for (Ob &o : obs) {
            if (o.alive) continue;
            const bool bird = res.score > 300 && random(100) < 30;
            if (bird) {
                const int level = random(3); // 0 low (jump), 1 middle (duck), 2 high (run under)
                const int y = level == 0 ? GY - 14 : (level == 1 ? GY - 32 : GY - 48);
                o = {(float)(W + 10), y, 19, 12, OB_BIRD, true};
            } else {
                const int kind = random(res.score > 150 ? 4 : 3);
                const int w = kind == 0 ? 8 : (kind == 1 ? 12 : (kind == 2 ? 20 : 30));
                const int h = kind == 0 ? 16 : (kind == 1 ? 24 : (kind == 2 ? 16 : 22));
                o = {(float)(W + 10), GY - h, w, h, OB_CACTUS, true};
            }
            nextGap = random((long)(70 + speed * 14), (long)(190 + speed * 14));
            return;
        }
    };

    auto render = [&](bool dead) {
        const uint16_t skyTop = blend(rgb(215, 235, 250), rgb(10, 12, 30), nightT);
        const uint16_t skyBot = blend(rgb(250, 250, 242), rgb(35, 38, 60), nightT);
        const uint16_t ink = blend(rgb(83, 83, 83), rgb(225, 225, 225), nightT);
        const uint16_t cactus = blend(rgb(50, 140, 60), rgb(90, 190, 100), nightT);
        vGradient(s, 0, 0, W, GY + 1, skyTop, skyBot);
        s.fillRect(0, GY + 1, W, H - GY - 1, blend(rgb(235, 225, 200), rgb(30, 30, 40), nightT));

        if (nightT > 120) { // moon + stars
            s.fillCircle(W - 40, 26, 9, rgb(240, 240, 220));
            s.fillCircle(W - 36, 23, 8, skyTop);
            for (int i = 0; i < 18; i++)
                if ((frame / 10 + i) % 5) s.drawPixel((i * 71 + 9) % W, (i * 37 + 5) % (GY / 2), C_WHITE);
        }
        for (int i = 0; i < 3; i++) { // clouds
            const int span = W + 50;
            const int cx0 = ((int)(i * 90 + 30 - scroll * 0.25f) % span + span) % span - 25;
            const uint16_t cc = blend(C_WHITE, rgb(70, 75, 100), nightT);
            s.fillRoundRect(cx0, 18 + i * 13, 26, 7, 3, cc);
            s.fillRoundRect(cx0 + 6, 14 + i * 13, 14, 7, 3, cc);
        }

        s.fillRect(0, GY, W, 1, ink);
        for (int x = -((int)scroll % 23); x < W; x += 23) { // ground texture
            s.fillRect(x, GY + 4, 3, 1, ink);
            s.fillRect(x + 11, GY + 8, 2, 1, ink);
        }

        for (const Ob &o : obs) {
            if (!o.alive) continue;
            if (o.type == OB_BIRD) {
                drawBirdy(s, (int)o.x, o.y, (frame / 6) % 2, ink);
            } else if (o.w >= 20) { // a group of cacti
                drawCactus(s, (int)o.x, GY, 8, o.h, cactus);
                drawCactus(s, (int)o.x + o.w / 2 - 2, GY, 8, o.h - 4, cactus);
                if (o.w >= 30) drawCactus(s, (int)o.x + o.w - 8, GY, 8, o.h - 2, cactus);
            } else {
                drawCactus(s, (int)o.x, GY, o.w, o.h, cactus);
            }
        }

        const uint16_t dinoCol = dead ? C_RED : ink;
        if (ducking && onGround) drawDino(s, DX - 4, GY - DUCK_H, true, false, frame, dinoCol);
        else drawDino(s, DX, (int)dinoY, false, !onGround, dead ? 0 : frame, dinoCol);
        fx.draw(s);
        pops.draw(s);

        const bool flash = lastHundred && frame - lastHundred < 30 && (frame / 4) % 2;
        snprintf(buf, sizeof(buf), "%05lu", (unsigned long)res.score);
        if (!flash) text(s, buf, W - 4, 4, 1, ink, AL_RIGHT);
        snprintf(buf, sizeof(buf), "HI %05lu", (unsigned long)max(best, res.score));
        text(s, buf, W - 40, 4, 1, blend(ink, skyTop, 110), AL_RIGHT);
        c.push();
    };

    uint32_t frameStart = millis();
    bool dead = false;
    while (!dead) {
        keepAwake();
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

#ifdef GAMES_DIRECT_BUTTONS
        const bool jumpPressed = in.rightHit;
        const bool jumpHeld = in.right;
        ducking = in.left;
#else
        const bool jumpPressed = in.rightHit || (in.action && !in.leftHit);
        const bool jumpHeld = false;
        if (in.leftHit) duckFrames = 22;
        ducking = duckFrames > 0;
        if (duckFrames > 0) duckFrames--;
#endif
        if (jumpPressed && onGround && !ducking) {
            vy = JUMP_V;
            onGround = false;
            sfx(SFX_JUMP);
        }
        if (!onGround) {
            float g = (jumpHeld && vy < 0) ? GRAV_HELD : GRAV;
            if (ducking) g = 0.9f; // fast-fall
            vy += g;
            dinoY += vy;
            if (dinoY >= GY - STAND_H) {
                dinoY = GY - STAND_H;
                vy = 0;
                onGround = true;
                fx.burst(DX + 8, GY - 1, 5, rgb(160, 150, 130), 0.8f, 10);
            }
        }

        speed = fminf(speed + 0.001f, 7.0f);
        scroll += speed;
        dist += speed * 0.08f;
        res.score = (uint32_t)dist;
        if (res.score / 100 > hundreds) { // every 100 points: chime + flashing score
            hundreds = res.score / 100;
            lastHundred = frame;
            sfx(SFX_SCORE);
        }
        const bool wantNight = (res.score % 1000) >= 700;
        nightT = constrain(nightT + (wantNight ? 6 : -6), 0, 255);

        float rightmost = -1000;
        for (Ob &o : obs) {
            if (!o.alive) continue;
            o.x -= speed * (o.type == OB_BIRD ? 1.15f : 1.0f);
            if (o.x + o.w < -4) o.alive = false;
            rightmost = fmaxf(rightmost, o.x + o.w);
        }
        if (rightmost < W - nextGap) spawn();

        // Hitbox (a little smaller than the drawing, for fairness).
        int hx, hy, hw, hh;
        if (ducking && onGround) {
            hx = DX - 2;
            hy = GY - DUCK_H + 3;
            hw = 26;
            hh = DUCK_H - 3;
        } else {
            hx = DX + 4;
            hy = (int)dinoY + 2;
            hw = 16;
            hh = STAND_H - 4;
        }
        for (const Ob &o : obs) {
            if (!o.alive) continue;
            const int ox0 = (int)o.x + 2, oy0 = o.y + 2, ow = o.w - 4, oh = o.h - 3;
            if (hx < ox0 + ow && hx + hw > ox0 && hy < oy0 + oh && hy + hh > oy0) dead = true;
        }

        fx.update(0.05f);
        pops.update();
        if (!dead) render(false);
    }

    sfx(SFX_DIE);
    c.shake(8, 3);
    fx.burst(DX + 12, dinoY + 10, 16, C_RED, 1.8f, 22);
    for (int i = 0; i < 24; i++) {
        fx.update(0.05f);
        render(true);
        waitMs(30);
    }
    return res;
}

} // namespace

const GameInfo kDino = {GAME_DINO, "DINO RUN", rgb(200, 200, 200), "Jump cacti, duck birds", controls, icon, run};

} // namespace games
