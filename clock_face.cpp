// Styled clock face for Bruce. See clock_face.h.
//
// The digits are drawn with shapes rather than fonts: on the M5StickS3's
// display driver Bruce's setTextFont() does nothing, so this is the only way
// to get real font choices there, and it looks the same on every board.

#include "clock_face.h"
#include "core/display.h"
#include <Preferences.h>
#include <globals.h>
#include <interface.h>
#include <math.h>

namespace {

// ---------------------------------------------------------------------------
// Colors (uint16_t on purpose: LovyanGFX picks the format from the C++ type)
// ---------------------------------------------------------------------------
constexpr uint16_t rgb(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}
constexpr uint16_t C_BLACK = 0x0000;
constexpr uint16_t C_WHITE = 0xFFFF;

uint16_t blend(uint16_t a, uint16_t b, uint8_t t) {
    const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
    const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
    return (uint16_t)(((ar + ((br - ar) * t) / 255) << 11) | ((ag + ((bg - ag) * t) / 255) << 5) |
                      (ab + ((bb - ab) * t) / 255));
}

uint16_t hsv(int hue, uint8_t sat = 255, uint8_t val = 255) {
    hue %= 360;
    if (hue < 0) hue += 360;
    const int rem = (hue % 60) * 255 / 60;
    const uint8_t p = val * (255 - sat) / 255;
    const uint8_t q = val * (255 - (sat * rem) / 255) / 255;
    const uint8_t t = val * (255 - (sat * (255 - rem)) / 255) / 255;
    switch (hue / 60) {
        case 0: return rgb(val, t, p);
        case 1: return rgb(q, val, p);
        case 2: return rgb(p, val, t);
        case 3: return rgb(p, q, val);
        case 4: return rgb(t, p, val);
        default: return rgb(val, p, q);
    }
}

// ---------------------------------------------------------------------------
// Style settings
// ---------------------------------------------------------------------------
enum Font : uint8_t { FONT_DIGITAL, FONT_SLIM, FONT_BOLD, FONT_DOTS, FONT_CLASSIC, FONT_COUNT };
const char *const kFontNames[FONT_COUNT] = {"Digital", "Slim", "Bold", "LED Dots", "Classic"};

constexpr int COLOR_THEME = 0, COLOR_RAINBOW = 13, COLOR_COUNT = 14;
const char *const kColorNames[COLOR_COUNT] = {"Theme", "White", "Red",    "Orange", "Amber",  "Yellow", "Lime",
                                              "Green", "Cyan",  "Sky",    "Blue",   "Purple", "Pink",   "Rainbow"};
const uint16_t kColors[COLOR_COUNT] = {0,
                                       C_WHITE,
                                       rgb(255, 50, 50),
                                       rgb(255, 120, 0),
                                       rgb(255, 176, 0),
                                       rgb(255, 230, 0),
                                       rgb(170, 255, 0),
                                       rgb(40, 230, 60),
                                       rgb(0, 230, 255),
                                       rgb(80, 170, 255),
                                       rgb(70, 100, 255),
                                       rgb(170, 80, 255),
                                       rgb(255, 90, 180),
                                       0};

constexpr int BG_THEME = 1, BG_GLOW = 6, BG_COUNT = 7;
const char *const kBgNames[BG_COUNT] = {"Black", "Theme", "Navy", "Forest", "Wine", "Slate", "Glow"};
const uint16_t kBgs[BG_COUNT] = {C_BLACK, 0, rgb(0, 10, 40), rgb(0, 28, 12), rgb(40, 0, 14), rgb(22, 26, 34), 0};

enum SecMode : uint8_t { SEC_OFF, SEC_SMALL, SEC_BIG, SEC_BAR, SEC_COUNT };
const char *const kSecNames[SEC_COUNT] = {"Off (blinking colon)", "Small", "Big", "Progress bar"};
const char *const kSecShort[SEC_COUNT] = {"Off", "Small", "Big", "Bar"};

struct ClockStyle {
    uint8_t font = FONT_DIGITAL;
    uint8_t color = COLOR_THEME;
    uint8_t bg = 0;
    uint8_t seconds = SEC_SMALL;
    bool date = true;
    bool battery = true;
};

const char *kPrefsNs = "bruce_clock";

ClockStyle loadStyle() {
    ClockStyle st;
    Preferences p;
    if (p.begin(kPrefsNs, true)) {
        st.font = p.getUChar("font", st.font);
        st.color = p.getUChar("color", st.color);
        st.bg = p.getUChar("bg", st.bg);
        st.seconds = p.getUChar("secs", st.seconds);
        st.date = p.getBool("date", st.date);
        st.battery = p.getBool("batt", st.battery);
        p.end();
    }
    if (st.font >= FONT_COUNT) st.font = FONT_DIGITAL;
    if (st.color >= COLOR_COUNT) st.color = COLOR_THEME;
    if (st.bg >= BG_COUNT) st.bg = 0;
    if (st.seconds >= SEC_COUNT) st.seconds = SEC_SMALL;
    return st;
}

void saveStyle(const ClockStyle &st) {
    Preferences p;
    if (!p.begin(kPrefsNs, false)) return;
    p.putUChar("font", st.font);
    p.putUChar("color", st.color);
    p.putUChar("bg", st.bg);
    p.putUChar("secs", st.seconds);
    p.putBool("date", st.date);
    p.putBool("batt", st.battery);
    p.end();
}

uint16_t baseColor(const ClockStyle &st, uint32_t ms) {
    if (st.color == COLOR_THEME) return (uint16_t)bruceConfig.priColor;
    if (st.color == COLOR_RAINBOW) return hsv((int)(ms / 40));
    return kColors[st.color];
}

uint16_t bgColor(const ClockStyle &st, uint16_t fg) {
    if (st.bg == BG_THEME) return (uint16_t)bruceConfig.bgColor;
    if (st.bg == BG_GLOW) return blend(fg, C_BLACK, 222);
    return kBgs[st.bg];
}

// ---------------------------------------------------------------------------
// Text in the built-in 6x8 font, placed by hand (datums differ by backend)
// ---------------------------------------------------------------------------
enum Align : uint8_t { AL_LEFT, AL_CENTER, AL_RIGHT };

void textAt(tft_sprite &s, const char *str, int x, int y, uint8_t size, uint16_t color, Align al = AL_LEFT) {
    const int w = (int)strlen(str) * 6 * size;
    if (al == AL_CENTER) x -= w / 2;
    else if (al == AL_RIGHT) x -= w;
    s.setTextDatum((uint8_t)0);
    s.setTextSize(size);
    s.setTextColor(color);
    s.drawString(str, x, y);
}

// ---------------------------------------------------------------------------
// Digit fonts
// ---------------------------------------------------------------------------
// Seven-segment bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
const uint8_t kSeg[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};

const char *const kBold[10][5] = {
    {"###", "#.#", "#.#", "#.#", "###"}, {".#.", "##.", ".#.", ".#.", "###"}, {"###", "..#", "###", "#..", "###"},
    {"###", "..#", "###", "..#", "###"}, {"#.#", "#.#", "###", "..#", "..#"}, {"###", "#..", "###", "..#", "###"},
    {"###", "#..", "###", "#.#", "###"}, {"###", "..#", "..#", "..#", "..#"}, {"###", "#.#", "###", "#.#", "###"},
    {"###", "#.#", "###", "..#", "###"},
};

const char *const kDots[10][7] = {
    {".###.", "#...#", "#..##", "#.#.#", "##..#", "#...#", ".###."},
    {"..#..", ".##..", "..#..", "..#..", "..#..", "..#..", ".###."},
    {".###.", "#...#", "....#", "...#.", "..#..", ".#...", "#####"},
    {"#####", "...#.", "..#..", "...#.", "....#", "#...#", ".###."},
    {"...#.", "..##.", ".#.#.", "#..#.", "#####", "...#.", "...#."},
    {"#####", "#....", "####.", "....#", "....#", "#...#", ".###."},
    {"..##.", ".#...", "#....", "####.", "#...#", "#...#", ".###."},
    {"#####", "....#", "...#.", "..#..", ".#...", ".#...", ".#..."},
    {".###.", "#...#", "#...#", ".###.", "#...#", "#...#", ".###."},
    {".###.", "#...#", "#...#", ".####", "....#", "...#.", ".##.."},
};

int classicScale(int h) { return max(1, h / 7); }

int digitW(Font f, int h) {
    switch (f) {
        case FONT_DIGITAL: return (int)(h * 0.55f);
        case FONT_SLIM: return (int)(h * 0.50f);
        case FONT_BOLD: return max(2, h / 5) * 3;
        case FONT_DOTS: return h * 5 / 7;
        default: return 5 * classicScale(h);
    }
}
int colonW(Font f, int h) {
    if (f == FONT_BOLD) return max(2, h / 5);
    if (f == FONT_CLASSIC) return 2 * classicScale(h);
    return max(4, h / 5);
}
int gapW(Font f, int h) { return f == FONT_CLASSIC ? classicScale(h) : max(2, h / 10); }

void hexH(tft_sprite &s, int x0, int x1, int yc, int t, uint16_t col) {
    const int half = t / 2, top = yc - half, bot = yc - half + t - 1;
    const int bl = x0 + half, br = x1 - half;
    if (br >= bl) s.fillRect(bl, top, br - bl + 1, t, col);
    s.fillTriangle(x0, yc, bl, top, bl, bot, col);
    s.fillTriangle(x1, yc, br, top, br, bot, col);
}

void hexV(tft_sprite &s, int y0, int y1, int xc, int t, uint16_t col) {
    const int half = t / 2, left = xc - half, right = xc - half + t - 1;
    const int bt = y0 + half, bb = y1 - half;
    if (bb >= bt) s.fillRect(left, bt, t, bb - bt + 1, col);
    s.fillTriangle(xc, y0, left, bt, right, bt, col);
    s.fillTriangle(xc, y1, left, bb, right, bb, col);
}

void drawSegDigit(tft_sprite &s, int x, int y, int w, int h, int d, uint16_t on, uint16_t off, bool slim) {
    const int t = slim ? max(2, w / 7) : max(3, w / 5);
    const int half = t / 2;
    const int g = max(1, t / 3);
    const int mid = y + h / 2;
    const int xl = x + half, xr = x + w - 1 - half;
    const int yt = y + half, yb = y + h - 1 - half;
    struct Seg {
        bool horiz;
        int a, b, c; // horiz: x0, x1, yc   vert: y0, y1, xc
    };
    const Seg segs[7] = {
        {true, xl + g, xr - g, yt},   {false, yt + g, mid - g, xr}, {false, mid + g, yb - g, xr},
        {true, xl + g, xr - g, yb},   {false, mid + g, yb - g, xl}, {false, yt + g, mid - g, xl},
        {true, xl + g, xr - g, mid},
    };
    // Unlit segments first (a faint "LCD ghost" in Digital), then lit ones on top.
    for (int pass = 0; pass < 2; pass++) {
        for (int i = 0; i < 7; i++) {
            const bool lit = (kSeg[d] >> i) & 1;
            if (lit != (pass == 1)) continue;
            if (!lit && slim) continue;
            const uint16_t col = lit ? on : off;
            const Seg &sg = segs[i];
            if (slim) {
                if (sg.horiz) s.fillRoundRect(sg.a - half, sg.c - half, sg.b - sg.a + t, t, half, col);
                else s.fillRoundRect(sg.c - half, sg.a - half, t, sg.b - sg.a + t, half, col);
            } else {
                if (sg.horiz) hexH(s, sg.a, sg.b, sg.c, t, col);
                else hexV(s, sg.a, sg.b, sg.c, t, col);
            }
        }
    }
}

void drawBoldDigit(tft_sprite &s, int x, int y, int h, int d, uint16_t col) {
    const int b = max(2, h / 5);
    const int r = max(1, b / 4);
    for (int row = 0; row < 5; row++)
        for (int c = 0; c < 3; c++)
            if (kBold[d][row][c] == '#') s.fillRoundRect(x + c * b, y + row * b, b - 1, b - 1, r, col);
}

// LED size for the dot-matrix font: round when there's room, rounded squares
// when small (tiny circles turn into crosses on a real screen).
int ledSize(int w, int h) { return max(2, (int)(fminf(w / 5.0f, h / 7.0f) * 0.82f)); }

void drawLed(tft_sprite &s, int cx, int cy, int d, uint16_t col) {
    s.fillRoundRect(cx - d / 2, cy - d / 2, d, d, d >= 6 ? d / 2 : d / 3, col);
}

void drawDotDigit(tft_sprite &s, int x, int y, int w, int h, int d, uint16_t on, uint16_t off) {
    const float px = w / 5.0f, py = h / 7.0f;
    const int led = ledSize(w, h);
    for (int row = 0; row < 7; row++)
        for (int c = 0; c < 5; c++) {
            const bool lit = kDots[d][row][c] == '#';
            drawLed(s, x + (int)(c * px + px / 2), y + (int)(row * py + py / 2), led, lit ? on : off);
        }
}

void drawClassicDigit(tft_sprite &s, int x, int y, int h, char ch, uint16_t col) {
    const char str[2] = {ch, 0};
    s.setTextDatum((uint8_t)0);
    s.setTextSize(classicScale(h));
    s.setTextColor(col);
    s.drawString(str, x, y);
}

void drawColon(tft_sprite &s, Font f, int x, int y, int h, uint16_t col) {
    const int w = colonW(f, h), cx = x + w / 2;
    switch (f) {
        case FONT_DIGITAL: {
            const int sq = max(2, (int)(h * 0.11f));
            s.fillRect(cx - sq / 2, y + (int)(h * 0.3f) - sq / 2, sq, sq, col);
            s.fillRect(cx - sq / 2, y + (int)(h * 0.7f) - sq / 2, sq, sq, col);
            break;
        }
        case FONT_SLIM: {
            const int r = max(1, h / 18);
            s.fillCircle(cx, y + (int)(h * 0.3f), r, col);
            s.fillCircle(cx, y + (int)(h * 0.7f), r, col);
            break;
        }
        case FONT_BOLD: {
            const int b = max(2, h / 5), r = max(1, b / 4);
            s.fillRoundRect(x, y + b, b - 1, b - 1, r, col);
            s.fillRoundRect(x, y + 3 * b, b - 1, b - 1, r, col);
            break;
        }
        case FONT_DOTS: {
            const float py = h / 7.0f;
            const int led = ledSize(digitW(f, h), h);
            drawLed(s, cx, y + (int)(2.5f * py), led, col);
            drawLed(s, cx, y + (int)(4.5f * py), led, col);
            break;
        }
        default: {
            const int sc = classicScale(h);
            s.fillRect(x, y + 2 * sc, 2 * sc, sc, col);
            s.fillRect(x, y + 5 * sc, 2 * sc, sc, col);
            break;
        }
    }
}

int measure(const char *str, Font f, int h) {
    int w = 0;
    for (const char *p = str; *p; p++) {
        if (p != str) w += gapW(f, h);
        w += (*p == ':') ? colonW(f, h) : digitW(f, h);
    }
    return w;
}

// Draws digits and colons; `colorAt(i)` gives the color for character i.
template <typename ColorFn>
int drawStr(tft_sprite &s, const char *str, int x, int y, Font f, int h, ColorFn colorAt, uint16_t bg, bool colonOn) {
    int i = 0;
    for (const char *p = str; *p; p++, i++) {
        if (p != str) x += gapW(f, h);
        const uint16_t on = colorAt(i);
        if (*p == ':') {
            if (colonOn) drawColon(s, f, x, y, h, on);
            x += colonW(f, h);
            continue;
        }
        if (*p < '0' || *p > '9') {
            x += digitW(f, h);
            continue;
        }
        const int d = *p - '0', w = digitW(f, h);
        // Unlit segments/LEDs: just a hint, so digits stay easy to read.
        const uint16_t ghost = blend(on, bg, f == FONT_DOTS ? 238 : 236);
        switch (f) {
            case FONT_DIGITAL: drawSegDigit(s, x, y, w, h, d, on, ghost, false); break;
            case FONT_SLIM: drawSegDigit(s, x, y, w, h, d, on, ghost, true); break;
            case FONT_BOLD: drawBoldDigit(s, x, y, h, d, on); break;
            case FONT_DOTS: drawDotDigit(s, x, y, w, h, d, on, ghost); break;
            default: drawClassicDigit(s, x, y, h, *p, on); break;
        }
        x += w;
    }
    return x;
}

void drawBattery(tft_sprite &s, int xRight, int y, int pct, uint16_t ink) {
    const int bw = 18, bh = 9, x = xRight - bw - 2;
    s.drawRect(x, y, bw, bh, ink);
    s.fillRect(x + bw, y + 3, 2, bh - 6, ink);
    const uint16_t fc = pct > 50 ? rgb(60, 220, 80) : (pct > 20 ? rgb(255, 200, 0) : rgb(255, 60, 60));
    s.fillRect(x + 2, y + 2, (bw - 4) * pct / 100, bh - 4, fc);
    char b[8];
    snprintf(b, sizeof(b), "%d%%", pct);
    textAt(s, b, x - 4, y + 1, 1, ink, AL_RIGHT);
}

struct FaceState {
    uint32_t ms;
    float secFrac;   // 0..1 progress through the current second
    bool colonOn;
    const char *hint1, *hint2;
    const char *toast;
};

void drawFace(tft_sprite &s, int W, int H, const ClockStyle &st, const struct tm &t, const FaceState &fs) {
    const Font f = (Font)st.font;
    const uint16_t fg = baseColor(st, fs.ms);
    const uint16_t bg = bgColor(st, fg);
    const uint16_t dimInk = blend(fg, bg, 110);
    s.fillScreen(bg);

    // Top row: date and battery.
    int top = 6;
    if (st.date || st.battery) {
        if (st.date) {
            static const char *const kDays[7] = {"SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"};
            static const char *const kMonths[12] = {"JAN", "FEB", "MAR", "APR", "MAY", "JUN",
                                                    "JUL", "AUG", "SEP", "OCT", "NOV", "DEC"};
            char d[20];
            snprintf(d, sizeof(d), "%s %d %s", kDays[((t.tm_wday % 7) + 7) % 7], t.tm_mday,
                     kMonths[((t.tm_mon % 12) + 12) % 12]);
            textAt(s, d, 8, 7, 1, dimInk);
        }
        if (st.battery) {
            const int bat = getBattery();
            if (bat > 0 && bat <= 100) drawBattery(s, W - 6, 6, bat, dimInk);
        }
        top = 20;
    }

    // Time text.
    const bool h24 = bruceConfig.clock24hr;
    int hour = t.tm_hour;
    const char *ampm = nullptr;
    if (!h24) {
        ampm = hour < 12 ? "AM" : "PM";
        hour %= 12;
        if (hour == 0) hour = 12;
    }
    char hm[12], ss[4];
    if (st.seconds == SEC_BIG) {
        if (h24) snprintf(hm, sizeof(hm), "%02d:%02d:%02d", hour, t.tm_min, t.tm_sec);
        else snprintf(hm, sizeof(hm), "%d:%02d:%02d", hour, t.tm_min, t.tm_sec);
    } else {
        if (h24) snprintf(hm, sizeof(hm), "%02d:%02d", hour, t.tm_min);
        else snprintf(hm, sizeof(hm), "%d:%02d", hour, t.tm_min);
    }
    snprintf(ss, sizeof(ss), "%02d", t.tm_sec);

    // Size the digits to fill the space.
    const int bottomReserve = st.seconds == SEC_BAR ? 16 : 6;
    const int availH = H - top - bottomReserve;
    const uint8_t ampmSize = availH >= 70 ? 2 : 1;
    const int ampmW = ampm ? (int)(2 * 6 * ampmSize) + 6 : 0;
    const int maxW = W - 16;
    auto smallH = [&](int h) { return max(8, (int)(h * 0.45f)); };
    auto totalW = [&](int h) {
        int w = measure(hm, f, h) + ampmW;
        if (st.seconds == SEC_SMALL) w += gapW(f, h) * 2 + measure(ss, f, smallH(h));
        return w;
    };
    int h = min(availH, (int)(H * 0.62f));
    while (h > 10 && totalW(h) > maxW) h--;
    if (f == FONT_CLASSIC) h = 7 * classicScale(h);
    if (f == FONT_BOLD) h = 5 * max(2, h / 5);

    const int tw = totalW(h);
    const int x0 = (W - tw) / 2;
    const int y0 = top + (availH - h) / 2;
    const bool rainbow = st.color == COLOR_RAINBOW;
    auto colorAt = [&](int i) { return rainbow ? hsv((int)(fs.ms / 25) + i * 40) : fg; };

    int x = drawStr(s, hm, x0, y0, f, h, colorAt, bg, fs.colonOn);
    if (st.seconds == SEC_SMALL) {
        const int sh = smallH(h);
        const int n = (int)strlen(hm);
        auto secColor = [&](int i) { return blend(colorAt(n + i), bg, 50); };
        x = drawStr(s, ss, x + gapW(f, h) * 2, y0 + h - sh, f, sh, secColor, bg, true);
    }
    if (ampm) textAt(s, ampm, x0 + tw, y0 + 2, ampmSize, dimInk, AL_RIGHT);

    if (st.seconds == SEC_BAR) {
        const int bx = x0, bw = tw - ampmW, by = y0 + h + 7;
        const float prog = (t.tm_sec + fs.secFrac) / 60.0f;
        s.fillRoundRect(bx, by, bw, 5, 2, blend(fg, bg, 200));
        const int fill = (int)(bw * prog);
        if (rainbow) {
            for (int i = 0; i < fill; i += 3) s.fillRect(bx + i, by, min(3, fill - i), 5, hsv((int)(fs.ms / 25) + i * 2));
        } else if (fill > 0) {
            s.fillRoundRect(bx, by, fill, 5, 2, fg);
        }
        for (int q = 1; q < 4; q++) s.fillRect(bx + bw * q / 4, by - 2, 1, 9, dimInk); // 15 s ticks
    }

    if (!clock_set && !fs.hint1) textAt(s, "Set the time: Config > Clock", W / 2, H - 11, 1, dimInk, AL_CENTER);

    if (fs.hint1) {
        s.fillRect(0, H - 24, W, 24, blend(bg, C_BLACK, 140));
        textAt(s, fs.hint1, W / 2, H - 21, 1, C_WHITE, AL_CENTER);
        textAt(s, fs.hint2, W / 2, H - 11, 1, blend(C_WHITE, bg, 90), AL_CENTER);
    }
    if (fs.toast) {
        const int tw2 = (int)strlen(fs.toast) * 6 + 16;
        s.fillRoundRect((W - tw2) / 2, H - 20, tw2, 15, 7, blend(bg, C_BLACK, 160));
        s.drawRoundRect((W - tw2) / 2, H - 20, tw2, 15, 7, fg);
        textAt(s, fs.toast, W / 2, H - 16, 1, C_WHITE, AL_CENTER);
    }
}

// Shows a list and returns the chosen index (or `current` if backed out).
uint8_t pickFrom(const char *title, const char *const *names, int n, uint8_t current) {
    int chosen = -1;
    options.clear();
    for (int i = 0; i < n; i++) options.push_back(Option(names[i], [&chosen, i]() { chosen = i; }, i == current));
    loopOptions(options, MENU_TYPE_SUBMENU, title, current);
    return chosen >= 0 ? (uint8_t)chosen : current;
}

} // namespace

bool clockFaceRun(bool showMenuHint, ClockNowFn now) {
    tft_sprite spr(&tft);
    spr.setColorDepth(16);
    if (!spr.createSprite(tftWidth, tftHeight)) {
        spr.setColorDepth(8);
        if (!spr.createSprite(tftWidth, tftHeight)) return false;
    }
    ClockStyle st = loadStyle();

    // Same as Bruce's clock: the press that opened us shouldn't count.
    tft.fillScreen(bruceConfig.bgColor);
    delay(300);

#if defined(SEL_BTN) && defined(DW_BTN) && defined(BTN_ALIAS)
    const char *hint1 = "Tap side: color   2x tap: font";
    const char *hint2 = showMenuHint ? BTN_ALIAS ": menu   Hold side: exit" : BTN_ALIAS " or hold side: exit";
#else
    const char *hint1 = "Next: color   Prev: font";
    const char *hint2 = showMenuHint ? "OK: menu   Esc: exit" : "OK or Esc: exit";
#endif

    const uint32_t start = millis();
    uint32_t toastUntil = 0, secChangedAt = millis(), lastDraw = 0, lastPoll = 0;
    char toast[32] = "";
    struct tm t = now();
    int lastSec = t.tm_sec;
    bool dirty = true;

    for (;;) {
        const uint32_t ms = millis();
        if (ms - lastPoll >= 100) {
            lastPoll = ms;
            t = now();
            if (t.tm_sec != lastSec) {
                lastSec = t.tm_sec;
                secChangedAt = ms;
                dirty = true;
            }
        }
        const bool hints = ms - start < 5000;
        const bool showToast = (int32_t)(toastUntil - ms) > 0;
        const bool animated = st.color == COLOR_RAINBOW || st.seconds == SEC_BAR || st.seconds == SEC_OFF ||
                              showToast || hints || ms - start < 5200;
        if (dirty || (animated && ms - lastDraw >= 100)) {
            FaceState fs;
            fs.ms = ms;
            fs.secFrac = fminf(0.999f, (ms - secChangedAt) / 1000.0f);
            fs.colonOn = st.seconds != SEC_OFF || (ms - secChangedAt) < 500;
            fs.hint1 = hints ? hint1 : nullptr;
            fs.hint2 = hint2;
            fs.toast = showToast ? toast : nullptr;
            drawFace(spr, tftWidth, tftHeight, st, t, fs);
            spr.pushSprite(0, 0);
            lastDraw = ms;
            dirty = false;
        }

        // Buttons work like Bruce's own menus.
#ifdef HAS_3_BUTTONS
        if (EscPress && PrevPress) EscPress = false;
#endif
        if (check(SelPress)) {
            if (!showMenuHint) returnToMenu = true;
            break;
        }
        if (check(EscPress)) {
            returnToMenu = true;
            break;
        }
        if (check(NextPress)) {
            st.color = (st.color + 1) % COLOR_COUNT;
            saveStyle(st);
            snprintf(toast, sizeof(toast), "Color: %s", kColorNames[st.color]);
            toastUntil = millis() + 1500;
            dirty = true;
        }
        if (check(PrevPress)) {
            st.font = (st.font + 1) % FONT_COUNT;
            saveStyle(st);
            snprintf(toast, sizeof(toast), "Font: %s", kFontNames[st.font]);
            toastUntil = millis() + 1500;
            dirty = true;
        }
        delay(10);
    }

    spr.deleteSprite();
    tft.fillScreen(bruceConfig.bgColor);
    return true;
}

void clockStyleMenu() {
    enum { A_FONT, A_COLOR, A_BG, A_SECS, A_DATE, A_BATT, A_BACK };
    int index = 0;
    while (true) {
        ClockStyle st = loadStyle();
        int action = -1;
        options.clear();
        options.push_back(Option(String("Font: ") + kFontNames[st.font], [&action]() { action = A_FONT; }));
        options.push_back(Option(String("Color: ") + kColorNames[st.color], [&action]() { action = A_COLOR; }));
        options.push_back(Option(String("Background: ") + kBgNames[st.bg], [&action]() { action = A_BG; }));
        options.push_back(Option(String("Seconds: ") + kSecShort[st.seconds], [&action]() { action = A_SECS; }));
        options.push_back(Option(String("Date: ") + (st.date ? "ON" : "OFF"), [&action]() { action = A_DATE; }));
        options.push_back(Option(String("Battery: ") + (st.battery ? "ON" : "OFF"), [&action]() {
            action = A_BATT;
        }));
        options.push_back(Option("Back to Clock", [&action]() { action = A_BACK; }));

        index = loopOptions(options, MENU_TYPE_SUBMENU, "Clock Style", index);
        if (index < 0 || returnToMenu || action == A_BACK || action < 0) break;

        switch (action) {
            case A_FONT: st.font = pickFrom("Font", kFontNames, FONT_COUNT, st.font); break;
            case A_COLOR: st.color = pickFrom("Color", kColorNames, COLOR_COUNT, st.color); break;
            case A_BG: st.bg = pickFrom("Background", kBgNames, BG_COUNT, st.bg); break;
            case A_SECS: st.seconds = pickFrom("Seconds", kSecNames, SEC_COUNT, st.seconds); break;
            case A_DATE: st.date = !st.date; break;
            case A_BATT: st.battery = !st.battery; break;
        }
        saveStyle(st);
    }
}
