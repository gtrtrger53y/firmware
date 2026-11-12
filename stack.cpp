// STACK: tap to drop the sliding block on the tower. Whatever hangs over the
// edge is sliced off. Land it perfectly to keep its size; three perfects in a
// row make it grow back.

#include "game_common.h"
#include <math.h>
#include <vector>

namespace games {
namespace {

char gNote[32];

void controls(char *l1, char *l2, size_t n) {
#ifdef GAMES_DIRECT_BUTTONS
    snprintf(l1, n, "Tap either button: drop");
#else
    snprintf(l1, n, "Any button: drop");
#endif
    snprintf(l2, n, "Line it up perfectly!");
}

void drawBlock(tft_sprite &s, int x, int y, int w, int h, uint16_t col) {
    if (w <= 0) return;
    s.fillRect(x, y, w, h, col);
    s.fillRect(x, y, w, 2, lighter(col, 100));
    s.fillRect(x, y + h - 1, w, 1, darker(col, 120));
    s.fillRect(x + w - 1, y, 1, h, darker(col, 70));
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    for (int i = 0; i < 5; i++) {
        const int w = 40 - i * 4;
        drawBlock(s, cx - w / 2, cy + 18 - i * 8, w, 8, hsv(200 + i * 25, 170, 240));
    }
    const int off = (int)(18 * sinf(t / 300.0f));
    drawBlock(s, cx - 10 + off, cy - 28, 20, 8, hsv(330, 170, 240));
}

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int BH = H >= 200 ? 12 : 9;
    const int BASE_W = W * 2 / 5;
    const int TOP_Y = H / 3;  // camera keeps the active block about here
    const int GROUND = H - 4; // world y of the pedestal top
    const uint32_t FRAME_MS = 25;
    const int startHue = random(360);

    struct Block {
        float x;
        int w;
        uint16_t col;
    };
    std::vector<Block> tower;
    tower.reserve(128);
    tower.push_back({(float)(W - BASE_W) / 2, BASE_W, hsv(startHue, 170, 240)});

    struct Debris {
        float x, y, vy;
        int w;
        uint16_t col;
        bool alive;
    } debris[6] = {};
    struct Ripple {
        int x, y, w, life;
    } ripple = {0, 0, 0, 0};

    float mx = 0, mdir = 1, cam = 0;
    int mw = BASE_W, combo = 0, perfects = 0;
    uint32_t frame = 0;
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];

    auto worldY = [&](int i) { return GROUND - (i + 1) * BH; }; // top of block i
    auto colorFor = [&](int i) { return hsv(startHue + i * 9, 170, 240); };
    auto speed = [&]() { return fminf(1.8f + tower.size() * 0.06f, 4.6f); };
    auto newMover = [&]() {
        mdir = (tower.size() % 2) ? 1.0f : -1.0f;
        mx = mdir > 0 ? -mw * 0.6f : W - mw * 0.4f;
    };
    newMover();

    auto addDebris = [&](float x, int w, int y, uint16_t col) {
        for (Debris &d : debris)
            if (!d.alive) {
                d = {x, (float)y, 0.0f, w, col, true};
                return;
            }
    };

    auto render = [&](bool showMover) {
        const int n = (int)tower.size();
        const int hue = startHue + n * 9;
        vGradient(s, 0, 0, W, H, hsv(hue + 160, 140, 70), hsv(hue + 200, 160, 25));
        const int off = (int)cam;

        // Tower (only what's on screen).
        for (int i = n - 1; i >= 0; i--) {
            const int y = worldY(i) + off;
            if (y > H) break;
            if (y + BH < 0) continue;
            drawBlock(s, (int)tower[i].x, y, tower[i].w, BH, tower[i].col);
        }
        // Pedestal under the first block.
        const int py = GROUND + off;
        if (py < H) s.fillRect((W - BASE_W) / 2 - 6, py, BASE_W + 12, H - py, rgb(30, 30, 40));

        for (const Debris &d : debris)
            if (d.alive) drawBlock(s, (int)d.x, (int)d.y + off, d.w, BH, darker(d.col, 40));

        if (ripple.life > 0) {
            const int g = 12 - ripple.life;
            s.drawRect(ripple.x - g, ripple.y + off - g, ripple.w + 2 * g, BH + 2 * g,
                       blend(C_WHITE, hsv(hue + 180, 140, 50), (uint8_t)(255 - ripple.life * 20)));
        }

        if (showMover) drawBlock(s, (int)mx, worldY(n) + off, mw, BH, colorFor(n));
        fx.draw(s);
        pops.draw(s);

        snprintf(buf, sizeof(buf), "%lu", (unsigned long)res.score);
        textShadow(s, buf, W / 2, 6, 3, C_WHITE, AL_CENTER);
        c.push();
    };

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

        // Slide back and forth.
        mx += mdir * speed();
        if (mx > W - mw * 0.4f) mdir = -1;
        if (mx < -mw * 0.6f) mdir = 1;

        if (in.action) {
            const Block &top = tower.back();
            const int n = (int)tower.size();
            const int y = worldY(n);
            if (fabsf(mx - top.x) <= 2.0f) { // perfect
                mx = top.x;
                combo++;
                perfects++;
                sfx(SFX_PERFECT);
                ripple = {(int)mx, y, mw, 12};
                if (combo >= 3 && mw < BASE_W) { // grow back a little
                    const int grow = min(8, BASE_W - mw);
                    mx -= grow / 2.0f;
                    mw += grow;
                }
                if (combo > 1) snprintf(buf, sizeof(buf), "PERFECT x%d", combo);
                else snprintf(buf, sizeof(buf), "PERFECT");
                pops.add(W / 2, y + (int)cam - 10, buf, C_WHITE);
            } else {
                const float left = fmaxf(mx, top.x), right = fminf(mx + mw, top.x + top.w);
                if (right - left < 1.0f) { // missed the tower completely
                    addDebris(mx, mw, y, colorFor(n));
                    over = true;
                    break;
                }
                combo = 0;
                sfx(SFX_BRICK);
                // Slice off the overhang and let it tumble.
                if (mx < left) addDebris(mx, (int)(left - mx), y, colorFor(n));
                if (mx + mw > right) addDebris(right, (int)(mx + mw - right), y, colorFor(n));
                mx = left;
                mw = (int)(right - left);
            }
            tower.push_back({mx, mw, colorFor(n)});
            res.score++;
            fx.burst(mx + mw / 2.0f, y + (int)cam + BH, 8, C_WHITE, 1.2f, 14);
            newMover();
        }

        // Camera follows the top of the tower.
        const float targetCam = fmaxf(0.0f, (float)(TOP_Y - worldY((int)tower.size())));
        cam += (targetCam - cam) * 0.15f;
        for (Debris &d : debris) {
            if (!d.alive) continue;
            d.vy += 0.35f;
            d.y += d.vy;
            if (d.y + cam > H + 20) d.alive = false;
        }
        if (ripple.life > 0) ripple.life--;
        fx.update(0.05f);
        pops.update();
        render(true);
    }

    // The missed block falls away.
    sfx(SFX_DIE);
    c.shake(8, 3);
    for (int i = 0; i < 30; i++) {
        for (Debris &d : debris) {
            if (!d.alive) continue;
            d.vy += 0.35f;
            d.y += d.vy;
        }
        fx.update(0.05f);
        pops.update();
        render(false);
        waitMs(28);
    }
    if (perfects > 0) {
        snprintf(gNote, sizeof(gNote), "Perfect drops: %d", perfects);
        res.note = gNote;
        res.noteColor = C_CYAN;
    }
    return res;
}

} // namespace

const GameInfo kStack = {GAME_STACK, "STACK", C_PINK, "Build the tallest tower", controls, icon, run};

} // namespace games
