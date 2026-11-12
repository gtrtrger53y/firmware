// PONG vs CPU: hold left/up and right/down to move your paddle.
// First to 5 wins the match; every win levels up the CPU.
// Score = total points you win before losing a match.

#include "game_common.h"
#include <math.h>

namespace games {
namespace {

char gNote[32];

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "Hold %s: paddle up", leftName());
    snprintf(l2, n, "Hold %s: paddle down", rightName());
#else
    snprintf(l1, n, "%s: paddle up", leftName());
    snprintf(l2, n, "%s: paddle down", rightName());
#endif
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    for (int y = cy - 26; y < cy + 26; y += 8) s.fillRect(cx - 1, y, 2, 4, C_DGREY);
    const float ph = (t % 1600) / 1600.0f;
    const float tri = ph < 0.5f ? ph * 2 : 2 - ph * 2; // 0..1..0
    const int bx = cx - 22 + (int)(44 * tri);
    const int by = cy + (int)(14 * sinf(t / 260.0f)); // stays inside the icon frame
    s.fillRect(cx - 26, by - 8, 4, 16, C_CYAN);
    s.fillRect(cx + 22, cy - 8 + (int)(10 * sinf(t / 300.0f)), 4, 16, C_RED);
    s.fillRect(bx - 2, by - 2, 4, 4, C_WHITE);
}

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int PW = 4, PH = H >= 200 ? 40 : 28;
    const int PX = 6, CX = W - 10;
    const int BS = 4; // ball size
    const int WIN = 5;
    const uint32_t FRAME_MS = 25;
    const uint16_t cpuCol = rgb(255, 70, 90);

    float py = (H - PH) / 2.0f, cy = (H - PH) / 2.0f;
    float bx = 0, by = 0, vx = 0, vy = 0, speed = 0;
    float trailX[4] = {}, trailY[4] = {};
    int me = 0, cpu = 0, level = 1, serveFrames = 0, serveDir = 1, flashSide = 0, flashFrames = 0;
    int cpuError = 0, banner = 0;
    uint32_t frame = 0;
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];

    // CPU skill rises with each match you win: faster, earlier, more accurate.
    auto cpuSpeed = [&]() { return fminf(1.25f + level * 0.3f, 3.6f); };
    auto cpuAimError = [&]() { return max(3, 24 - level * 4); };
    auto cpuReactX = [&]() { return W * fmaxf(0.3f, 0.6f - level * 0.05f); };
    auto serve = [&](int dir) {
        bx = W / 2.0f;
        by = H / 2.0f;
        speed = 2.4f + 0.1f * (level - 1);
        serveDir = dir;
        serveFrames = 40;
        cpuError = random(-cpuAimError(), cpuAimError() + 1);
        for (int i = 0; i < 4; i++) {
            trailX[i] = bx;
            trailY[i] = by;
        }
    };
    // Where will the ball be when it reaches x? (bounces off top/bottom)
    auto predictY = [&](float targetX) {
        if (vx <= 0.01f) return H / 2.0f;
        const float t = (targetX - bx) / vx;
        float y = by + vy * t;
        const float span = (float)(H - BS);
        y = fmodf(y, 2 * span);
        if (y < 0) y += 2 * span;
        if (y > span) y = 2 * span - y;
        return y + BS / 2.0f;
    };

    auto render = [&]() {
        s.fillScreen(rgb(6, 8, 16));
        if (flashFrames > 0) { // tint the half that just lost a point
            const int fxs = flashSide < 0 ? 0 : W / 2;
            s.fillRect(fxs, 0, W / 2, H, blend(rgb(6, 8, 16), flashSide < 0 ? accent() : cpuCol, 40));
        }
        for (int y = 2; y < H; y += 10) s.fillRect(W / 2 - 1, y, 2, 5, C_DGREY);

        snprintf(buf, sizeof(buf), "%d", me);
        text(s, buf, W / 2 - 28, 8, 4, darker(accent(), 140), AL_CENTER);
        snprintf(buf, sizeof(buf), "%d", cpu);
        text(s, buf, W / 2 + 28, 8, 4, darker(cpuCol, 140), AL_CENTER);
        snprintf(buf, sizeof(buf), "LV %d", level);
        text(s, buf, W / 2, H - 10, 1, C_GREY, AL_CENTER);

        s.fillRoundRect(PX, (int)py, PW, PH, 2, accent());
        s.fillRoundRect(CX, (int)cy, PW, PH, 2, cpuCol);

        for (int i = 3; i >= 1; i--)
            s.fillRect((int)trailX[i], (int)trailY[i], BS, BS, blend(C_WHITE, rgb(6, 8, 16), (uint8_t)(100 + i * 38)));
        if (serveFrames == 0 || (frame / 4) % 2) s.fillRect((int)bx, (int)by, BS, BS, C_WHITE);

        fx.draw(s);
        pops.draw(s);
        if (banner > 0) {
            snprintf(buf, sizeof(buf), "LEVEL %d", level);
            textShadow(s, buf, W / 2, H / 2 - 22, 2, C_GOLD, AL_CENTER);
            textShadow(s, "CPU gets faster!", W / 2, H / 2 + 2, 1, C_WHITE, AL_CENTER);
        }
        c.push();
    };

    serve(random(2) ? 1 : -1);
    uint32_t frameStart = millis();
    bool over = false;

    while (!over) {
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

        // Your paddle.
#ifdef GAMES_DIRECT_BUTTONS
        if (in.left) py -= 4;
        if (in.right) py += 4;
#else
        if (in.leftHit) py -= 18;
        if (in.rightHit) py += 18;
#endif
        py = constrain(py, 0.0f, (float)(H - PH));

        // CPU paddle: chase the predicted landing spot once the ball heads its way.
        float target = H / 2.0f;
        float maxMove = cpuSpeed() * 0.5f; // drift back to the middle while the ball is away
        if (vx > 0 && bx > cpuReactX()) {
            target = predictY((float)CX) + cpuError;
            maxMove = cpuSpeed();
        }
        const float diff = target - (cy + PH / 2.0f);
        cy += constrain(diff, -maxMove, maxMove);
        cy = constrain(cy, 0.0f, (float)(H - PH));

        if (flashFrames > 0) flashFrames--;
        if (banner > 0) banner--;

        for (int i = 3; i > 0; i--) {
            trailX[i] = trailX[i - 1];
            trailY[i] = trailY[i - 1];
        }
        trailX[0] = bx;
        trailY[0] = by;

        if (serveFrames > 0) {
            if (--serveFrames == 0) {
                const float ang = (random(100) - 50) / 100.0f;
                vx = serveDir * speed * cosf(ang);
                vy = speed * sinf(ang);
            }
        } else {
            bx += vx;
            by += vy;
            if (by < 0) {
                by = 0;
                vy = fabsf(vy);
            } else if (by > H - BS) {
                by = H - BS;
                vy = -fabsf(vy);
            }

            auto hitPaddle = [&](float paddleY, int dir, uint16_t col) {
                float off = ((by + BS / 2.0f) - (paddleY + PH / 2.0f)) / (PH / 2.0f);
                off = constrain(off, -1.0f, 1.0f);
                float ang = off * 1.0f;
                if (fabsf(ang) < 0.12f) ang = vy >= 0 ? 0.12f : -0.12f;
                speed = fminf(speed + 0.15f, 5.5f);
                vx = dir * speed * cosf(ang);
                vy = speed * sinf(ang);
                fx.burst(dir > 0 ? PX + PW : CX, by + BS / 2.0f, 8, col, 1.5f, 14);
                sfx(SFX_BOUNCE);
            };
            if (vx < 0 && bx <= PX + PW && bx + BS >= PX && by + BS >= py && by <= py + PH) {
                bx = PX + PW;
                hitPaddle(py, 1, accent());
            } else if (vx > 0 && bx + BS >= CX && bx <= CX + PW && by + BS >= cy && by <= cy + PH) {
                bx = CX - BS;
                hitPaddle(cy, -1, cpuCol);
                cpuError = random(-cpuAimError(), cpuAimError() + 1);
            }

            if (bx < -BS) { // CPU scores
                cpu++;
                flashSide = -1;
                flashFrames = 12;
                c.shake(8, 3);
                sfx(SFX_HIT);
                fx.burst(0, by, 14, cpuCol, 2.0f);
                if (cpu >= WIN) over = true;
                else serve(-1);
            } else if (bx > W) { // you score
                me++;
                res.score++;
                flashSide = 1;
                flashFrames = 12;
                sfx(SFX_SCORE);
                fx.burst(W, by, 14, accent(), 2.0f);
                pops.add(W * 3 / 4, H / 2, "+1", C_WHITE);
                if (me >= WIN) { // match won: next level
                    level++;
                    me = cpu = 0;
                    banner = 70;
                    sfx(SFX_LEVEL);
                    for (int i = 0; i < 5; i++) fx.burst(random(W), random(H), 10, hsv(i * 70), 2.0f, 28);
                    serve(1);
                    serveFrames = 70;
                } else {
                    serve(1);
                }
            }
        }

        fx.update(0.04f);
        pops.update();
        render();
    }

    sfx(SFX_DIE);
    for (int i = 0; i < 18; i++) {
        fx.update(0.04f);
        render();
        waitMs(30);
    }
    if (level > 1) {
        snprintf(gNote, sizeof(gNote), "Beat %d CPU%s", level - 1, level > 2 ? "s" : "");
        res.note = gNote;
        res.noteColor = C_CYAN;
    }
    return res;
}

} // namespace

const GameInfo kPong = {GAME_PONG, "PONG", C_CYAN, "Beat the CPU to 5", controls, icon, run};

} // namespace games
