// SPACE BLASTER: hold left/right to fly. Your ship fires on its own.
// Drones (10), zig-zaggers (20, two hits) and gunners (30, three hits, they
// shoot back). Catch T = triple shot, R = rapid fire, + = extra life.
// A new, harder wave every 30 seconds.

#include "game_common.h"
#include <math.h>

namespace games {
namespace {

char gNote[32];

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "Hold %s/%s: fly", leftName(), rightName());
#else
    snprintf(l1, n, "%s/%s: fly", leftName(), rightName());
#endif
    snprintf(l2, n, "Auto-fire is always on");
}

void drawShip(tft_sprite &s, int x, int y, uint16_t col, uint32_t frame) {
    // x,y = top center
    s.fillTriangle(x, y, x - 7, y + 10, x + 7, y + 10, col);
    s.fillRect(x - 9, y + 6, 3, 5, darker(col, 60));
    s.fillRect(x + 7, y + 6, 3, 5, darker(col, 60));
    s.fillRect(x - 1, y + 3, 2, 3, C_CYAN);
    if ((frame / 2) % 2) s.fillTriangle(x - 3, y + 10, x + 3, y + 10, x, y + 14, C_ORANGE);
    else s.fillTriangle(x - 2, y + 10, x + 2, y + 10, x, y + 13, C_YELLOW);
}

void drawEnemy(tft_sprite &s, int x, int y, uint8_t type, bool flash) {
    // x,y = center
    if (type == 0) { // drone saucer
        const uint16_t col = flash ? C_WHITE : C_GREEN;
        s.fillRoundRect(x - 7, y - 2, 14, 5, 2, col);
        s.fillCircle(x, y - 2, 3, flash ? C_WHITE : rgb(150, 255, 170));
        s.drawPixel(x - 4, y, C_BLACK);
        s.drawPixel(x + 4, y, C_BLACK);
    } else if (type == 1) { // zig-zagger diamond
        const uint16_t col = flash ? C_WHITE : C_PURPLE;
        s.fillTriangle(x, y - 7, x - 6, y, x + 6, y, col);
        s.fillTriangle(x, y + 7, x - 6, y, x + 6, y, darker(col, 60));
        s.fillRect(x - 1, y - 1, 2, 2, C_WHITE);
    } else { // gunner
        const uint16_t col = flash ? C_WHITE : C_ORANGE;
        s.fillRoundRect(x - 9, y - 4, 18, 7, 3, col);
        s.fillRect(x - 1, y + 3, 3, 4, darker(col, 80));
        s.fillRect(x - 6, y - 2, 3, 2, C_RED);
        s.fillRect(x + 4, y - 2, 3, 2, C_RED);
    }
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    for (int i = 0; i < 10; i++) s.drawPixel(cx - 28 + (i * 37) % 56, cy - 28 + (int)((i * 23 + t / 30) % 56), C_WHITE);
    drawEnemy(s, cx - 12, cy - 16, 0, false);
    drawEnemy(s, cx + 12, cy - 12, 1, false);
    const int shipX = cx + (int)(14 * sinf(t / 350.0f));
    drawShip(s, shipX, cy + 10, C_CYAN, t / 25);
    const int by = cy + 8 - (int)((t / 8) % 30);
    s.fillRect(shipX - 1, by, 2, 4, C_YELLOW);
}

enum PowType : uint8_t { POW_TRIPLE, POW_RAPID, POW_LIFE };

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int HUD = 11;
    const int SHIP_Y = H - 16;
    const uint32_t FRAME_MS = 25;
    const uint16_t bg = rgb(2, 2, 10);

    struct Star {
        float x, y, v;
    } stars[36];
    struct Bullet {
        float x, y, vx, vy;
        bool alive;
    } shots[24] = {}, enemyShots[16] = {};
    struct Enemy {
        float x, y, vx, vy;
        int hp, timer;
        uint8_t type, flash;
        bool alive;
    } enemies[14] = {};
    struct Pow {
        float x, y;
        uint8_t type;
        bool alive;
    } pows[3] = {};

    for (Star &st : stars) st = {(float)random(W), (float)random(H), 0.4f + random(160) / 100.0f};
    float shipX = W / 2.0f;
    int lives = 3, invincible = 60, fireTimer = 0, spawnTimer = 40, tripleTimer = 0, rapidTimer = 0;
    int wave = 1, banner = 60;
    uint32_t frame = 0;
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];

    auto fire = [&](float x, float y, float vx, float vy, Bullet *pool, int n) {
        for (int i = 0; i < n; i++)
            if (!pool[i].alive) {
                pool[i] = {x, y, vx, vy, true};
                return;
            }
    };
    auto spawnEnemy = [&]() {
        for (Enemy &e : enemies) {
            if (e.alive) continue;
            int type = 0;
            const int roll = random(100);
            if (wave >= 3 && roll < 22) type = 2;
            else if (wave >= 2 && roll < 50) type = 1;
            const float x = 12 + random(W - 24);
            const float speedUp = 1.0f + (wave - 1) * 0.08f;
            if (type == 0) e = {x, -8.0f, 0.0f, 0.8f * speedUp, 1, 0, 0, 0, true};
            else if (type == 1) e = {x, -8.0f, (random(2) ? 1.4f : -1.4f) * speedUp, 0.6f * speedUp, 2, 0, 1, 0, true};
            else e = {x, -8.0f, (random(2) ? 0.6f : -0.6f), 0.5f, 3, 40 + (int)random(40), 2, 0, true};
            return;
        }
    };
    auto hitShip = [&]() {
        lives--;
        invincible = 90;
        c.shake(10, 3);
        fx.burst(shipX, SHIP_Y + 6, 20, C_CYAN, 2.2f, 24);
        fx.burst(shipX, SHIP_Y + 6, 10, C_ORANGE, 1.6f, 20);
        sfx(lives > 0 ? SFX_HIT : SFX_DIE);
    };

    auto render = [&](bool showShip) {
        s.fillScreen(bg);
        for (const Star &st : stars) {
            const uint8_t b = (uint8_t)min(255, (int)(st.v * 120));
            s.fillRect((int)st.x, (int)st.y, 1, st.v > 1.5f ? 2 : 1, rgb(b, b, b));
        }
        for (const Pow &p : pows) {
            if (!p.alive) continue;
            const uint16_t pc = p.type == POW_TRIPLE ? C_YELLOW : p.type == POW_RAPID ? C_CYAN : C_PINK;
            s.fillRoundRect((int)p.x - 6, (int)p.y - 5, 12, 10, 3, pc);
            text(s, p.type == POW_TRIPLE ? "T" : p.type == POW_RAPID ? "R" : "+", (int)p.x, (int)p.y - 3, 1, C_BLACK, AL_CENTER);
        }
        for (const Enemy &e : enemies)
            if (e.alive) drawEnemy(s, (int)e.x, (int)e.y, e.type, e.flash > 0);
        for (const Bullet &b : shots)
            if (b.alive) s.fillRect((int)b.x - 1, (int)b.y, 2, 5, rapidTimer > 0 ? C_CYAN : C_YELLOW);
        for (const Bullet &b : enemyShots)
            if (b.alive) {
                s.fillCircle((int)b.x, (int)b.y, 2, C_RED);
                s.drawPixel((int)b.x, (int)b.y, C_WHITE);
            }
        if (showShip && (invincible == 0 || (frame / 3) % 2)) drawShip(s, (int)shipX, SHIP_Y, accent(), frame);
        fx.draw(s);
        pops.draw(s);

        s.fillRect(0, 0, W, HUD, rgb(10, 12, 26));
        snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)res.score);
        text(s, buf, 3, 2, 1, C_WHITE);
        snprintf(buf, sizeof(buf), "WAVE %d", wave);
        text(s, buf, W / 2, 2, 1, C_GREY, AL_CENTER);
        for (int i = 0; i < lives && i < 5; i++) s.fillTriangle(W - 6 - i * 9, 2, W - 10 - i * 9, 9, W - 2 - i * 9, 9, accent());
        if (tripleTimer > 0) text(s, "3X", W / 2 + 34, 2, 1, C_YELLOW);
        if (rapidTimer > 0) text(s, "RF", W / 2 + 50, 2, 1, C_CYAN);
        if (banner > 0) {
            snprintf(buf, sizeof(buf), "WAVE %d", wave);
            textShadow(s, buf, W / 2, H / 2 - 16, 2, C_GOLD, AL_CENTER);
        }
        c.push();
    };

    uint32_t frameStart = millis();
    while (lives > 0) {
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
        if (in.left) shipX -= 3.5f;
        if (in.right) shipX += 3.5f;
#else
        if (in.leftHit) shipX -= 20;
        if (in.rightHit) shipX += 20;
#endif
        shipX = constrain(shipX, 9.0f, (float)(W - 10));

        // Waves get harder every 30 seconds.
        const int newWave = 1 + (int)(frame / 1200);
        if (newWave != wave) {
            wave = newWave;
            banner = 60;
            sfx(SFX_LEVEL);
        }
        if (banner > 0) banner--;
        if (invincible > 0) invincible--;
        if (tripleTimer > 0) tripleTimer--;
        if (rapidTimer > 0) rapidTimer--;

        for (Star &st : stars) {
            st.y += st.v;
            if (st.y > H) {
                st.y = HUD;
                st.x = random(W);
            }
        }

        // Auto-fire.
        if (--fireTimer <= 0) {
            fireTimer = rapidTimer > 0 ? 5 : 9;
            fire(shipX, SHIP_Y - 4, 0, -6, shots, 24);
            if (tripleTimer > 0) {
                fire(shipX - 4, SHIP_Y, -1.2f, -5.8f, shots, 24);
                fire(shipX + 4, SHIP_Y, 1.2f, -5.8f, shots, 24);
            }
            if (frame % 2 == 0) sfx(SFX_SHOOT);
        }

        if (--spawnTimer <= 0) {
            spawnEnemy();
            spawnTimer = max(14, 55 - wave * 5) + (int)random(20);
        }

        for (Bullet &b : shots) {
            if (!b.alive) continue;
            b.x += b.vx;
            b.y += b.vy;
            if (b.y < HUD - 4 || b.x < -4 || b.x > W + 4) b.alive = false;
        }
        for (Bullet &b : enemyShots) {
            if (!b.alive) continue;
            b.x += b.vx;
            b.y += b.vy;
            if (b.y > H + 4 || b.x < -4 || b.x > W + 4) b.alive = false;
            else if (invincible == 0 && fabsf(b.x - shipX) < 6 && b.y > SHIP_Y && b.y < SHIP_Y + 11) {
                b.alive = false;
                hitShip();
            }
        }

        for (Enemy &e : enemies) {
            if (!e.alive) continue;
            if (e.flash > 0) e.flash--;
            if (e.type == 0) { // drone: drift down with a wobble
                e.y += e.vy;
                e.x += sinf((frame + (int)e.y) * 0.08f) * 0.8f;
            } else if (e.type == 1) { // zig-zag off the walls
                e.x += e.vx;
                e.y += e.vy;
                if (e.x < 8 || e.x > W - 8) e.vx = -e.vx;
            } else { // gunner: strafe and shoot at you
                e.x += e.vx;
                e.y += e.y < 34 ? e.vy : 0.12f;
                if (e.x < 12 || e.x > W - 12) e.vx = -e.vx;
                if (--e.timer <= 0) {
                    e.timer = max(35, 80 - wave * 6) + (int)random(30);
                    const float dxs = shipX - e.x, dys = (SHIP_Y + 5) - e.y;
                    const float d = sqrtf(dxs * dxs + dys * dys);
                    if (d > 1) fire(e.x, e.y + 6, dxs / d * 2.2f, dys / d * 2.2f, enemyShots, 16);
                }
            }
            if (e.y > H + 10) {
                e.alive = false;
                continue;
            }

            // Your shots.
            const float hw = e.type == 2 ? 9 : 7, hh = e.type == 1 ? 7 : 5;
            for (Bullet &b : shots) {
                if (!b.alive || fabsf(b.x - e.x) > hw || b.y > e.y + hh || b.y + 5 < e.y - hh) continue;
                b.alive = false;
                e.flash = 3;
                if (--e.hp <= 0) {
                    e.alive = false;
                    const int pts = e.type == 0 ? 10 : (e.type == 1 ? 20 : 30);
                    res.score += pts;
                    snprintf(buf, sizeof(buf), "+%d", pts);
                    pops.add((int)e.x, (int)e.y - 8, buf, C_WHITE);
                    fx.burst(e.x, e.y, 14, e.type == 0 ? C_GREEN : (e.type == 1 ? C_PURPLE : C_ORANGE), 2.0f, 20);
                    fx.burst(e.x, e.y, 6, C_YELLOW, 1.2f, 14);
                    sfx(SFX_BOOM);
                    if (random(100) < 8) {
                        for (Pow &p : pows)
                            if (!p.alive) {
                                const int roll = random(10);
                                p = {e.x, e.y, (uint8_t)(roll < 4 ? POW_TRIPLE : roll < 8 ? POW_RAPID : POW_LIFE), true};
                                break;
                            }
                    }
                    break;
                }
                fx.burst(b.x, b.y, 3, C_WHITE, 0.8f, 8);
            }
            // Ramming you.
            if (e.alive && invincible == 0 && fabsf(e.x - shipX) < hw + 5 && e.y + hh > SHIP_Y + 2 && e.y - hh < SHIP_Y + 11) {
                e.alive = false;
                fx.burst(e.x, e.y, 12, C_ORANGE, 2.0f, 18);
                hitShip();
            }
        }

        for (Pow &p : pows) {
            if (!p.alive) continue;
            p.y += 1.0f;
            if (p.y > H + 6) p.alive = false;
            else if (fabsf(p.x - shipX) < 12 && p.y > SHIP_Y - 4 && p.y < SHIP_Y + 12) {
                p.alive = false;
                sfx(SFX_POWER);
                if (p.type == POW_TRIPLE) {
                    tripleTimer = 400;
                    pops.add((int)p.x, SHIP_Y - 12, "TRIPLE!", C_YELLOW);
                } else if (p.type == POW_RAPID) {
                    rapidTimer = 400;
                    pops.add((int)p.x, SHIP_Y - 12, "RAPID!", C_CYAN);
                } else {
                    if (lives < 5) lives++;
                    pops.add((int)p.x, SHIP_Y - 12, "+1 LIFE", C_PINK);
                }
            }
        }

        fx.update(0.03f);
        pops.update();
        render(true);
    }

    for (int i = 0; i < 30; i++) {
        frame++;
        for (Star &st : stars) {
            st.y += st.v * 0.5f;
            if (st.y > H) st.y = HUD;
        }
        fx.update(0.03f);
        pops.update();
        render(false);
        waitMs(30);
    }
    snprintf(gNote, sizeof(gNote), "Reached wave %d", wave);
    res.note = gNote;
    res.noteColor = C_CYAN;
    return res;
}

} // namespace

const GameInfo kBlaster = {GAME_BLASTER, "BLASTER", C_PURPLE, "Blast ships, dodge fire", controls, icon, run};

} // namespace games
