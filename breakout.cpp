// BREAKOUT: hold left/right to move the paddle. Clear the wall to level up.
// Silver bricks take two hits. Catch capsules: W = wide paddle,
// S = slow ball, + = extra life.

#include "game_common.h"
#include <math.h>

namespace games {
namespace {

char gNote[32];

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "Hold %s: left", leftName());
    snprintf(l2, n, "Hold %s: right", rightName());
#else
    snprintf(l1, n, "%s: left", leftName());
    snprintf(l2, n, "%s: right", rightName());
#endif
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    const uint16_t cols[3] = {C_RED, C_ORANGE, C_YELLOW};
    for (int r = 0; r < 3; r++)
        for (int c = 0; c < 4; c++) s.fillRect(cx - 24 + c * 12, cy - 22 + r * 6, 11, 5, cols[r]);
    const int px = cx - 8 + (int)(14 * sinf(t / 400.0f));
    s.fillRoundRect(px - 9, cy + 20, 18, 4, 2, C_CYAN);
    const float ph = (t % 1400) / 1400.0f;
    const int bx = cx - 20 + (int)(40 * ph);
    const int by = cy + 16 - (int)(28 * fabsf(sinf(ph * 3.14159f * 2)));
    s.fillCircle(bx, by, 2, C_WHITE);
}

enum CapType : uint8_t { CAP_WIDE, CAP_SLOW, CAP_LIFE };

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int HUD = 12;
    const int COLS = 10, ROWS = 6;
    const int BW = W / COLS;
    const int BX0 = (W - BW * COLS) / 2;
    const int BH = H >= 200 ? 10 : 7;
    const int BTOP = HUD + 4;
    const int PADW = W / 6, PADH = 4, PADY = H - 8;
    const float R = 2.0f;
    const uint32_t FRAME_MS = 25;
    const uint16_t rowColor[ROWS] = {C_RED, C_ORANGE, C_YELLOW, C_GREEN, C_CYAN, C_BLUE};
    const uint16_t silver = rgb(200, 205, 215);

    uint8_t bricks[ROWS][COLS]; // 0 empty, 1 normal, 2 silver (two hits)
    struct Cap {
        float x, y;
        uint8_t type;
        bool alive;
    } caps[4] = {};
    float trailX[4] = {}, trailY[4] = {};
    int remaining = 0, level = 1, lives = 3, stuckFrames = 0, wideTimer = 0, slowTimer = 0;
    float px = (W - PADW) / 2.0f, bx = 0, by = 0, vx = 0, vy = 0, speed = 0;
    uint32_t frame = 0;
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];

    auto baseSpeed = [&]() { return fminf(2.4f + 0.25f * (level - 1), 3.6f); };
    auto maxSpeed = [&]() { return slowTimer > 0 ? baseSpeed() : baseSpeed() + 1.2f; };
    auto padW = [&]() { return wideTimer > 0 ? PADW * 3 / 2 : PADW; };

    auto layout = [&](int r, int col) -> uint8_t {
        bool on = true;
        switch ((level - 1) % 5) {
            case 0: on = true; break;                                     // full wall
            case 1: on = ((r + col) % 2) == 0; break;                     // checker
            case 2: on = col >= 4 - r && col <= 5 + r; break;             // pyramid
            case 3: on = r == 0 || r == ROWS - 1 || col == 0 || col == COLS - 1 || ((r == 2 || r == 3) && (col == 4 || col == 5)); break;
            default: on = (col % 3) != 1; break;                          // columns
        }
        if (!on) return 0;
        const int toughRows = level >= 6 ? 2 : (level >= 3 ? 1 : 0);
        return r < toughRows ? 2 : 1;
    };

    auto newLevel = [&]() {
        remaining = 0;
        for (int r = 0; r < ROWS; r++)
            for (int col = 0; col < COLS; col++) {
                bricks[r][col] = layout(r, col);
                if (bricks[r][col]) remaining++;
            }
        for (Cap &cp : caps) cp.alive = false;
        speed = baseSpeed();
        stuckFrames = 50;
    };

    auto brickColor = [&](int r, int col) { return bricks[r][col] == 2 ? silver : rowColor[r]; };

    auto brickAt = [&](float fx0, float fy0) -> bool {
        const int x = (int)fx0, y = (int)fy0;
        if (y < BTOP || y >= BTOP + ROWS * BH || x < BX0 || x >= BX0 + COLS * BW) return false;
        const int col = (x - BX0) / BW, r = (y - BTOP) / BH;
        if (!bricks[r][col]) return false;
        const int cx = BX0 + col * BW + BW / 2, cy = BTOP + r * BH + BH / 2;
        if (bricks[r][col] == 2) { // silver: crack it first
            bricks[r][col] = 1;
            fx.burst(cx, cy, 5, silver, 1.2f, 12);
            sfx(SFX_BOUNCE);
            return true;
        }
        fx.burst(cx, cy, 9, rowColor[r], 1.6f);
        bricks[r][col] = 0;
        remaining--;
        res.score += (ROWS - r) * 10;
        speed = fminf(speed + 0.04f, maxSpeed());
        sfx(SFX_BRICK);
        if (random(100) < 12) {
            for (Cap &cp : caps)
                if (!cp.alive) {
                    const uint8_t roll = (uint8_t)random(10);
                    cp = {(float)cx, (float)cy, (uint8_t)(roll < 4 ? CAP_WIDE : roll < 8 ? CAP_SLOW : CAP_LIFE), true};
                    break;
                }
        }
        return true;
    };

    auto render = [&]() {
        s.fillScreen(rgb(4, 6, 14));
        for (int y = HUD; y < H; y += 12) s.fillRect(0, y, W, 1, rgb(10, 14, 28)); // faint grid

        snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)res.score);
        text(s, buf, 3, 2, 1, C_WHITE);
        snprintf(buf, sizeof(buf), "LV %d", level);
        text(s, buf, W / 2, 2, 1, C_GREY, AL_CENTER);
        for (int i = 0; i < lives && i < 6; i++) s.fillCircle(W - 6 - i * 8, 5, 2, C_PINK);
        s.fillRect(0, HUD - 1, W, 1, C_DGREY);

        for (int r = 0; r < ROWS; r++)
            for (int col = 0; col < COLS; col++) {
                if (!bricks[r][col]) continue;
                const int x = BX0 + col * BW + 1, y = BTOP + r * BH + 1;
                const uint16_t bc = brickColor(r, col);
                s.fillRect(x, y, BW - 2, BH - 2, bc);
                s.fillRect(x, y, BW - 2, 1, lighter(bc, 110));
                s.fillRect(x, y + BH - 3, BW - 2, 1, darker(bc, 90));
            }

        for (const Cap &cp : caps) {
            if (!cp.alive) continue;
            const uint16_t cc = cp.type == CAP_WIDE ? C_CYAN : cp.type == CAP_SLOW ? C_GREEN : C_PINK;
            s.fillRoundRect((int)cp.x - 7, (int)cp.y - 4, 14, 9, 4, cc);
            text(s, cp.type == CAP_WIDE ? "W" : cp.type == CAP_SLOW ? "S" : "+", (int)cp.x, (int)cp.y - 3, 1, C_BLACK, AL_CENTER);
        }

        const int pw = padW();
        const uint16_t pc = wideTimer > 0 ? C_CYAN : accent();
        s.fillRoundRect((int)px, PADY, pw, PADH, 2, pc);
        s.fillRect((int)px + 2, PADY, pw - 4, 1, lighter(pc, 130));

        for (int i = 3; i >= 1; i--)
            s.fillCircle((int)trailX[i], (int)trailY[i], 1, blend(C_WHITE, rgb(4, 6, 14), (uint8_t)(90 + i * 40)));
        s.fillCircle((int)bx, (int)by, (int)R, slowTimer > 0 ? C_GREEN : C_WHITE);

        fx.draw(s);
        pops.draw(s);
        if (stuckFrames > 0) {
            snprintf(buf, sizeof(buf), "LEVEL %d", level);
            textShadow(s, buf, W / 2, BTOP + ROWS * BH + 12, 2, C_WHITE, AL_CENTER);
        }
        c.push();
    };

    newLevel();
    uint32_t frameStart = millis();

    while (true) {
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

        // Paddle. Direct buttons can be held; menu-button fallback moves in steps.
        const float oldCenter = px + padW() / 2.0f;
        if (wideTimer > 0 && --wideTimer == 0) px = oldCenter - PADW / 2.0f; // shrink around center
        if (slowTimer > 0) slowTimer--;
#ifdef GAMES_DIRECT_BUTTONS
        if (in.left) px -= 5;
        if (in.right) px += 5;
#else
        if (in.leftHit) px -= 24;
        if (in.rightHit) px += 24;
#endif
        px = constrain(px, 0.0f, (float)(W - padW()));

        // Capsules fall; catch them with the paddle.
        for (Cap &cp : caps) {
            if (!cp.alive) continue;
            cp.y += 1.3f;
            if (cp.y + 4 >= PADY && cp.y - 4 <= PADY + PADH && cp.x >= px - 6 && cp.x <= px + padW() + 6) {
                cp.alive = false;
                sfx(SFX_POWER);
                fx.burst(cp.x, PADY, 10, C_WHITE, 1.4f);
                if (cp.type == CAP_WIDE) {
                    const float center = px + padW() / 2.0f;
                    wideTimer = 600;
                    px = constrain(center - padW() / 2.0f, 0.0f, (float)(W - padW()));
                    pops.add((int)cp.x, PADY - 12, "WIDE!", C_CYAN);
                } else if (cp.type == CAP_SLOW) {
                    slowTimer = 400;
                    speed = baseSpeed();
                    pops.add((int)cp.x, PADY - 12, "SLOW", C_GREEN);
                } else {
                    if (lives < 6) lives++;
                    pops.add((int)cp.x, PADY - 12, "+1 LIFE", C_PINK);
                }
            } else if (cp.y > H + 6) {
                cp.alive = false;
            }
        }

        for (int i = 3; i > 0; i--) {
            trailX[i] = trailX[i - 1];
            trailY[i] = trailY[i - 1];
        }
        trailX[0] = bx;
        trailY[0] = by;

        if (stuckFrames > 0) { // ball rides the paddle, then launches
            bx = px + padW() / 2.0f;
            by = PADY - R - 1;
            for (int i = 0; i < 4; i++) {
                trailX[i] = bx;
                trailY[i] = by;
            }
            if (--stuckFrames == 0) {
                const float ang = (random(2) ? 1 : -1) * (0.35f + random(30) / 100.0f);
                vx = speed * sinf(ang);
                vy = -speed * cosf(ang);
            }
            fx.update(0.06f);
            pops.update();
            render();
            continue;
        }

        // Move in small sub-steps so the ball never skips through a brick.
        const int steps = 1 + (int)(speed / 2.0f);
        bool lost = false;
        for (int k = 0; k < steps && !lost; k++) {
            const float sx = vx / steps, sy = vy / steps;

            bx += sx;
            if (bx - R < 0) {
                bx = R;
                vx = fabsf(vx);
            } else if (bx + R > W) {
                bx = W - R;
                vx = -fabsf(vx);
            }
            if (brickAt(bx + (sx > 0 ? R : -R), by)) {
                bx -= sx;
                vx = -vx;
            }

            by += sy;
            if (by - R < HUD) {
                by = HUD + R;
                vy = fabsf(vy);
            }
            if (brickAt(bx, by + (sy > 0 ? R : -R))) {
                by -= sy;
                vy = -vy;
            }

            // Paddle: where it hits sets the angle (center = almost straight up).
            const int pw = padW();
            if (vy > 0 && by + R >= PADY && by + R <= PADY + PADH + 3 && bx >= px - R && bx <= px + pw + R) {
                float off = (bx - (px + pw / 2.0f)) / (pw / 2.0f);
                off = constrain(off, -1.0f, 1.0f);
                float ang = off * 1.05f; // up to ~60 degrees
                // Never straight up: a vertical ball can bounce in one column forever.
                if (fabsf(ang) < 0.15f) ang = (vx >= 0) ? 0.15f : -0.15f;
                vx = speed * sinf(ang);
                vy = -speed * cosf(ang);
                by = PADY - R;
                sfx(SFX_BOUNCE);
            }

            if (by - R > H) lost = true;
        }

        // Keep speed consistent after brick hits sped it up.
        const float mag = sqrtf(vx * vx + vy * vy);
        if (mag > 0.01f) {
            vx = vx / mag * speed;
            vy = vy / mag * speed;
        }

        if (lost) {
            c.shake(8, 3);
            if (--lives <= 0) break;
            sfx(SFX_HIT);
            speed = baseSpeed();
            stuckFrames = 50;
        } else if (remaining == 0) {
            level++;
            if (lives < 6) lives++; // bonus life for clearing a wall
            sfx(SFX_LEVEL);
            for (int i = 0; i < 5; i++) fx.burst(random(W), BTOP + random(ROWS * BH), 10, hsv(i * 70), 2.0f, 28);
            pops.add(W / 2, PADY - 30, "+1 LIFE", C_PINK);
            newLevel();
        }
        fx.update(0.06f);
        pops.update();
        render();
    }

    sfx(SFX_DIE);
    for (int i = 0; i < 20; i++) {
        fx.update(0.06f);
        pops.update();
        render();
        waitMs(30);
    }
    snprintf(gNote, sizeof(gNote), "Reached level %d", level);
    res.note = gNote;
    res.noteColor = C_CYAN;
    return res;
}

} // namespace

const GameInfo kBreakout = {GAME_BREAKOUT, "BREAKOUT", C_ORANGE, "Smash every brick!", controls, icon, run};

} // namespace games
