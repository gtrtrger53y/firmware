// SNAKE: left/right turn the snake relative to where it's heading.
// Red apple +10, golden apple +50 (appears every 5 apples, then fades away).

#include "game_common.h"
#include <math.h>
#include <vector>

namespace games {
namespace {

void controls(char *l1, char *l2, size_t n) {
    snprintf(l1, n, "%s: turn left", leftName());
    snprintf(l2, n, "%s: turn right", rightName());
}

void icon(tft_sprite &s, int cx, int cy, uint32_t t) {
    for (int i = 0; i < 6; i++) {
        const int x = cx - 24 + i * 7;
        const int y = cy + 4 + (int)(4 * sinf(t / 200.0f + i * 0.8f));
        const uint16_t col = (i == 5) ? C_GREEN : ((i & 1) ? rgb(0, 170, 40) : rgb(0, 130, 30));
        s.fillRoundRect(x, y, 7, 7, 2, col);
    }
    s.fillCircle(cx + 16, cy - 12, 6, C_RED);
    s.fillRect(cx + 17, cy - 21, 2, 4, rgb(120, 70, 20));
    s.fillRect(cx + 19, cy - 20, 4, 2, C_GREEN);
    s.fillRect(cx + 13, cy - 15, 2, 2, C_WHITE);
}

GameResult run(Canvas &c, Pad &pad) {
    tft_sprite &s = c.spr;
    const int W = c.w, H = c.h;
    const int HUD = 12;
    const int CELL = (H >= 200) ? 10 : 8;
    const int cols = (W - 4) / CELL;
    const int rows = (H - HUD - 3) / CELL;
    const int ox = (W - cols * CELL) / 2;
    const int oy = HUD + 1 + ((H - HUD - 1) - rows * CELL) / 2;
    const int cells = cols * rows;
    const int dx[4] = {0, 1, 0, -1}; // 0 up, 1 right, 2 down, 3 left
    const int dy[4] = {-1, 0, 1, 0};
    const uint32_t FRAME_MS = 25;
    const int BONUS_STEPS = 45;
    const uint16_t field1 = rgb(6, 26, 12), field2 = rgb(10, 34, 16);

    std::vector<uint16_t> body(cells); // ring buffer of cell indexes
    std::vector<uint8_t> occ(cells, 0);
    int head = 0, len = 0, dir = 1, food = -1, bonus = -1, bonusLeft = 0;
    int8_t turns[3];
    int nTurns = 0;
    uint32_t tick = 150, eaten = 0, frame = 0;
    Particles fx;
    Popups pops;
    GameResult res;
    char buf[24];
    const uint32_t best = loadBest(GAME_SNAKE);

    const int startRow = rows / 2, startCol = cols / 2 - 3;
    for (int i = 0; i < 4; i++) {
        head = i;
        body[head] = startRow * cols + startCol + i;
        occ[body[head]] = 1;
        len++;
    }

    auto centerX = [&](int cell) { return ox + (cell % cols) * CELL + CELL / 2; };
    auto centerY = [&](int cell) { return oy + (cell / cols) * CELL + CELL / 2; };
    auto freeCell = [&](int avoid) -> int {
        int freeCount = 0;
        for (int i = 0; i < cells; i++)
            if (!occ[i] && i != avoid) freeCount++;
        if (freeCount == 0) return -1;
        int k = random(freeCount);
        for (int i = 0; i < cells; i++) {
            if (occ[i] || i == avoid) continue;
            if (k-- == 0) return i;
        }
        return -1;
    };
    food = freeCell(-1);

    auto render = [&](bool dying) {
        s.fillScreen(C_BLACK);
        for (int r = 0; r < rows; r++)
            for (int col = 0; col < cols; col++)
                s.fillRect(ox + col * CELL, oy + r * CELL, CELL, CELL, ((r + col) & 1) ? field1 : field2);
        s.drawRect(ox - 1, oy - 1, cols * CELL + 2, rows * CELL + 2, rgb(40, 110, 60));

        snprintf(buf, sizeof(buf), "SCORE %lu", (unsigned long)res.score);
        text(s, buf, 3, 2, 1, C_WHITE);
        snprintf(buf, sizeof(buf), "BEST %lu", (unsigned long)max(best, res.score));
        text(s, buf, W - 3, 2, 1, C_GREY, AL_RIGHT);
        if (bonus >= 0) { // golden apple countdown
            const int bw = 50 * bonusLeft / BONUS_STEPS;
            s.fillRect(W / 2 - 25, 4, 50, 4, C_DGREY);
            s.fillRect(W / 2 - 25, 4, bw, 4, C_GOLD);
        }

        if (food >= 0) {
            const int fx0 = centerX(food), fy0 = centerY(food);
            s.fillCircle(fx0, fy0 + 1, CELL / 2 - 1, C_RED);
            s.fillRect(fx0 - 2, fy0 - 1, 1, 1, C_WHITE);
            s.fillRect(fx0, fy0 - CELL / 2, 1, 2, rgb(120, 70, 20));
            s.fillRect(fx0 + 1, fy0 - CELL / 2, 2, 1, C_GREEN);
        }
        if (bonus >= 0 && (bonusLeft > 10 || (frame / 3) % 2)) { // blinks before vanishing
            const int bx0 = centerX(bonus), by0 = centerY(bonus);
            const int rr = CELL / 2 - 1 + ((frame / 5) % 2);
            s.fillCircle(bx0, by0, rr, C_GOLD);
            s.fillRect(bx0 - 1, by0 - 2, 2, 2, C_WHITE);
        }

        for (int i = len - 1; i >= 0; i--) {
            const int cell = body[(head - i + cells) % cells];
            const int x = ox + (cell % cols) * CELL, y = oy + (cell / cols) * CELL;
            uint16_t col;
            if (dying) col = ((frame / 3) % 2) ? C_WHITE : C_RED;
            else if (i == 0) col = C_GREEN;
            else col = blend((i & 1) ? rgb(30, 200, 60) : rgb(20, 160, 50), rgb(0, 80, 30), (uint8_t)(i * 160 / len));
            s.fillRoundRect(x, y, CELL - 1, CELL - 1, 2, col);
            if (i == 0) { // eyes, looking where it's going
                const int mx = x + CELL / 2 - 1, my = y + CELL / 2 - 1;
                const int ex = dy[dir] != 0 ? 2 : 0, ey = dx[dir] != 0 ? 2 : 0;
                s.fillRect(mx - ex + dx[dir], my - ey + dy[dir], 2, 2, C_BLACK);
                s.fillRect(mx + ex + dx[dir], my + ey + dy[dir], 2, 2, C_BLACK);
            }
        }
        fx.draw(s);
        pops.draw(s);
        c.push();
    };

    render(false);
    uint32_t nextStep = millis() + 500; // short grace before it starts moving
    uint32_t frameStart = millis();
    bool crashed = false;

    while (!crashed && !res.won) {
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
            nextStep = millis() + 400;
            frameStart = millis();
            continue;
        }
        if (in.leftHit && nTurns < 3) turns[nTurns++] = -1;
        if (in.rightHit && nTurns < 3) turns[nTurns++] = 1;

        if ((int32_t)(millis() - nextStep) >= 0) {
            nextStep += tick;
            if ((int32_t)(millis() - nextStep) > 0) nextStep = millis() + tick;

            if (nTurns) {
                dir = (dir + turns[0] + 4) % 4;
                for (int i = 1; i < nTurns; i++) turns[i - 1] = turns[i];
                nTurns--;
            }

            const int hc = body[head];
            const int hx = hc % cols + dx[dir], hy = hc / cols + dy[dir];
            if (hx < 0 || hx >= cols || hy < 0 || hy >= rows) {
                crashed = true; // wall
                break;
            }
            const int nc = hy * cols + hx;
            const bool ate = (nc == food), gotBonus = (nc == bonus);
            if (!ate && !gotBonus) occ[body[(head - len + 1 + cells) % cells]] = 0; // tail moves on
            if (occ[nc]) {
                crashed = true; // bit itself
                break;
            }
            head = (head + 1) % cells;
            body[head] = nc;
            occ[nc] = 1;

            if (ate || gotBonus) {
                len++;
                const int px = centerX(nc), py = centerY(nc);
                if (ate) {
                    eaten++;
                    res.score += 10;
                    tick = eaten * 4 >= 85 ? 65 : 150 - eaten * 4;
                    sfx(SFX_EAT);
                    fx.burst(px, py, 12, C_RED, 1.6f);
                    pops.add(px, py - 8, "+10", C_WHITE);
                    food = freeCell(bonus);
                    if (bonus < 0 && eaten % 5 == 0) {
                        bonus = freeCell(food);
                        bonusLeft = BONUS_STEPS;
                    }
                } else {
                    res.score += 50;
                    sfx(SFX_BONUS);
                    fx.burst(px, py, 22, C_GOLD, 2.2f);
                    pops.add(px, py - 8, "+50", C_GOLD);
                    bonus = -1;
                }
                if (len >= cells || food < 0) res.won = true;
            }
            if (bonus >= 0 && --bonusLeft <= 0) bonus = -1;
        }

        fx.update(0.05f);
        pops.update();
        render(false);
    }

    // Ending animation: flash + burst on a crash, confetti on a win.
    const int hc = body[head];
    if (res.won) {
        sfx(SFX_LEVEL);
        for (int i = 0; i < 6; i++) fx.burst(random(W), random(H / 2), 10, hsv(i * 60), 2.0f, 30);
    } else {
        sfx(SFX_DIE);
        c.shake(10, 3);
        fx.burst(centerX(hc), centerY(hc), 30, C_GREEN, 2.5f, 26);
    }
    for (int i = 0; i < 26; i++) {
        frame++;
        fx.update(0.06f);
        pops.update();
        render(!res.won && i < 14);
        waitMs(30);
    }
    return res;
}

} // namespace

const GameInfo kSnake = {GAME_SNAKE, "SNAKE", C_GREEN, "Eat apples. Gold = +50!", controls, icon, run};

} // namespace games
