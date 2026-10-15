/*==============================================================================
 *  SATISFY  —  a satisfying terminal toybox
 *------------------------------------------------------------------------------
 *  Nine little worlds in one dependency-free C++17 program:
 *
 *    1  FALLING SAND   reacting materials (lava+water, acid, fire, plants...)
 *    2  LIFE           colourful Conway's Game of Life
 *    3  RIPPLE         real water-surface wave equation
 *    4  PARTICLES      attract / repel playground with glowing trails
 *    5  FIREWORKS      rockets, rings, willows, glitter
 *    6  BOIDS          flocking birds
 *    7  PLASMA         generative colour fields (4 formulas)
 *    8  RAIN           falling glyph rain
 *    9  ZEN            breathing circle, mandala, floating bokeh
 *
 *  Extras: true-colour half-block renderer, SGR mouse (paint, erase, scroll),
 *  colour themes, brush cursor, auto-demo mode, toasts, help overlay,
 *  screenshots to .ans files, live FPS HUD, graceful resize + cleanup.
 *
 *  build:  make        (or: g++ -std=c++17 -O2 -o satisfy satisfy.cpp)
 *  run:    ./satisfy
 *============================================================================*/

#include <sys/ioctl.h>
#include <sys/select.h>
#include <termios.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdarg>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <random>
#include <string>
#include <vector>

/*============================ small utilities ==============================*/

using u32 = uint32_t;

static std::mt19937 g_rng(0xC0FFEEu);
static inline int rnd(int n) { return n <= 0 ? 0 : (int)(g_rng() % (unsigned)n); }
static inline int rndi(int a, int b) { return a + rnd(b - a + 1); }
static inline float rndf() { return (float)(g_rng() % 1000000u) / 1000000.0f; }
static inline float frnd(float a, float b) { return a + (b - a) * rndf(); }
static inline int clampi(int v, int lo, int hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static inline float lerpf(float a, float b, float t) { return a + (b - a) * t; }

static std::string fstr(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n < 0) return std::string();
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    return std::string(buf, (size_t)n);
}

static bool hasCI(const char *s, const char *sub) {
    if (!s || !sub) return false;
    size_t n = strlen(sub);
    for (const char *p = s; *p; ++p) {
        size_t k = 0;
        while (k < n && p[k] && tolower((unsigned char)p[k]) == tolower((unsigned char)sub[k])) ++k;
        if (k == n) return true;
    }
    return false;
}

/*=============================== colours ===================================*/

static inline u32 rgb(int r, int g, int b) {
    return ((u32)clampi(r, 0, 255) << 16) | ((u32)clampi(g, 0, 255) << 8) | (u32)clampi(b, 0, 255);
}
static inline int cr(u32 c) { return (int)((c >> 16) & 255u); }
static inline int cg(u32 c) { return (int)((c >> 8) & 255u); }
static inline int cb(u32 c) { return (int)(c & 255u); }
static inline u32 scaleC(u32 c, float f) { return rgb((int)(cr(c) * f), (int)(cg(c) * f), (int)(cb(c) * f)); }
static inline u32 addC(u32 a, u32 b) { return rgb(cr(a) + cr(b), cg(a) + cg(b), cb(a) + cb(b)); }
static inline u32 mixC(u32 a, u32 b, float t) {
    t = clampf(t, 0.0f, 1.0f);
    return rgb((int)(cr(a) + (cr(b) - cr(a)) * t),
               (int)(cg(a) + (cg(b) - cg(a)) * t),
               (int)(cb(a) + (cb(b) - cb(a)) * t));
}
static u32 hsv(float h, float s, float v) {
    h = std::fmod(h, 360.0f);
    if (h < 0) h += 360.0f;
    s = clampf(s, 0.0f, 1.0f);
    v = clampf(v, 0.0f, 1.0f);
    float c = v * s;
    float x = c * (1.0f - std::fabs(std::fmod(h / 60.0f, 2.0f) - 1.0f));
    float m = v - c;
    float r = 0, g = 0, b = 0;
    if (h < 60)       { r = c; g = x; }
    else if (h < 120) { r = x; g = c; }
    else if (h < 180) { g = c; b = x; }
    else if (h < 240) { g = x; b = c; }
    else if (h < 300) { r = x; b = c; }
    else              { r = c; b = x; }
    return rgb((int)((r + m) * 255.0f), (int)((g + m) * 255.0f), (int)((b + m) * 255.0f));
}
static inline float lum(u32 c) {
    return 0.299f * (float)cr(c) + 0.587f * (float)cg(c) + 0.114f * (float)cb(c);
}

/*================================ UTF-8 ====================================*/

static uint32_t utf8Next(const std::string &s, size_t &i) {
    unsigned char c = (unsigned char)s[i];
    uint32_t cp = 0;
    int len = 1;
    if (c < 0x80) { cp = c; }
    else if ((c & 0xE0u) == 0xC0u) { cp = c & 0x1Fu; len = 2; }
    else if ((c & 0xF0u) == 0xE0u) { cp = c & 0x0Fu; len = 3; }
    else if ((c & 0xF8u) == 0xF0u) { cp = c & 0x07u; len = 4; }
    else { cp = c; }
    for (int k = 1; k < len; ++k)
        if (i + (size_t)k < s.size()) cp = (cp << 6) | ((unsigned char)s[i + (size_t)k] & 0x3Fu);
    i += (size_t)len;
    if (i > s.size()) i = s.size();
    return cp;
}

static void utf8Enc(uint32_t cp, std::string &o) {
    if (cp < 0x80u) o.push_back((char)cp);
    else if (cp < 0x800u) {
        o.push_back((char)(0xC0u | (cp >> 6)));
        o.push_back((char)(0x80u | (cp & 0x3Fu)));
    } else if (cp < 0x10000u) {
        o.push_back((char)(0xE0u | (cp >> 12)));
        o.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        o.push_back((char)(0x80u | (cp & 0x3Fu)));
    } else {
        o.push_back((char)(0xF0u | (cp >> 18)));
        o.push_back((char)(0x80u | ((cp >> 12) & 0x3Fu)));
        o.push_back((char)(0x80u | ((cp >> 6) & 0x3Fu)));
        o.push_back((char)(0x80u | (cp & 0x3Fu)));
    }
}

/*============================ framebuffer ==================================*/

struct Cell {
    u32 fg = 0;
    u32 bg = 0;
    uint32_t ch = ' ';   // ' ' = solid bg, 0 = half-block pixel pair, else glyph
};
static inline bool operator==(const Cell &a, const Cell &b) {
    return a.fg == b.fg && a.bg == b.bg && a.ch == b.ch;
}
static inline bool operator!=(const Cell &a, const Cell &b) { return !(a == b); }

static const char *const HALFSTR = "\xE2\x96\x80";          // U+2580 upper half block
static const char *const RAMPSTR = " .:-=+*#%@";

static bool g_utf8 = true;
static bool g_tty = true;
static bool g_mouse = true;

static void putCell(std::vector<Cell> &fb, int W, int H, int x, int y, uint32_t ch, u32 fg, u32 bg) {
    if (x < 0 || y < 0 || x >= W || y >= H) return;
    Cell &c = fb[(size_t)y * (size_t)W + (size_t)x];
    c.ch = ch;
    c.fg = fg;
    c.bg = bg;
}

static void drawText(std::vector<Cell> &fb, int W, int H, int x, int y, const std::string &s,
                     u32 fg, u32 bg, bool solid = true) {
    if (y < 0 || y >= H) return;
    int cx = x;
    for (size_t i = 0; i < s.size();) {
        uint32_t cp = utf8Next(s, i);
        if (cx >= 0 && cx < W) {
            Cell &c = fb[(size_t)y * (size_t)W + (size_t)cx];
            c.ch = cp;
            c.fg = fg;
            if (solid) c.bg = bg;
        }
        ++cx;
    }
}

static void fillRect(std::vector<Cell> &fb, int W, int H, int x, int y, int w, int h, u32 bg) {
    for (int j = y; j < y + h; ++j)
        for (int i = x; i < x + w; ++i)
            putCell(fb, W, H, i, j, ' ', 0, bg);
}

static void drawFrame(std::vector<Cell> &fb, int W, int H, int x, int y, int w, int h,
                      const std::string &title, u32 fg, u32 bg) {
    for (int i = 0; i < w; ++i) {
        putCell(fb, W, H, x + i, y, '-', fg, bg);
        putCell(fb, W, H, x + i, y + h - 1, '-', fg, bg);
    }
    for (int j = 0; j < h; ++j) {
        putCell(fb, W, H, x, y + j, '|', fg, bg);
        putCell(fb, W, H, x + w - 1, y + j, '|', fg, bg);
    }
    putCell(fb, W, H, x, y, '+', fg, bg);
    putCell(fb, W, H, x + w - 1, y, '+', fg, bg);
    putCell(fb, W, H, x, y + h - 1, '+', fg, bg);
    putCell(fb, W, H, x + w - 1, y + h - 1, '+', fg, bg);
    if (!title.empty()) drawText(fb, W, H, x + 2, y, " " + title + " ", fg, bg, true);
}

/*--- pixel helpers: pixel space is W wide and (2 * ph) tall, origin at row 1 --*/

static inline void pxSet(std::vector<Cell> &fb, int W, int ph, int x, int y, u32 col) {
    if (x < 0 || y < 0 || x >= W) return;
    int row = 1 + (y >> 1);
    if (row >= 1 + ph) return;
    Cell &c = fb[(size_t)row * (size_t)W + (size_t)x];
    c.ch = 0;
    if ((y & 1) == 0) c.fg = col;
    else c.bg = col;
}

static inline u32 pxGet(const std::vector<Cell> &fb, int W, int ph, int x, int y) {
    if (x < 0 || y < 0 || x >= W) return 0;
    int row = 1 + (y >> 1);
    if (row >= 1 + ph) return 0;
    const Cell &c = fb[(size_t)row * (size_t)W + (size_t)x];
    if (c.ch != 0) return c.bg;
    return (y & 1) == 0 ? c.fg : c.bg;
}

static inline void pxBlend(std::vector<Cell> &fb, int W, int ph, int x, int y, u32 col, float a) {
    if (a <= 0.0f) return;
    if (a > 1.0f) a = 1.0f;
    u32 old = pxGet(fb, W, ph, x, y);
    pxSet(fb, W, ph, x, y, mixC(old, col, a));
}

static inline void pxAdd(std::vector<Cell> &fb, int W, int ph, int x, int y, u32 col) {
    u32 old = pxGet(fb, W, ph, x, y);
    pxSet(fb, W, ph, x, y, addC(old, col));
}

static void pxDisc(std::vector<Cell> &fb, int W, int ph, int cx, int cy, int r, u32 col, float a = 1.0f) {
    int r2 = r * r;
    for (int y = -r; y <= r; ++y)
        for (int x = -r; x <= r; ++x)
            if (x * x + y * y <= r2) pxBlend(fb, W, ph, cx + x, cy + y, col, a);
}

/*================================ themes ===================================*/

struct Theme {
    const char *name;
    float h0, h1, sat;
};
static const Theme THEMES[] = {
    { "NEON",    160.0f, 330.0f, 0.85f },
    { "SUNSET",  335.0f, 405.0f, 0.90f },
    { "OCEAN",   185.0f, 265.0f, 0.80f },
    { "CANDY",   280.0f, 400.0f, 0.75f },
    { "FOREST",   70.0f, 175.0f, 0.70f },
    { "RAINBOW",   0.0f, 360.0f, 0.90f },
};
static const int NTHEMES = (int)(sizeof THEMES / sizeof THEMES[0]);

/*============================== terminal ===================================*/

enum Key {
    K_UP = 1000, K_DOWN, K_LEFT, K_RIGHT, K_ESC, K_TAB, K_ENTER, K_BACK
};

struct Input {
    bool quit = false;
    bool eof = false;
    std::vector<int> keys;
    int mx = -1, my = -1;      // 0-based cell coords
    bool motion = false;
    bool lmb = false, rmb = false, mmb = false;
    bool lpress = false, rpress = false, lrel = false;
    int wheel = 0;
};

static std::string g_inbuf;

static void handleMouse(long b, long x, long y, bool press, Input &in) {
    in.mx = (int)x;
    in.my = (int)y;
    if (b & 64L) {                       // wheel
        in.wheel += (b & 1L) ? -1 : 1;
        return;
    }
    long base = b & 3L;
    if (b & 32L) {                       // motion: button state unchanged
        in.motion = true;
        if (base == 0) in.lmb = true;
        else if (base == 2) in.rmb = true;
        else if (base == 1) in.mmb = true;
        return;
    }
    in.motion = true;
    if (press) {
        if (base == 0) { in.lmb = true; in.lpress = true; }
        else if (base == 1) in.mmb = true;
        else if (base == 2) { in.rmb = true; in.rpress = true; }
    } else {
        if (base == 0) { if (in.lmb) in.lrel = true; in.lmb = false; }
        else if (base == 1) in.mmb = false;
        else if (base == 2) in.rmb = false;
    }
}

static void feedInput(const char *data, size_t n, Input &in) {
    g_inbuf.append(data, n);
    size_t i = 0;
    const size_t sz = g_inbuf.size();
    while (i < sz) {
        unsigned char c = (unsigned char)g_inbuf[i];
        if (c == 0x1b) {
            if (i + 3 <= sz && g_inbuf.compare(i, 3, "\x1b[<") == 0) {
                size_t j = i + 3;
                long b = 0, x = 0, y = 0;
                bool ok = false;
                auto num = [&](long &v) -> bool {
                    if (j >= g_inbuf.size() || !isdigit((unsigned char)g_inbuf[j])) return false;
                    v = 0;
                    while (j < g_inbuf.size() && isdigit((unsigned char)g_inbuf[j])) {
                        v = v * 10 + (g_inbuf[j] - '0');
                        ++j;
                    }
                    return true;
                };
                if (num(b) && j < g_inbuf.size() && g_inbuf[j] == ';') {
                    ++j;
                    if (num(x) && j < g_inbuf.size() && g_inbuf[j] == ';') {
                        ++j;
                        if (num(y) && j < g_inbuf.size() && (g_inbuf[j] == 'M' || g_inbuf[j] == 'm'))
                            ok = true;
                    }
                }
                if (!ok) break;                      // incomplete: wait for more bytes
                handleMouse(b, x - 1, y - 1, g_inbuf[j] == 'M', in);
                i = j + 1;
                continue;
            }
            if (i + 3 <= sz) {
                char c3 = g_inbuf[i + 2];
                int k = -1;
                if (c3 == 'A') k = K_UP;
                else if (c3 == 'B') k = K_DOWN;
                else if (c3 == 'C') k = K_RIGHT;
                else if (c3 == 'D') k = K_LEFT;
                if (k >= 0 && g_inbuf[i + 1] == '[') { in.keys.push_back(k); i += 3; continue; }
            }
            if (i + 2 > sz) { in.keys.push_back(K_ESC); i = sz; continue; }  // lone ESC
            // unknown CSI: swallow until a final letter
            size_t j = i + 2;
            while (j < sz && !((g_inbuf[j] >= 'A' && g_inbuf[j] <= 'Z') ||
                               (g_inbuf[j] >= 'a' && g_inbuf[j] <= 'z') || g_inbuf[j] == '~'))
                ++j;
            if (j >= sz) break;                      // incomplete
            i = j + 1;
            continue;
        }
        if (c == '\r' || c == '\n') in.keys.push_back(K_ENTER);
        else if (c == '\t') in.keys.push_back(K_TAB);
        else if (c == 0x7f || c == 0x08) in.keys.push_back(K_BACK);
        else if (c == 3) in.quit = true;
        else in.keys.push_back((int)c);
        ++i;
    }
    g_inbuf.erase(0, i);
}

static struct termios g_origTerm;
static bool g_termSaved = false;
static volatile sig_atomic_t g_resized = 0;
static volatile sig_atomic_t g_stopped = 0;

static void restoreTerminal() {
    if (!g_termSaved) return;
    static const char *leave = "\x1b[?1006l\x1b[?1003l\x1b[?1002l\x1b[?1000l\x1b[?25h\x1b[?1049l\x1b[0m";
    ssize_t rc = write(1, leave, strlen(leave));
    (void)rc;
    tcsetattr(0, TCSAFLUSH, &g_origTerm);
    g_termSaved = false;
}

static void onWinch(int) { g_resized = 1; }
static void onStop(int) { g_stopped = 1; }

static void setupTerminal() {
    if (tcgetattr(0, &g_origTerm) == 0) {
        struct termios t = g_origTerm;
        t.c_lflag &= (tcflag_t) ~(ICANON | ECHO | ISIG | IEXTEN);
        t.c_iflag &= (tcflag_t) ~(IXON | ICRNL | BRKINT | INPCK | ISTRIP);
        t.c_oflag &= (tcflag_t) ~(OPOST);
        t.c_cc[VMIN] = 0;
        t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t);
        g_termSaved = true;
        atexit(restoreTerminal);
    }
    std::string enter;
    if (g_mouse)
        enter = "\x1b[?1049h\x1b[?25l\x1b[?1002h\x1b[?1003h\x1b[?1006h\x1b[2J\x1b[H";
    else
        enter = "\x1b[?1049h\x1b[?25l\x1b[2J\x1b[H";
    if (g_tty) {
        ssize_t rc = write(1, enter.data(), enter.size());
        (void)rc;
    }
    signal(SIGWINCH, onWinch);
    signal(SIGTERM, onStop);
    signal(SIGHUP, onStop);
}

static void readInput(Input &in) {
    char buf[4096];
    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(0, &rf);
        struct timeval tv;
        tv.tv_sec = 0;
        tv.tv_usec = 0;
        if (select(1, &rf, NULL, NULL, &tv) <= 0) break;   // never block waiting for input
        ssize_t n = ::read(0, buf, sizeof buf);
        if (n > 0) { feedInput(buf, (size_t)n, in); continue; }
        if (n == 0) { if (!g_tty) in.eof = true; break; }   // end of file on stdin
        if (errno == EINTR) continue;
        break;
    }
}

static void sleepFor(double seconds) {
    if (seconds <= 0.0) return;
    if (seconds > 0.25) seconds = 0.25;
    if (g_tty) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(0, &rf);
        struct timeval tv;
        tv.tv_sec = (time_t)seconds;
        tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1e6);
        select(1, &rf, NULL, NULL, &tv);
    } else {
        struct timeval tv;
        tv.tv_sec = (time_t)seconds;
        tv.tv_usec = (long)((seconds - (double)tv.tv_sec) * 1e6);
        select(0, NULL, NULL, NULL, &tv);
    }
}

/*========================== presentation (diff) ============================*/

static void colSeq(char kind, u32 c, std::string &out) {
    char b[32];
    int n = snprintf(b, sizeof b, "\x1b[%c8;2;%d;%d;%dm", kind, cr(c), cg(c), cb(c));
    out.append(b, (size_t)n);
}
static void curMove(std::string &out, int r, int c) {
    char b[24];
    int n = snprintf(b, sizeof b, "\x1b[%d;%dH", r + 1, c + 1);
    out.append(b, (size_t)n);
}
static void curRight(std::string &out, int n) {
    char b[16];
    int k = snprintf(b, sizeof b, "\x1b[%dC", n);
    out.append(b, (size_t)k);
}

// Writes only the cells that differ from prev (unless forceAll).
static void present(const std::vector<Cell> &fb, std::vector<Cell> &prev, int W, int H,
                    std::string &out, bool forceAll, bool track) {
    out.clear();
    int ccRow = -1, ccCol = -1;
    u32 lastFg = 0xFFFFFFFFu, lastBg = 0xFFFFFFFFu;
    for (int r = 0; r < H; ++r) {
        int c = 0;
        while (c < W) {
            int idx = r * W + c;
            if (!forceAll && fb[idx] == prev[idx]) { ++c; continue; }
            if (ccRow == r && ccCol >= 0 && ccCol <= c) {
                if (c > ccCol) curRight(out, c - ccCol);
            } else {
                curMove(out, r, c);
            }
            const Cell &cell = fb[idx];
            if (cell.ch == 0 && g_utf8) {
                if (cell.fg != lastFg) { colSeq('3', cell.fg, out); lastFg = cell.fg; }
                if (cell.bg != lastBg) { colSeq('4', cell.bg, out); lastBg = cell.bg; }
                out += HALFSTR;
            } else if (cell.ch == 0) {
                // no-unicode fallback: brightness ramp, one char per cell
                u32 base = mixC(cell.fg, cell.bg, 0.5f);
                int v = (int)(lum(base) / 255.0f * 9.0f + 0.5f);
                v = clampi(v, 0, 9);
                u32 fg = scaleC(base, v == 0 ? 1.0f : 0.45f + 0.075f * (float)v);
                if (fg != lastFg) { colSeq('3', fg, out); lastFg = fg; }
                if (lastBg != 0) { colSeq('4', 0, out); lastBg = 0; }
                out.push_back(RAMPSTR[v]);
            } else if (cell.ch == ' ') {
                if (cell.bg != lastBg) { colSeq('4', cell.bg, out); lastBg = cell.bg; }
                out.push_back(' ');
            } else {
                if (cell.fg != lastFg) { colSeq('3', cell.fg, out); lastFg = cell.fg; }
                if (cell.bg != lastBg) { colSeq('4', cell.bg, out); lastBg = cell.bg; }
                utf8Enc(cell.ch, out);
            }
            if (track) prev[idx] = fb[idx];
            ccRow = r;
            ccCol = c + 1;
            ++c;
        }
    }
    out += "\x1b[0m";
}

/*================================ modes ====================================*/

struct App;

struct Mode {
    virtual ~Mode() {}
    virtual const char *name() const = 0;
    virtual const char *hint() const = 0;
    virtual void resize(int, int) {}
    virtual void clear() {}
    virtual void update(float, App &) {}
    virtual void render(App &, std::vector<Cell> &) = 0;
    virtual bool key(int, App &) { return false; }
    virtual void opt(int, App &) {}
    virtual const char *optName() const { return ""; }
    virtual std::string optValue(const App &) const { return ""; }
};

struct App {
    int W = 80, H = 24, ph = 22;
    int themeIdx = 0;
    int brush = 3;
    bool paused = false;
    bool autoMode = false;
    bool help = false;
    bool wantShot = false;
    float fps = 60.0f;
    float time = 0.0f;
    std::string toast;
    float toastT = 0.0f;
    std::vector<Mode *> modes;
    int modeIdx = 0;

    // mouse in play-area coordinates
    int mx = -1, my = -1, pxm = -1, pym = -1;
    bool inPlay = false, lmb = false, rmb = false, lpres = false, rpres = false;
    int wheel = 0;

    struct Conf { float x, y, vx, vy, life, max; u32 col; };
    std::vector<Conf> conf;

    Mode *mode() { return modes[(size_t)clampi(modeIdx, 0, (int)modes.size() - 1)]; }
    const char *themeName() const { return THEMES[clampi(themeIdx, 0, NTHEMES - 1)].name; }
    u32 tc(float t) const {
        const Theme &th = THEMES[clampi(themeIdx, 0, NTHEMES - 1)];
        return hsv(th.h0 + (th.h1 - th.h0) * clampf(t, 0.0f, 1.0f), th.sat, 1.0f);
    }
    u32 tcd(float t, float v) const {
        const Theme &th = THEMES[clampi(themeIdx, 0, NTHEMES - 1)];
        return hsv(th.h0 + (th.h1 - th.h0) * clampf(t, 0.0f, 1.0f), th.sat, v);
    }
    void say(const std::string &s) { toast = s; toastT = 2.6f; }

    void burst(float x, float y, int n) {
        for (int i = 0; i < n; ++i) {
            float a = frnd(0.0f, 6.2831853f);
            float sp = frnd(15.0f, 130.0f);
            Conf c;
            c.x = x;
            c.y = y;
            c.vx = std::cos(a) * sp;
            c.vy = std::sin(a) * sp - 25.0f;
            c.life = 0.0f;
            c.max = frnd(0.6f, 1.6f);
            c.col = tc(rndf());
            conf.push_back(c);
        }
        if (conf.size() > 6000) conf.erase(conf.begin(), conf.begin() + (conf.size() - 6000));
    }

    void updateConf(float dt) {
        float top = (float)(2 * ph);
        for (size_t i = 0; i < conf.size();) {
            Conf &c = conf[i];
            c.life += dt;
            c.vy += 260.0f * dt;
            c.vx *= (1.0f - 1.2f * dt);
            c.x += c.vx * dt;
            c.y += c.vy * dt;
            if (c.y > top) c.vy *= -0.35f, c.y = top - 1.0f;
            if (c.x < 0) { c.x = 0; c.vx = -c.vx; }
            if (c.x >= W - 1) { c.x = (float)W - 1; c.vx = -c.vx; }
            if (c.life >= c.max) {
                conf[i] = conf.back();
                conf.pop_back();
                continue;
            }
            ++i;
        }
    }

    void renderConf(std::vector<Cell> &fb) const {
        for (size_t i = 0; i < conf.size(); ++i) {
            const Conf &c = conf[i];
            float a = 1.0f - c.life / c.max;
            pxAdd(fb, W, ph, (int)c.x, (int)c.y, scaleC(c.col, clampf(a, 0.0f, 1.0f) * 0.8f));
        }
    }

    void setMouse(const Input &in) {
        mx = in.mx;
        my = in.my - 1;                 // play-field row
        inPlay = in.mx >= 0 && in.my >= 1 && in.my < H - 1 && in.mx < W;
        if (inPlay) { pxm = mx; pym = (my) * 2; }
        else { pxm = -1; pym = -1; }
        lmb = in.lmb;
        rmb = in.rmb;
        lpres = in.lpress;
        rpres = in.rpress;
        wheel = in.wheel;
    }
};

/*======================== 1. FALLING SAND ==================================*/

enum Mat : uint8_t {
    M_EMPTY = 0, M_WALL, M_STONE, M_SAND, M_WATER, M_OIL, M_LAVA, M_FIRE,
    M_SMOKE, M_STEAM, M_PLANT, M_ACID, M_ICE, M_WOOD, M_GUN, M_SPARK, M_COUNT
};
static const char *const MAT_NAMES[M_COUNT] = {
    "EMPTY", "WALL", "STONE", "SAND", "WATER", "OIL", "LAVA", "FIRE",
    "SMOKE", "STEAM", "PLANT", "ACID", "ICE", "WOOD", "GUNPOWDER", "SPARK"
};
static const uint8_t MAT_OK[M_COUNT] = {
    0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1
};
static inline bool isLiquid(uint8_t t) {
    return t == M_WATER || t == M_OIL || t == M_LAVA || t == M_ACID;
}
static inline bool isPowder(uint8_t t) { return t == M_SAND || t == M_GUN; }
static inline bool isGas(uint8_t t) {
    return t == M_FIRE || t == M_SMOKE || t == M_STEAM || t == M_SPARK;
}
static inline bool isFlammable(uint8_t t) {
    return t == M_OIL || t == M_WOOD || t == M_PLANT || t == M_GUN;
}
static inline float dens(uint8_t t) {
    switch (t) {
        case M_EMPTY: return 0.0f;
        case M_FIRE: return 1.0f;
        case M_SMOKE: return 2.0f;
        case M_STEAM: return 3.0f;
        case M_SPARK: return 1.2f;
        case M_OIL: return 8.0f;
        case M_WATER: return 10.0f;
        case M_ACID: return 10.5f;
        case M_LAVA: return 12.0f;
        case M_GUN: return 18.0f;
        case M_SAND: return 20.0f;
        default: return 99.0f;
    }
}
static const int NBX[4] = { 1, -1, 0, 0 };
static const int NBY[4] = { 0, 0, 1, -1 };

struct SandMode : Mode {
    int w = 0, h = 0;
    std::vector<uint8_t> g, ga, gs, moved;
    uint8_t tick = 1;
    int mat = M_SAND;
    float anim = 0.0f;

    void resize(int W, int Ph) override {
        w = W;
        h = Ph;
        g.assign((size_t)w * (size_t)h, M_EMPTY);
        ga.assign((size_t)w * (size_t)h, 0);
        gs.assign((size_t)w * (size_t)h, (uint8_t)rnd(256));
        moved.assign((size_t)w * (size_t)h, 0);
    }
    void clear() override { std::fill(g.begin(), g.end(), (uint8_t)M_EMPTY); }

    const char *name() const override { return "FALLING SAND"; }
    const char *hint() const override {
        return "drag: paint  right: erase  wheel: brush  , .: material  n: random  a: auto";
    }
    const char *optName() const override { return "MATERIAL"; }
    std::string optValue(const App &) const override { return MAT_NAMES[mat]; }
    void opt(int d, App &app) override {
        int m = mat;
        do { m = (m + d + M_COUNT) % M_COUNT; } while (!MAT_OK[m]);
        mat = m;
        app.say(fstr("Material: %s", MAT_NAMES[mat]));
    }
    bool key(int k, App &app) override {
        if (k == 'n' || k == 'N') {
            int m = mat;
            while (m == mat || !MAT_OK[m]) m = rnd(M_COUNT);
            mat = m;
            app.say(fstr("Material: %s", MAT_NAMES[mat]));
            return true;
        }
        return false;
    }

    size_t idx(int x, int y) const { return (size_t)y * (size_t)w + (size_t)x; }
    bool inb(int x, int y) const { return x >= 0 && y >= 0 && x < w && y < h; }
    uint8_t T(int x, int y) const { return g[idx(x, y)]; }
    void setCell(int x, int y, uint8_t nt, uint8_t life = 0) {
        size_t i = idx(x, y);
        g[i] = nt;
        ga[i] = life;
        if (nt != M_EMPTY) gs[i] = (uint8_t)rnd(256);
    }
    void moveSwap(int x, int y, int nx, int ny) {
        size_t a = idx(x, y), b = idx(nx, ny);
        std::swap(g[a], g[b]);
        std::swap(ga[a], ga[b]);
        std::swap(gs[a], gs[b]);
        moved[b] = tick;
    }

    bool powderCan(uint8_t /*self*/, uint8_t tg) const {
        if (tg == M_EMPTY || isGas(tg)) return true;
        if (isLiquid(tg)) return rnd(3) != 0;
        return false;
    }
    bool liquidCan(uint8_t self, uint8_t tg) const {
        if (tg == M_EMPTY || isGas(tg)) return true;
        if (isLiquid(tg) && dens(self) > dens(tg)) return true;
        return false;
    }

    void powderMove(int x, int y, uint8_t self) {
        if (y + 1 >= h) return;
        if (powderCan(self, T(x, y + 1))) { moveSwap(x, y, x, y + 1); return; }
        int d = rnd(2) ? 1 : -1;
        for (int k = 0; k < 2; ++k) {
            int nx = x + d;
            if (nx >= 0 && nx < w && powderCan(self, T(nx, y + 1))) { moveSwap(x, y, nx, y + 1); return; }
            d = -d;
        }
    }

    void liquidMove(int x, int y, uint8_t self) {
        if (y > 0) {                       // buoyancy: lighter liquid rises
            uint8_t up = T(x, y - 1);
            if (isLiquid(up) && dens(self) < dens(up)) { moveSwap(x, y, x, y - 1); return; }
        }
        if (y + 1 < h) {
            if (liquidCan(self, T(x, y + 1))) { moveSwap(x, y, x, y + 1); return; }
            int d = rnd(2) ? 1 : -1;
            for (int k = 0; k < 2; ++k) {
                int nx = x + d;
                if (nx >= 0 && nx < w && liquidCan(self, T(nx, y + 1))) { moveSwap(x, y, nx, y + 1); return; }
                d = -d;
            }
        }
        int d = rnd(2) ? 1 : -1;           // spread sideways
        for (int k = 0; k < 2; ++k) {
            int nx = x + d;
            if (nx >= 0 && nx < w) {
                uint8_t tg = T(nx, y);
                if (tg == M_EMPTY) {
                    int far = nx;
                    for (int s = 1; s < 4; ++s) {
                        int fx = x + d * (s + 1);
                        if (fx < 0 || fx >= w) break;
                        if (T(fx, y) != M_EMPTY) break;
                        far = fx;
                    }
                    moveSwap(x, y, rnd(3) == 0 ? far : nx, y);
                    return;
                }
            }
            d = -d;
        }
    }

    void gasMove(int x, int y, uint8_t /*self*/) {
        if (y <= 0) return;
        int d = rnd(3) - 1;                // -1,0,1
        if (d != 0) {
            int nx = x + d;
            if (nx >= 0 && nx < w && isGas(T(nx, y - 1)) && rnd(2) == 0) { moveSwap(x, y, nx, y - 1); return; }
        }
        if (isGas(T(x, y - 1)) || T(x, y - 1) == M_EMPTY) { moveSwap(x, y, x, y - 1); return; }
        if (d != 0) {
            int nx = x + d;
            if (nx >= 0 && nx < w && T(nx, y - 1) == M_EMPTY) moveSwap(x, y, nx, y - 1);
        }
    }

    void igniteAt(int x, int y) {
        if (!inb(x, y)) return;
        uint8_t t = T(x, y);
        if (t == M_GUN) { explode(x, y); return; }
        if (t == M_ICE) { setCell(x, y, M_WATER); return; }
        if (t == M_OIL && rnd(100) < 45) setCell(x, y, M_FIRE, (uint8_t)rndi(20, 45));
        else if (t == M_WOOD && rnd(100) < 7) setCell(x, y, M_FIRE, (uint8_t)rndi(15, 35));
        else if (t == M_PLANT && rnd(100) < 25) setCell(x, y, M_FIRE, (uint8_t)rndi(15, 40));
    }

    void explode(int sx, int sy) {
        if (!inb(sx, sy)) return;
        std::vector<int> qx, qy;
        std::vector<uint8_t> seen((size_t)w * (size_t)h, 0);
        qx.push_back(sx);
        qy.push_back(sy);
        seen[idx(sx, sy)] = 1;
        for (size_t head = 0; head < qx.size(); ++head) {
            int x = qx[head], y = qy[head];
            if (!inb(x, y) || T(x, y) != M_GUN) continue;
            for (int k = 0; k < 4; ++k) {   // find chain neighbours first
                int nx = x + NBX[k], ny = y + NBY[k];
                if (!inb(nx, ny)) continue;
                size_t ni = idx(nx, ny);
                if (g[ni] == M_GUN && !seen[ni]) {
                    seen[ni] = 1;
                    qx.push_back(nx);
                    qy.push_back(ny);
                }
            }
            for (int yy = y - 2; yy <= y + 2; ++yy) {   // then blow up
                for (int xx = x - 2; xx <= x + 2; ++xx) {
                    if (!inb(xx, yy)) continue;
                    int dx = xx - x, dy = yy - y;
                    if (dx * dx + dy * dy > 6) continue;
                    size_t i = idx(xx, yy);
                    uint8_t tt = g[i];
                    if (tt == M_GUN) {
                        if (!seen[i]) { seen[i] = 1; qx.push_back(xx); qy.push_back(yy); }
                        continue;
                    }
                    if (tt == M_WALL) continue;
                    if (isFlammable(tt) || tt == M_LAVA) {
                        g[i] = M_FIRE;
                        ga[i] = (uint8_t)rndi(18, 45);
                        gs[i] = (uint8_t)rnd(256);
                    } else if (tt == M_EMPTY || isGas(tt) || tt == M_STEAM) {
                        g[i] = (rnd(4) == 0) ? M_SMOKE : M_FIRE;
                        ga[i] = (uint8_t)rndi(25, 70);
                        gs[i] = (uint8_t)rnd(256);
                    }
                }
            }
        }
    }

    void fireUpdate(int x, int y) {
        uint8_t life = ga[idx(x, y)];
        if (life > 0) ga[idx(x, y)] = (uint8_t)(life - 1);
        bool quench = false;
        for (int k = 0; k < 4; ++k) {
            int nx = x + NBX[k], ny = y + NBY[k];
            if (!inb(nx, ny)) continue;
            uint8_t t = T(nx, ny);
            if (t == M_WATER) { quench = true; break; }
            igniteAt(nx, ny);
        }
        if (quench) { setCell(x, y, M_STEAM, (uint8_t)rndi(90, 180)); return; }
        if (life <= 1) {
            if (rnd(3) == 0) setCell(x, y, M_SMOKE, (uint8_t)rndi(60, 130));
            else setCell(x, y, M_EMPTY);
            return;
        }
        gasMove(x, y, M_FIRE);
    }

    void lavaUpdate(int x, int y) {
        for (int k = 0; k < 4; ++k) {
            int nx = x + NBX[k], ny = y + NBY[k];
            if (!inb(nx, ny)) continue;
            uint8_t t = T(nx, ny);
            if (t == M_WATER) {
                setCell(x, y, M_STONE);
                setCell(nx, ny, M_STEAM, (uint8_t)rndi(90, 180));
                return;
            }
            if (t == M_ICE) { setCell(nx, ny, M_WATER); continue; }
            igniteAt(nx, ny);
        }
        if (T(x, y) != M_LAVA) return;
        uint8_t c = ga[idx(x, y)];
        if (c > 0) { ga[idx(x, y)] = (uint8_t)(c - 1); return; }
        liquidMove(x, y, M_LAVA);
        ga[idx(x, y)] = (uint8_t)rndi(2, 7);
    }

    void liquidUpdate(int x, int y, uint8_t self) {
        for (int k = 0; k < 4; ++k) {
            int nx = x + NBX[k], ny = y + NBY[k];
            if (!inb(nx, ny)) continue;
            uint8_t t = T(nx, ny);
            if (self == M_WATER) {
                if (t == M_LAVA) {
                    setCell(nx, ny, M_STONE);
                    setCell(x, y, M_STEAM, (uint8_t)rndi(90, 180));
                    return;
                }
                if (t == M_FIRE && rnd(2) == 0) {
                    setCell(nx, ny, M_STEAM, (uint8_t)rndi(90, 180));
                    continue;
                }
            } else if (self == M_ACID) {
                if (t != M_EMPTY && t != M_ACID && t != M_WALL && rnd(100) < 9) {
                    uint8_t nt = (rnd(4) == 0) ? M_SMOKE : M_EMPTY;
                    setCell(nx, ny, nt, nt == M_SMOKE ? (uint8_t)rndi(40, 120) : (uint8_t)0);
                    if (rnd(100) < 25) { setCell(x, y, M_SMOKE, (uint8_t)rndi(40, 110)); return; }
                }
            } else if (self == M_OIL) {
                if (t == M_FIRE) { setCell(x, y, M_FIRE, (uint8_t)rndi(25, 55)); return; }
            }
        }
        liquidMove(x, y, self);
    }

    void steamUpdate(int x, int y) {
        size_t i = idx(x, y);
        if (ga[i] > 0) ga[i] = (uint8_t)(ga[i] - 1);
        else { setCell(x, y, M_EMPTY); return; }
        gasMove(x, y, M_STEAM);
        if (inb(x, y) && y < 4 && rnd(100) < 12 && T(x, y) == M_STEAM) setCell(x, y, M_WATER);
    }

    void smokeUpdate(int x, int y) {
        size_t i = idx(x, y);
        if (ga[i] > 0) ga[i] = (uint8_t)(ga[i] - 1);
        else { setCell(x, y, M_EMPTY); return; }
        gasMove(x, y, M_SMOKE);
    }

    void sparkUpdate(int x, int y) {
        size_t i = idx(x, y);
        if (ga[i] > 0) ga[i] = (uint8_t)(ga[i] - 1);
        else { setCell(x, y, M_EMPTY); return; }
        for (int k = 0; k < 4; ++k) {
            int nx = x + NBX[k], ny = y + NBY[k];
            if (!inb(nx, ny)) continue;
            uint8_t t = T(nx, ny);
            if (t == M_WATER) { setCell(x, y, M_EMPTY); return; }
            igniteAt(nx, ny);
        }
        if (T(x, y) != M_SPARK) return;
        int d = rnd(4);
        int nx = x + NBX[d], ny = y + NBY[d];
        if (inb(nx, ny) && (T(nx, ny) == M_EMPTY || isGas(T(nx, ny)))) moveSwap(x, y, nx, ny);
    }

    void plantUpdate(int x, int y) {
        bool wet = false;
        for (int k = 0; k < 4; ++k) {
            int nx = x + NBX[k], ny = y + NBY[k];
            if (!inb(nx, ny)) continue;
            if (T(nx, ny) == M_WATER) { wet = true; break; }
        }
        if (!wet) return;
        if (rnd(100) < 6) {                 // roots drink
            for (int k = 0; k < 4; ++k) {
                int nx = x + NBX[k], ny = y + NBY[k];
                if (inb(nx, ny) && T(nx, ny) == M_WATER && rnd(2) == 0) {
                    setCell(nx, ny, M_PLANT);
                    return;
                }
            }
        }
        if (rnd(100) < 14) {                // and grow
            int d = rnd(4);
            int nx = x + NBX[d], ny = y + NBY[d];
            if (inb(nx, ny) && T(nx, ny) == M_EMPTY) setCell(nx, ny, M_PLANT);
        }
    }

    void updateCell(int x, int y) {
        size_t i = idx(x, y);
        if (moved[i] == tick) return;
        uint8_t t = g[i];
        switch (t) {
            case M_EMPTY: case M_WALL: case M_STONE: case M_ICE: case M_WOOD:
                if (t == M_ICE) {
                    for (int k = 0; k < 4; ++k) {
                        int nx = x + NBX[k], ny = y + NBY[k];
                        if (!inb(nx, ny)) continue;
                        uint8_t nb = T(nx, ny);
                        if (nb == M_FIRE || nb == M_LAVA) { setCell(x, y, M_WATER); return; }
                        if (nb == M_WATER && rnd(1000) < 6) { setCell(nx, ny, M_ICE); return; }
                    }
                }
                return;
            case M_SAND: case M_GUN: powderMove(x, y, t); return;
            case M_WATER: case M_OIL: case M_ACID: liquidUpdate(x, y, t); return;
            case M_LAVA: lavaUpdate(x, y); return;
            case M_FIRE: fireUpdate(x, y); return;
            case M_SMOKE: smokeUpdate(x, y); return;
            case M_STEAM: steamUpdate(x, y); return;
            case M_SPARK: sparkUpdate(x, y); return;
            case M_PLANT: plantUpdate(x, y); return;
            default: return;
        }
    }

    void paint(int cx, int cy, int r, uint8_t nt) {
        int r2 = r * r;
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (x * x + y * y > r2) continue;
                int px = cx + x, py = cy + y;
                if (!inb(px, py)) continue;
                if (nt == M_EMPTY) { setCell(px, py, M_EMPTY); continue; }
                if (T(px, py) == M_WALL && nt != M_WALL) continue;
                if (rnd(100) < 78) setCell(px, py, nt, nt == M_FIRE ? (uint8_t)rndi(20, 45) : 0);
            }
    }

    void autoDrop(App &app) {
        if (!app.autoMode || w <= 0) return;
        if (mat == M_EMPTY) {
            int m = mat;
            while (m == mat || !MAT_OK[m]) m = rnd(M_COUNT);
            mat = m;
        }
        if (rnd(100) < 75) {                 // sweeping faucet pours the current material
            int cx = (int)((float)w * 0.5f + std::sin(app.time * 0.6f) * (float)w * 0.38f);
            paint(cx, rndi(0, 2), rndi(1, 3), (uint8_t)mat);
        }
        if (rnd(100) < 6) {                  // plus the occasional surprise
            static const uint8_t pool[] = { M_SAND, M_WATER, M_OIL, M_LAVA, M_FIRE, M_PLANT,
                                            M_ACID, M_ICE, M_GUN, M_SPARK, M_STEAM, M_STONE };
            paint(rnd(w), rndi(0, 3), rndi(2, 4), pool[rnd(12)]);
        }
    }

    void update(float dt, App &app) override {
        anim += dt;
        if (app.inPlay) {
            if (app.lmb && app.mx >= 0 && app.my >= 0) paint(app.mx, app.my, app.brush, (uint8_t)mat);
            else if (app.rmb && app.mx >= 0 && app.my >= 0) paint(app.mx, app.my, app.brush, M_EMPTY);
        }
        autoDrop(app);
        if (app.paused || w <= 0 || h <= 0) return;
        tick = (tick == 1) ? 2 : 1;
        std::fill(moved.begin(), moved.end(), (uint8_t)0);
        for (int y = 0; y < h; ++y)
            for (int k = 0; k < w; ++k) {
                int x = (y & 1) ? (w - 1 - k) : k;
                updateCell(x, y);
            }
    }

    u32 colorOf(int x, int y, uint8_t t, uint8_t sh, uint8_t ax) const {
        float r = (float)sh / 255.0f;
        switch (t) {
            case M_EMPTY: return 0;
            case M_WALL: return mixC(rgb(58, 62, 76), rgb(104, 110, 128),
                                     clampf(r * 0.5f + (((x + y) & 1) ? 0.35f : 0.0f), 0, 1));
            case M_STONE: return mixC(rgb(86, 86, 92), rgb(146, 146, 150), r);
            case M_SAND: return mixC(rgb(190, 156, 88), rgb(240, 214, 152), r);
            case M_WATER: {
                float w = 0.5f + 0.5f * std::sin(anim * 3.0f + x * 0.35f + r * 7.0f);
                return mixC(rgb(20, 74, 172), rgb(78, 150, 240), clampf(0.35f * w + 0.35f * r, 0, 1));
            }
            case M_OIL: return mixC(rgb(44, 32, 18), rgb(92, 70, 40), r);
            case M_LAVA: {
                float f = 0.5f + 0.5f * std::sin(anim * 5.0f + r * 24.0f + x * 0.7f);
                return hsv(12.0f + 26.0f * f, 0.95f, 0.55f + 0.45f * f);
            }
            case M_FIRE: {
                float l = clampf((float)ax / 45.0f, 0.0f, 1.0f);
                return hsv(lerpf(6.0f, 52.0f, l) + 6.0f * std::sin(anim * 20.0f + r * 15.0f),
                           1.0f, clampf(0.45f + 0.55f * l, 0, 1));
            }
            case M_SMOKE: return mixC(rgb(34, 34, 40), rgb(126, 126, 136), (float)ax / 255.0f);
            case M_STEAM: return mixC(rgb(64, 76, 92), rgb(196, 210, 228), (float)ax / 255.0f);
            case M_PLANT: return hsv(lerpf(92.0f, 145.0f, r), 0.72f, 0.34f + 0.46f * r);
            case M_ACID: {
                float b = 0.5f + 0.5f * std::sin(anim * 7.0f + r * 33.0f);
                return hsv(74.0f + 14.0f * b, 0.9f, 0.72f + 0.28f * b);
            }
            case M_ICE: return mixC(rgb(146, 200, 240), rgb(232, 248, 255), r);
            case M_WOOD: {
                u32 base = mixC(rgb(92, 60, 34), rgb(146, 100, 56), r);
                if ((x * 3 + y) % 7 == 0) base = scaleC(base, 0.7f);
                return base;
            }
            case M_GUN: {
                u32 base = mixC(rgb(44, 44, 52), rgb(86, 86, 96), r);
                if (sh % 11 == 0) base = mixC(base, rgb(190, 180, 140), 0.5f);
                return base;
            }
            case M_SPARK: return mixC(rgb(255, 246, 190), rgb(255, 255, 255), r);
            default: return rgb(255, 0, 255);
        }
    }

    void render(App &app, std::vector<Cell> &fb) override {
        u32 ring = rgb(255, 255, 255);
        int r2o = (app.brush - 1) * (app.brush - 1);
        int r2i = (app.brush + 1) * (app.brush + 1);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t i = idx(x, y);
                u32 c = colorOf(x, y, g[i], gs[i], ga[i]);
                if (app.inPlay && app.brush > 0) {
                    int dx = x - app.mx, dy = y - app.my;
                    int d2 = dx * dx + dy * dy;
                    if (d2 >= r2o && d2 <= r2i) c = mixC(c, ring, 0.55f);
                }
                putCell(fb, app.W, app.H, x, 1 + y, ' ', 0, c);
            }
    }
};

/*============================= 2. LIFE =====================================*/

struct LifeMode : Mode {
    int w = 0, h = 0;
    std::vector<uint8_t> alive, age;
    float acc = 0.0f;
    static const int NRATES = 5;
    int rateIdx = 2;
    static float rateOf(int i) {
        static const float r[NRATES] = { 0.35f, 0.16f, 0.08f, 0.04f, 0.015f };
        return r[clampi(i, 0, NRATES - 1)];
    }

    void resize(int W, int Ph) override {
        w = W;
        h = Ph;
        alive.assign((size_t)w * (size_t)h, 0);
        age.assign((size_t)w * (size_t)h, 0);
        randomise();
    }
    void clear() override {
        std::fill(alive.begin(), alive.end(), (uint8_t)0);
        std::fill(age.begin(), age.end(), (uint8_t)0);
    }
    void randomise() {
        for (size_t i = 0; i < alive.size(); ++i) {
            alive[i] = rnd(100) < 22 ? 1 : 0;
            age[i] = alive[i] ? (uint8_t)rndi(20, 80) : 0;
        }
    }

    const char *name() const override { return "GAME OF LIFE"; }
    const char *hint() const override {
        return "drag: paint   right: erase   x: step once   r: randomise   , .: speed";
    }
    const char *optName() const override { return "SPEED"; }
    std::string optValue(const App &) const override { return fstr("%.2fs/gen", rateOf(rateIdx)); }
    void opt(int d, App &app) override {
        rateIdx = clampi(rateIdx + d, 0, NRATES - 1);
        app.say(fstr("Speed: %.2f s / generation", rateOf(rateIdx)));
    }
    bool key(int k, App &app) override {
        if (k == 'r' || k == 'R') { randomise(); app.say("Randomised"); return true; }
        if (k == 'x') { step(); app.say("Stepped one generation"); return true; }
        return false;
    }

    void paint(int cx, int cy, int r, bool on) {
        int r2 = r * r;
        for (int y = -r; y <= r; ++y)
            for (int x = -r; x <= r; ++x) {
                if (x * x + y * y > r2) continue;
                int px = cx + x, py = cy + y;
                if (px < 0 || py < 0 || px >= w || py >= h) continue;
                size_t i = (size_t)py * (size_t)w + (size_t)px;
                alive[i] = on ? 1 : 0;
                age[i] = on ? (uint8_t)rndi(10, 50) : 0;
            }
    }

    void step() {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                int n = 0;
                for (int dy = -1; dy <= 1; ++dy)
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (!dx && !dy) continue;
                        int nx = (x + dx + w) % w;
                        int ny = (y + dy + h) % h;
                        n += alive[(size_t)ny * (size_t)w + (size_t)nx];
                    }
                size_t i = (size_t)y * (size_t)w + (size_t)x;
                bool born = !alive[i] && n == 3;
                bool lives = alive[i] && (n == 2 || n == 3);
                if (born || lives) {
                    alive[i] = 1;
                    age[i] = (uint8_t)std::min(255, (int)age[i] + (born ? 40 : 2));
                } else if (alive[i]) {
                    alive[i] = 0;
                    age[i] = 190;                      // ghost trail
                } else if (age[i] > 0) {
                    age[i] = (uint8_t)(age[i] > 7 ? age[i] - 7 : 0);
                }
            }
    }

    void update(float dt, App &app) override {
        if (app.inPlay && (app.lmb || app.rmb) && app.mx >= 0 && app.my >= 0)
            paint(app.mx, app.my, app.brush, app.lmb);
        if (app.paused) return;
        acc += dt;
        float rate = rateOf(rateIdx);
        int guard = 0;
        while (acc >= rate && guard++ < 8) { step(); acc -= rate; }
        if (acc > 0.5f) acc = 0.0f;
    }

    void render(App &app, std::vector<Cell> &fb) override {
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t i = (size_t)y * (size_t)w + (size_t)x;
                u32 c;
                if (alive[i]) {
                    float t = std::fmod((float)age[i] * 0.02f + (float)x * 0.008f, 1.0f);
                    c = app.tcd(t, clampf(0.55f + (float)age[i] / 200.0f, 0.0f, 1.0f));
                } else if (age[i] > 0) {
                    float t = std::fmod((float)x * 0.008f, 1.0f);
                    c = scaleC(app.tcd(t, 0.5f), (float)age[i] / 420.0f);
                } else {
                    c = rgb(6, 7, 11);
                }
                putCell(fb, app.W, app.H, x, 1 + y, ' ', 0, c);
            }
    }
};

/*============================ 3. RIPPLE ====================================*/

struct RippleMode : Mode {
    int w = 0, h = 0;
    std::vector<float> cur, prv, nxt;
    float damp = 0.994f;
    int strength = 70;

    void resize(int W, int Ph) override {
        w = W;
        h = Ph;
        cur.assign((size_t)w * (size_t)h, 0.0f);
        prv.assign((size_t)w * (size_t)h, 0.0f);
        nxt.assign((size_t)w * (size_t)h, 0.0f);
    }
    void clear() override {
        std::fill(cur.begin(), cur.end(), 0.0f);
        std::fill(prv.begin(), prv.end(), 0.0f);
        std::fill(nxt.begin(), nxt.end(), 0.0f);
    }

    const char *name() const override { return "WATER RIPPLE"; }
    const char *hint() const override {
        return "click / drag: disturb the surface  a: rain  , .: strength  c: still water";
    }
    const char *optName() const override { return "STRENGTH"; }
    std::string optValue(const App &) const override { return fstr("%d", strength); }
    void opt(int d, App &app) override {
        strength = clampi(strength + d * 20, 20, 260);
        app.say(fstr("Splash strength: %d", strength));
    }

    void drop(int cx, int cy, int rad, float amt) {
        if (rad < 1) rad = 1;
        for (int y = cy - rad; y <= cy + rad; ++y)
            for (int x = cx - rad; x <= cx + rad; ++x) {
                if (x < 0 || y < 0 || x >= w || y >= h) continue;
                float dx = (float)(x - cx) / (float)rad;
                float dy = (float)(y - cy) / (float)rad;
                float d2 = dx * dx + dy * dy;
                if (d2 > 1.0f) continue;
                cur[(size_t)y * (size_t)w + (size_t)x] += amt * (1.0f - d2);
            }
    }

    void step() {
        for (int y = 1; y < h - 1; ++y) {
            size_t row = (size_t)y * (size_t)w;
            for (int x = 1; x < w - 1; ++x) {
                size_t i = row + (size_t)x;
                float s = (cur[i - 1] + cur[i + 1] + cur[i - (size_t)w] + cur[i + (size_t)w]) * 0.5f - prv[i];
                nxt[i] = s * damp;
            }
        }
        for (int x = 0; x < w; ++x) {
            nxt[(size_t)x] = 0.0f;
            nxt[(size_t)(h - 1) * (size_t)w + (size_t)x] = 0.0f;
        }
        for (int y = 0; y < h; ++y) {
            nxt[(size_t)y * (size_t)w] = 0.0f;
            nxt[(size_t)y * (size_t)w + (size_t)(w - 1)] = 0.0f;
        }
        prv.swap(cur);
        cur.swap(nxt);
    }

    void update(float, App &app) override {
        if (app.inPlay && app.mx >= 0 && app.my >= 0) {
            if (app.lpres) drop(app.mx, app.my, app.brush + 4, (float)strength);
            else if (app.lmb) drop(app.mx, app.my, app.brush, (float)strength * 0.28f);
        }
        if (app.autoMode && rnd(100) < 4) drop(rnd(w), rnd(h), rndi(2, 6), (float)strength);
        if (app.paused || w < 3 || h < 3) return;
        step();
    }

    void render(App &app, std::vector<Cell> &fb) override {
        u32 deep = mixC(rgb(4, 16, 46), app.tc(0.5f), 0.35f);
        u32 lite = mixC(rgb(96, 178, 250), app.tc(0.35f), 0.30f);
        u32 foam = rgb(232, 246, 255);
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                size_t i = (size_t)y * (size_t)w + (size_t)x;
                float v = clampf(cur[i] * 0.10f, -1.0f, 1.0f);
                float gx = v - (x > 0 ? clampf(cur[i - 1] * 0.10f, -1.0f, 1.0f) : v);
                float gy = v - (y > 0 ? clampf(cur[i - (size_t)w] * 0.10f, -1.0f, 1.0f) : v);
                float light = clampf((gx + gy) * 2.5f, -1.0f, 1.0f);
                u32 c = mixC(deep, lite, clampf(0.5f + 0.5f * v, 0.0f, 1.0f));
                if (light > 0) c = mixC(c, foam, light * 0.85f);
                else c = scaleC(c, 1.0f + light * 0.45f);
                putCell(fb, app.W, app.H, x, 1 + y, ' ', 0, c);
            }
    }
};

/*========================== 4. PARTICLES ===================================*/

struct ParticlesMode : Mode {
    struct P { float x, y, vx, vy; u32 col; };
    std::vector<P> ps;
    std::vector<u32> buf;
    int bw = 0, bh = 0;
    int target = 700;
    bool gravity = true;
    bool trails = true;
    float hueT = 0.0f;

    void resize(int W, int Ph) override {
        bw = W;
        bh = Ph * 2;
        buf.assign((size_t)bw * (size_t)bh, 0);
    }
    void clear() override { ps.clear(); }

    const char *name() const override { return "PARTICLES"; }
    const char *hint() const override {
        return "left: attract  right: repel  click: burst  g: gravity  b: trails  , .: count";
    }
    const char *optName() const override { return "COUNT"; }
    std::string optValue(const App &) const override { return fstr("%d", target); }
    void opt(int d, App &app) override {
        target = clampi(target + d * 150, 0, 4000);
        app.say(fstr("Particle target: %d", target));
    }
    bool key(int k, App &app) override {
        if (k == 'g') { gravity = !gravity; app.say(std::string("Gravity: ") + (gravity ? "on" : "off")); return true; }
        if (k == 'b') { trails = !trails; app.say(std::string("Trails: ") + (trails ? "on" : "off")); return true; }
        return false;
    }

    void spawn(App &app, float x, float y, float vx, float vy) {
        if ((int)ps.size() >= 4000) return;
        P p;
        p.x = x;
        p.y = y;
        p.vx = vx;
        p.vy = vy;
        p.col = app.tc(std::fmod(hueT + (float)ps.size() * 0.0007f, 1.0f));
        ps.push_back(p);
    }

    void burst(App &app, float x, float y, int n, float power) {
        for (int i = 0; i < n; ++i) {
            float a = frnd(0.0f, 6.2831853f);
            float s = frnd(0.0f, power);
            spawn(app, x, y, std::cos(a) * s, std::sin(a) * s);
        }
    }

    void update(float dt, App &app) override {
        hueT += dt * 0.05f;
        if (dt > 0.05f) dt = 0.05f;

        // trail layer
        float f = trails ? std::pow(0.12f, dt) : 0.0f;
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = f > 0.001f ? scaleC(buf[i], f) : 0;

        // keep the population alive
        int want = app.autoMode ? std::max(target, 1200) : target;
        if ((int)ps.size() < want) {
            int add = std::min(want - (int)ps.size(), 40);
            for (int i = 0; i < add; ++i) {
                float cx = (float)bw * 0.5f, cy = (float)bh * 0.5f;
                float a = frnd(0.0f, 6.2831853f);
                float r = frnd(0.0f, (float)std::min(bw, bh) * 0.35f);
                spawn(app, cx + std::cos(a) * r, cy + std::sin(a) * r,
                      std::cos(a + 1.57f) * frnd(5, 40), std::sin(a + 1.57f) * frnd(5, 40));
            }
        }

        if (app.inPlay && app.lpres && app.pxm >= 0) burst(app, (float)app.pxm, (float)app.pym, 90, 150.0f);

        bool attract = app.inPlay && app.lmb && !app.lpres;
        bool repel = app.inPlay && app.rmb;
        float mxf = (float)app.pxm, myf = (float)app.pym;
        float g = gravity ? 55.0f : 0.0f;
        float drag = 1.0f - 0.55f * dt;
        float cx = (float)bw * 0.5f, cy = (float)bh * 0.5f;

        for (size_t i = 0; i < ps.size(); ++i) {
            P &p = ps[i];
            if (app.paused) continue;
            if (attract || repel) {
                float dx = mxf - p.x, dy = myf - p.y;
                float d2 = dx * dx + dy * dy + 36.0f;
                float inv = 1.0f / std::sqrt(d2);
                float mag = clampf(9000.0f / d2, 0.0f, 1.2f);
                mag *= clampf((d2 - 260.0f) / 260.0f, 0.0f, 1.0f);   // keep a little orbit room
                float sgn = attract ? 1.0f : -1.0f;
                p.vx += dx * inv * mag * 780.0f * dt * sgn;
                p.vy += dy * inv * mag * 780.0f * dt * sgn;
            } else if (app.autoMode) {
                float dx = cx - p.x, dy = cy - p.y;
                float d2 = dx * dx + dy * dy + 400.0f;
                p.vx += (-dy / d2) * 60000.0f * dt;
                p.vy += (dx / d2) * 60000.0f * dt;
            }
            p.vy += g * dt;
            p.vx *= drag;
            p.vy *= drag;
            p.x += p.vx * dt;
            p.y += p.vy * dt;
            if (p.x < 0) { p.x = 0; p.vx = -p.vx * 0.7f; }
            else if (p.x >= bw - 1) { p.x = (float)bw - 1.0f; p.vx = -p.vx * 0.7f; }
            if (p.y < 0) { p.y = 0; p.vy = -p.vy * 0.7f; }
            else if (p.y >= bh - 1) { p.y = (float)bh - 1.0f; p.vy = -p.vy * 0.7f; }

            // burn the trail
            size_t bi = (size_t)(int)p.y * (size_t)bw + (size_t)(int)p.x;
            if (bi < buf.size()) buf[bi] = addC(buf[bi], scaleC(p.col, 0.55f));
        }

        int over = (int)ps.size() - target;
        if (over > 0 && !app.autoMode) ps.erase(ps.begin(), ps.begin() + over);
    }

    void render(App &, std::vector<Cell> &fb) override {
        for (int y = 0; y < bh; ++y) {
            size_t row = (size_t)y * (size_t)bw;
            for (int x = 0; x < bw; ++x) {
                u32 c = buf[row + (size_t)x];
                if (c) pxSet(fb, bw, bh / 2, x, y, c);
            }
        }
        for (size_t i = 0; i < ps.size(); ++i) {
            const P &p = ps[i];
            int x = (int)p.x, y = (int)p.y;
            u32 c = p.col;
            pxAdd(fb, bw, bh / 2, x, y, c);
            pxAdd(fb, bw, bh / 2, x - 1, y, scaleC(c, 0.32f));
            pxAdd(fb, bw, bh / 2, x + 1, y, scaleC(c, 0.32f));
            pxAdd(fb, bw, bh / 2, x, y - 1, scaleC(c, 0.32f));
            pxAdd(fb, bw, bh / 2, x, y + 1, scaleC(c, 0.32f));
        }
    }
};

/*========================== 5. FIREWORKS ===================================*/

struct FireworksMode : Mode {
    struct R { float x, y, vx, vy, ty; u32 col; int style; };
    struct S { float x, y, vx, vy, life, max; u32 col; int style; float ph; };
    std::vector<R> rk;
    std::vector<S> sp;
    std::vector<u32> buf;
    int bw = 0, bh = 0;
    float timer = 0.4f;
    int density = 2;

    void resize(int W, int Ph) override {
        bw = W;
        bh = Ph * 2;
        buf.assign((size_t)bw * (size_t)bh, 0);
    }
    void clear() override { rk.clear(); sp.clear(); }

    const char *name() const override { return "FIREWORKS"; }
    const char *hint() const override {
        return "click: launch   a: auto   , .: frequency   c: clear the sky";
    }
    const char *optName() const override { return "FREQUENCY"; }
    std::string optValue(const App &) const override { return fstr("%d", density); }
    void opt(int d, App &app) override {
        density = clampi(density + d, 1, 6);
        app.say(fstr("Launch frequency: %d", density));
    }

    void launch(App &app, float tx, float ty, int style) {
        if ((int)rk.size() > 24) return;
        R r;
        r.x = clampf(tx, 2.0f, (float)bw - 3.0f);
        r.y = (float)bh - 1.0f;
        r.vx = frnd(-14.0f, 14.0f);
        r.vy = -std::sqrt(2.0f * 105.0f * clampf(r.y - ty, 40.0f, (float)bh)) * frnd(0.86f, 1.0f);
        r.ty = ty;
        r.col = app.tc(rndf());
        r.style = style;
        rk.push_back(r);
    }

    void explode(App &app, const R &r) {
        u32 base = r.col;
        int style = r.style;
        if (style == 2) base = hsv(42.0f, 0.75f, 1.0f);            // willow = gold
        int n = style == 2 ? 90 : (style == 1 ? 130 : 150);
        float power = style == 2 ? frnd(26, 40) : frnd(45, 78);
        for (int i = 0; i < n; ++i) {
            S s;
            s.x = r.x;
            s.y = r.y;
            float a, sp2;
            if (style == 1) {                                       // ring
                a = (float)i / (float)n * 6.2831853f;
                sp2 = power * frnd(0.92f, 1.08f);
            } else {
                a = frnd(0.0f, 6.2831853f);
                sp2 = power * std::sqrt(rndf());
            }
            s.vx = std::cos(a) * sp2;
            s.vy = std::sin(a) * sp2;
            s.max = style == 2 ? frnd(1.6f, 2.6f) : frnd(0.7f, 1.5f);
            s.life = 0.0f;
            s.col = rnd(7) == 0 ? mixC(base, rgb(255, 255, 255), 0.75f)
                                : mixC(base, app.tc(rndf()), 0.45f);
            s.style = style;
            s.ph = frnd(0.0f, 6.28f);
            sp.push_back(s);
        }
        S core;
        core.x = r.x;
        core.y = r.y;
        core.vx = core.vy = 0;
        core.life = 0;
        core.max = 0.14f;
        core.col = mixC(base, rgb(255, 255, 255), 0.65f);
        core.style = 9;
        core.ph = 0;
        sp.push_back(core);
        if (sp.size() > 9000) sp.erase(sp.begin(), sp.begin() + (sp.size() - 9000));
    }

    void update(float dt, App &app) override {
        if (dt > 0.05f) dt = 0.05f;
        float f = std::pow(0.10f, dt);
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = scaleC(buf[i], f);

        if (app.inPlay && app.lpres && app.pxm >= 0)
            launch(app, (float)app.pxm, (float)app.pym, rnd(4));
        if (!app.paused) {
            timer -= dt * (float)density;
            if (timer <= 0.0f) {
                timer = frnd(0.55f, 1.6f);
                if (app.autoMode || rnd(3) == 0)
                    launch(app, frnd(4.0f, (float)bw - 5.0f), frnd((float)bh * 0.15f, (float)bh * 0.75f),
                           rnd(4));
            }
        }
        if (app.paused) return;

        for (size_t i = 0; i < rk.size();) {
            R &r = rk[i];
            r.vy += 62.0f * dt;
            r.x += r.vx * dt;
            r.y += r.vy * dt;
            size_t bi = (size_t)(int)r.y * (size_t)bw + (size_t)(int)r.x;
            if (bi < buf.size()) buf[bi] = addC(buf[bi], scaleC(r.col, 0.75f));
            if (r.vy >= 0.0f || r.y <= r.ty) {
                explode(app, r);
                rk[i] = rk.back();
                rk.pop_back();
                continue;
            }
            ++i;
        }
        for (size_t i = 0; i < sp.size();) {
            S &s = sp[i];
            s.life += dt;
            if (s.life >= s.max) {
                s = sp.back();
                sp.pop_back();
                continue;
            }
            if (s.style == 9) { ++i; continue; }
            s.vy += (s.style == 2 ? 34.0f : 60.0f) * dt;
            float drag = 1.0f - (s.style == 2 ? 1.4f : 0.7f) * dt;
            s.vx *= drag;
            s.vy *= drag;
            s.x += s.vx * dt;
            s.y += s.vy * dt;
            if (s.x >= 0 && s.x < bw && s.y >= 0 && s.y < bh) {
                size_t bi = (size_t)(int)s.y * (size_t)bw + (size_t)(int)s.x;
                float tw = 0.5f + 0.5f * std::sin(s.ph + s.life * 26.0f);
                buf[bi] = addC(buf[bi], scaleC(s.col, 0.45f * tw));
            }
            ++i;
        }
    }

    void render(App &, std::vector<Cell> &fb) override {
        for (int y = 0; y < bh; ++y) {
            size_t row = (size_t)y * (size_t)bw;
            for (int x = 0; x < bw; ++x) {
                u32 c = buf[row + (size_t)x];
                if (c) pxSet(fb, bw, bh / 2, x, y, c);
            }
        }
        for (size_t i = 0; i < sp.size(); ++i) {
            const S &s = sp[i];
            float k = 1.0f - s.life / s.max;
            if (s.style == 9) {
                int rad = 1 + (int)(4.0f * (s.life / s.max));
                pxDisc(fb, bw, bh / 2, (int)s.x, (int)s.y, rad, scaleC(s.col, 0.7f * k), 0.9f);
                continue;
            }
            float tw = 0.6f + 0.4f * std::sin(s.ph + s.life * 26.0f);
            u32 c = scaleC(s.col, clampf(k * tw, 0.0f, 1.0f));
            int x = (int)s.x, y = (int)s.y;
            pxAdd(fb, bw, bh / 2, x, y, c);
            pxAdd(fb, bw, bh / 2, x - 1, y, scaleC(c, 0.3f));
            pxAdd(fb, bw, bh / 2, x + 1, y, scaleC(c, 0.3f));
        }
        for (size_t i = 0; i < rk.size(); ++i) {
            const R &r = rk[i];
            int x = (int)r.x, y = (int)r.y;
            pxAdd(fb, bw, bh / 2, x, y, rgb(255, 250, 220));
            pxAdd(fb, bw, bh / 2, x, y + 1, scaleC(r.col, 0.7f));
            pxAdd(fb, bw, bh / 2, x, y + 2, scaleC(r.col, 0.3f));
        }
    }
};

/*============================ 6. BOIDS =====================================*/

struct BoidsMode : Mode {
    struct B { float x, y, vx, vy; };
    std::vector<B> bs;
    std::vector<u32> buf;
    int bw = 0, bh = 0;
    int target = 260;
    float hueT = 0.0f;

    void resize(int W, int Ph) override {
        bw = W;
        bh = Ph * 2;
        buf.assign((size_t)bw * (size_t)bh, 0);
    }
    void clear() override { bs.clear(); }

    const char *name() const override { return "BOIDS"; }
    const char *hint() const override {
        return "left: attract the flock   right: scatter   , .: flock size   c: empty sky";
    }
    const char *optName() const override { return "FLOCK"; }
    std::string optValue(const App &) const override { return fstr("%d", target); }
    void opt(int d, App &app) override {
        target = clampi(target + d * 60, 0, 900);
        app.say(fstr("Flock size: %d", target));
    }

    void spawn(App &app) {
        if (bw <= 0 || bh <= 0) return;
        B b;
        b.x = frnd(0.0f, (float)bw);
        b.y = frnd(0.0f, (float)bh);
        float a = frnd(0.0f, 6.2831853f);
        b.vx = std::cos(a) * 35.0f;
        b.vy = std::sin(a) * 35.0f;
        bs.push_back(b);
        (void)app;
    }

    void update(float dt, App &app) override {
        hueT += dt * 0.04f;
        if (dt > 0.05f) dt = 0.05f;
        float f = std::pow(0.22f, dt);
        for (size_t i = 0; i < buf.size(); ++i) buf[i] = scaleC(buf[i], f);

        int want = app.autoMode ? std::max(target, 320) : target;
        int guard = 0;
        while ((int)bs.size() < want && guard++ < 60) spawn(app);
        int over = (int)bs.size() - want;
        if (over > 0) bs.erase(bs.begin(), bs.begin() + over);
        if (bs.empty()) return;

        const float sepR = 7.0f, aliR = 17.0f, cohR = 23.0f;
        const float sepR2 = sepR * sepR, aliR2 = aliR * aliR, cohR2 = cohR * cohR;
        bool attract = app.inPlay && app.lmb;
        bool repel = app.inPlay && app.rmb;
        float mxf = (float)app.pxm, myf = (float)app.pym;
        float cx = (float)bw * 0.5f, cy = (float)bh * 0.5f;

        std::vector<float> axx(bs.size()), ayy(bs.size());
        for (size_t i = 0; i < bs.size(); ++i) {
            float sx = 0, sy = 0, alx = 0, aly = 0, cox = 0, coy = 0;
            int na = 0, nc = 0;
            for (size_t j = 0; j < bs.size(); ++j) {
                if (i == j) continue;
                float dx = bs[j].x - bs[i].x, dy = bs[j].y - bs[i].y;
                float d2 = dx * dx + dy * dy;
                if (d2 < sepR2 && d2 > 0.001f) { sx -= dx / d2; sy -= dy / d2; }
                if (d2 < aliR2) { alx += bs[j].vx; aly += bs[j].vy; ++na; }
                if (d2 < cohR2) { cox += bs[j].x; coy += bs[j].y; ++nc; }
            }
            float fx = sx * 6.0f, fy = sy * 6.0f;
            if (na > 0) { fx += (alx / (float)na - bs[i].vx) * 1.6f; fy += (aly / (float)na - bs[i].vy) * 1.6f; }
            if (nc > 0) {
                fx += (cox / (float)nc - bs[i].x) * 1.1f;
                fy += (coy / (float)nc - bs[i].y) * 1.1f;
            }
            if (attract || repel) {
                float dx = mxf - bs[i].x, dy = myf - bs[i].y;
                float d2 = dx * dx + dy * dy + 64.0f;
                float mag = clampf(14000.0f / d2, 0.0f, 2.0f) * (attract ? 1.0f : -1.0f);
                fx += dx * mag;
                fy += dy * mag;
            } else if (app.autoMode) {
                fx += (cx - bs[i].x) * 0.03f;
                fy += (cy - bs[i].y) * 0.03f;
            }
            axx[i] = fx;
            ayy[i] = fy;
        }

        if (app.paused) return;
        for (size_t i = 0; i < bs.size(); ++i) {
            B &b = bs[i];
            b.vx += axx[i] * dt;
            b.vy += ayy[i] * dt;
            float sp = std::sqrt(b.vx * b.vx + b.vy * b.vy);
            const float MAXS = 95.0f, MINS = 28.0f;
            if (sp > MAXS) { b.vx *= MAXS / sp; b.vy *= MAXS / sp; }
            else if (sp > 0.01f && sp < MINS) { b.vx *= MINS / sp; b.vy *= MINS / sp; }
            b.x += b.vx * dt;
            b.y += b.vy * dt;
            if (b.x < 0) { b.x = 0; b.vx = -b.vx; }
            else if (b.x >= bw - 1) { b.x = (float)bw - 1.0f; b.vx = -b.vx; }
            if (b.y < 0) { b.y = 0; b.vy = -b.vy; }
            else if (b.y >= bh - 1) { b.y = (float)bh - 1.0f; b.vy = -b.vy; }
            size_t bi = (size_t)(int)b.y * (size_t)bw + (size_t)(int)b.x;
            if (bi < buf.size())
                buf[bi] = addC(buf[bi], scaleC(app.tc(std::fmod(hueT + (float)i * 0.004f, 1.0f)), 0.4f));
        }
    }

    void render(App &app, std::vector<Cell> &fb) override {
        for (int y = 0; y < bh; ++y) {
            size_t row = (size_t)y * (size_t)bw;
            for (int x = 0; x < bw; ++x) {
                u32 c = buf[row + (size_t)x];
                if (c) pxSet(fb, bw, bh / 2, x, y, c);
            }
        }
        for (size_t i = 0; i < bs.size(); ++i) {
            const B &b = bs[i];
            float a = std::atan2(b.vy, b.vx);
            float ca = std::cos(a), sa = std::sin(a);
            int x = (int)b.x, y = (int)b.y;
            u32 col = app.tc(std::fmod(hueT + (float)i * 0.004f, 1.0f));
            pxAdd(fb, bw, bh / 2, x, y, col);
            pxAdd(fb, bw, bh / 2, (int)(b.x - ca * 2.0f), (int)(b.y - sa * 2.0f), scaleC(col, 0.6f));
            pxAdd(fb, bw, bh / 2, (int)(b.x - ca * 3.0f - sa), (int)(b.y - sa * 3.0f + ca), scaleC(col, 0.35f));
            pxAdd(fb, bw, bh / 2, (int)(b.x - ca * 3.0f + sa), (int)(b.y - sa * 3.0f - ca), scaleC(col, 0.35f));
        }
    }
};

/*============================ 7. PLASMA ====================================*/

struct PlasmaMode : Mode {
    int w = 0, h = 0;
    int pattern = 0;
    float t = 0.0f;
    float speed = 1.0f;
    static const int NP = 4;
    static const char *const PN[NP];

    void resize(int W, int Ph) override { w = W; h = Ph; }
    const char *name() const override { return "PLASMA"; }
    const char *hint() const override {
        return "mouse: bends the field   p: pattern   , .: pattern   - =: speed";
    }
    const char *optName() const override { return "PATTERN"; }
    std::string optValue(const App &) const override { return PN[clampi(pattern, 0, NP - 1)]; }
    void opt(int d, App &app) override {
        pattern = (pattern + d + NP) % NP;
        app.say(fstr("Pattern: %s", PN[pattern]));
    }
    bool key(int k, App &app) override {
        if (k == 'p') { pattern = (pattern + 1) % NP; app.say(fstr("Pattern: %s", PN[pattern])); return true; }
        if (k == '-' || k == '_') { speed = clampf(speed - 0.2f, 0.2f, 4.0f); app.say(fstr("Speed: %.1f", speed)); return true; }
        if (k == '=' || k == '+') { speed = clampf(speed + 0.2f, 0.2f, 4.0f); app.say(fstr("Speed: %.1f", speed)); return true; }
        return false;
    }

    void update(float dt, App &app) {
        if (!app.paused) t += dt * speed;
    }

    void render(App &app, std::vector<Cell> &fb) override {
        float cx = (float)w * 0.5f, cy = (float)h * 0.5f;
        float mx = (float)app.mx, my = (float)app.my;
        bool mok = app.inPlay && app.mx >= 0 && app.my >= 1;
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float u = (float)x, v = (float)y;
                float val = 0.0f;
                switch (pattern) {
                    case 0:
                        val = std::sin(u * 0.16f + t)
                            + std::sin(v * 0.13f - t * 0.8f)
                            + std::sin((u + v) * 0.09f + t * 0.55f)
                            + std::sin(std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy)) * 0.11f - t);
                        break;
                    case 1: {
                        float d1 = std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy));
                        float d2 = std::sqrt((u - w * 0.22f) * (u - w * 0.22f) + (v - h * 0.7f) * (v - h * 0.7f));
                        val = std::sin(d1 * 0.19f - t * 1.6f) + std::sin(d2 * 0.16f + t * 1.1f)
                            + std::sin((d1 + d2) * 0.07f + t * 0.4f);
                        break;
                    }
                    case 2:
                        val = std::sin(u * 0.11f + std::sin(v * 0.07f + t) * 2.0f)
                            + std::cos(v * 0.12f + std::sin(u * 0.06f - t) * 2.0f)
                            + std::sin((u * u + v * v) * 0.0004f + t * 0.7f);
                        break;
                    default:
                        val = std::sin(u * 0.21f + t) * std::sin(v * 0.19f - t * 0.7f) * 2.0f
                            + std::sin((u - v) * 0.08f + t * 0.5f)
                            + std::cos(std::sqrt((u - cx) * (u - cx) + (v - cy) * (v - cy)) * 0.14f - t * 1.3f);
                        break;
                }
                if (mok) {
                    float dx = u - mx, dy = v - my;
                    float d = std::sqrt(dx * dx + dy * dy);
                    val += std::sin(d * 0.34f - t * 5.0f) * std::exp(-d / 26.0f) * 2.2f;
                }
                float n = clampf(0.5f + 0.5f * (val * 0.33f), 0.0f, 1.0f);
                float vv = clampf(0.55f + 0.45f * std::sin(val * 2.3f + t * 0.4f), 0.18f, 1.0f);
                u32 c = app.tcd(n, vv);
                putCell(fb, app.W, app.H, x, 1 + y, ' ', 0, c);
            }
        }
    }
};
const char *const PlasmaMode::PN[PlasmaMode::NP] = { "CLASSIC", "RINGS", "WARPED", "LATTICE" };

/*============================= 8. RAIN =====================================*/

struct RainMode : Mode {
    struct Col { float y, speed; };
    int w = 0, h = 0;
    std::vector<Col> cols;
    std::vector<uint32_t> gl;
    std::vector<float> bri;
    float mul = 1.0f;
    std::vector<uint32_t> pool;

    void buildPool() {
        pool.clear();
        for (int c = 'A'; c <= 'Z'; ++c) pool.push_back((uint32_t)c);
        for (int c = '0'; c <= '9'; ++c) pool.push_back((uint32_t)c);
        const char *extra = "%&@#$*+=:;.,?/~^";
        for (const char *p = extra; *p; ++p) pool.push_back((uint32_t)*p);
        if (g_utf8) {
            pool.push_back(0x2591);   // ░
            pool.push_back(0x2592);   // ▒
            pool.push_back(0x2593);   // ▓
            pool.push_back(0x2588);   // █
        }
    }
    void resize(int W, int Ph) override {
        w = W;
        h = Ph;
        buildPool();
        cols.assign((size_t)w, Col());
        gl.assign((size_t)w * (size_t)h, (uint32_t)' ');
        bri.assign((size_t)w * (size_t)h, 0.0f);
        for (int i = 0; i < w; ++i) {
            cols[(size_t)i].y = -frnd(0.0f, (float)h);
            cols[(size_t)i].speed = frnd(14.0f, 46.0f);
        }
    }
    void clear() override {
        std::fill(gl.begin(), gl.end(), (uint32_t)' ');
        std::fill(bri.begin(), bri.end(), 0.0f);
    }

    const char *name() const override { return "GLYPH RAIN"; }
    const char *hint() const override {
        return "click: pour into a column   , .: speed   a: downpour   c: wipe";
    }
    const char *optName() const override { return "SPEED"; }
    std::string optValue(const App &) const override { return fstr("%.1fx", mul); }
    void opt(int d, App &app) override {
        mul = clampf(mul + (float)d * 0.2f, 0.2f, 4.0f);
        app.say(fstr("Rain speed: %.1fx", mul));
    }

    void update(float dt, App &app) override {
        if (w <= 0 || h <= 0) return;
        if (app.inPlay && app.lpres && app.mx >= 0 && app.my >= 0) {
            int x = clampi(app.mx, 0, w - 1);
            for (int y = 0; y < h; ++y) {
                size_t i = (size_t)y * (size_t)w + (size_t)x;
                gl[i] = pool[(size_t)rnd((int)pool.size())];
                bri[i] = clampf(1.0f - (float)y / (float)h * 0.6f, 0.0f, 1.0f);
            }
            cols[(size_t)x].y = -frnd(0.0f, (float)h * 0.4f);
        }
        float decay = app.paused ? 1.0f : std::pow(0.42f, dt);
        for (int y = 0; y < h; ++y) {
            size_t row = (size_t)y * (size_t)w;
            for (int x = 0; x < w; ++x) bri[row + (size_t)x] *= decay;
        }
        if (app.paused) return;
        int boost = app.autoMode ? 2 : 1;
        for (int i = 0; i < w; ++i) {
            Col &c = cols[(size_t)i];
            float oldY = c.y;
            c.y += c.speed * dt * mul * (float)boost;
            int from = (int)oldY + 1;
            if (from < 0) from = 0;
            int to = (int)c.y;
            bool dropped = to >= h;
            if (dropped) to = h - 1;
            for (int hy = from; hy <= to; ++hy) {        // fill every cell the head passed
                size_t idx2 = (size_t)hy * (size_t)w + (size_t)i;
                gl[idx2] = pool[(size_t)rnd((int)pool.size())];
                bri[idx2] = 1.0f;
            }
            if (dropped) {
                c.y = -frnd(1.0f, (float)h * 0.6f);
                c.speed = frnd(14.0f, 46.0f);
            }
        }
    }

    void render(App &app, std::vector<Cell> &fb) override {
        u32 glow = app.tc(0.45f);
        u32 dim = scaleC(glow, 0.30f);
        for (int y = 0; y < h; ++y) {
            size_t row = (size_t)y * (size_t)w;
            for (int x = 0; x < w; ++x) {
                float b = clampf(bri[row + (size_t)x], 0.0f, 1.0f);
                if (b < 0.02f) {
                    putCell(fb, app.W, app.H, x, 1 + y, ' ', 0, rgb(4, 6, 9));
                    continue;
                }
                u32 c = mixC(dim, glow, b);
                if (b > 0.86f) c = mixC(c, rgb(255, 255, 255), (b - 0.86f) / 0.14f * 0.9f);
                putCell(fb, app.W, app.H, x, 1 + y, gl[row + (size_t)x], c, rgb(4, 6, 9));
            }
        }
    }
};

/*============================= 9. ZEN ======================================*/

static const char *const ZEN_QUOTES[] = {
    "The quieter you become, the more you can hear.",
    "Breath is the anchor of a busy mind.",
    "Let the water settle, the reflection returns.",
    "You are not the storm - you are the sky behind it.",
    "Slow down. Nothing is missing right now.",
    "Stillness is not emptiness, it is fullness.",
    "One breath at a time is enough.",
    "Bloom exactly where you are planted.",
};
static const int NZQUOTES = (int)(sizeof ZEN_QUOTES / sizeof ZEN_QUOTES[0]);

struct BreathPhase { const char *label; float dur; };

struct ZenMode : Mode {
    struct Orb { float x, y, r, ph, sp; u32 col; };
    struct Star { int x, y; float ph, sp; };
    std::vector<Orb> orbs;
    std::vector<Star> stars;
    float t = 0.0f;
    int norb = 9;
    int pw = 0, phh = 0;
    int phase = 0;
    float phaseT = 0.0f;
    int quote = 0;
    float quoteT = 0.0f;
    static const BreathPhase PH[4];
    float breath = 0.0f;

    void build() {
        int W = pw, Ph = phh;
        float bw = (float)std::max(1, W), bh = (float)std::max(1, Ph * 2);
        orbs.clear();
        for (int i = 0; i < norb; ++i) {
            Orb o;
            o.x = frnd(0.0f, bw);
            o.y = frnd(0.0f, bh);
            o.r = frnd(4.0f, 11.0f);
            o.ph = frnd(0.0f, 6.2831853f);
            o.sp = frnd(0.25f, 0.9f);
            o.col = hsv(frnd(-30.0f, 60.0f), 0.55f, 1.0f);
            orbs.push_back(o);
        }
        stars.clear();
        int n = clampi(std::max(1, W) * std::max(1, Ph) / 26, 12, 320);
        for (int i = 0; i < n; ++i) {
            Star s;
            s.x = rnd(std::max(1, W));
            s.y = rnd(std::max(1, Ph * 2));
            s.ph = frnd(0.0f, 6.2831853f);
            s.sp = frnd(0.6f, 2.4f);
            stars.push_back(s);
        }
    }
    void resize(int W, int Ph) override {
        pw = W;
        phh = Ph;
        build();
    }
    void clear() override { build(); }

    const char *name() const override { return "ZEN"; }
    const char *hint() const override {
        return "just watch   , .: orb count   space: pause the breath   c: new stars";
    }
    const char *optName() const override { return "ORBS"; }
    std::string optValue(const App &) const override { return fstr("%d", norb); }
    void opt(int d, App &app) override {
        norb = clampi(norb + d, 0, 40);
        app.say(fstr("Orbs: %d", norb));
        resize(app.W, app.ph);
    }

    void update(float dt, App &app) {
        if (app.paused) return;
        t += dt;
        phaseT += dt;
        if (phaseT >= PH[phase].dur) {
            phaseT = 0.0f;
            phase = (phase + 1) % 4;
        }
        switch (phase) {
            case 0: breath = clampf(phaseT / PH[0].dur, 0, 1); break;
            case 1: breath = 1.0f; break;
            case 2: breath = 1.0f - clampf(phaseT / PH[2].dur, 0, 1); break;
            default: breath = 0.0f; break;
        }
        quoteT += dt;
        if (quoteT > 14.0f) {
            quoteT = 0.0f;
            quote = (quote + 1) % NZQUOTES;
        }
        float bw = (float)app.W, bh = (float)(app.ph * 2);
        for (size_t i = 0; i < orbs.size(); ++i) {
            Orb &o = orbs[i];
            o.x += std::cos(t * o.sp * 0.5f + o.ph) * 9.0f * dt;
            o.y += std::sin(t * o.sp * 0.37f + o.ph * 1.7f) * 7.0f * dt;
            if (o.x < -o.r) o.x = bw + o.r;
            if (o.x > bw + o.r) o.x = -o.r;
            if (o.y < -o.r) o.y = bh + o.r;
            if (o.y > bh + o.r) o.y = -o.r;
        }
    }

    void render(App &app, std::vector<Cell> &fb) override {
        if (app.W <= 0 || app.ph <= 0) return;
        int bwid = app.W, bhei = app.ph * 2;
        float cx = (float)bwid * 0.5f, cy = (float)bhei * 0.5f;
        float base = std::min((float)bwid, (float)bhei);

        for (size_t i = 0; i < stars.size(); ++i) {
            const Star &s = stars[i];
            float tw = 0.35f + 0.65f * std::fabs(std::sin(s.ph + t * s.sp));
            pxBlend(fb, bwid, app.ph, s.x, s.y, rgb(255, 255, 255), tw * 0.45f);
        }

        for (size_t i = 0; i < orbs.size(); ++i) {
            const Orb &o = orbs[i];
            int r = (int)o.r;
            u32 c = mixC(o.col, app.tc(std::fmod((float)i * 0.12f + t * 0.03f, 1.0f)), 0.5f);
            for (int y = -r; y <= r; ++y)
                for (int x = -r; x <= r; ++x) {
                    float d = std::sqrt((float)(x * x + y * y)) / (float)std::max(1, r);
                    if (d > 1.0f) continue;
                    float a = (1.0f - d) * (1.0f - d) * 0.55f;
                    pxBlend(fb, bwid, app.ph, (int)o.x + x, (int)o.y + y, c, a);
                }
        }

        // mandala rings
        for (int ring = 0; ring < 3; ++ring) {
            float rad = base * (0.13f + 0.05f * (float)ring);
            int n = 7 + ring * 5;
            float rot = t * (ring % 2 ? -0.35f : 0.28f) + (float)ring;
            for (int i = 0; i < n; ++i) {
                float a = rot + (float)i / (float)n * 6.2831853f;
                int x = (int)(cx + std::cos(a) * rad);
                int y = (int)(cy + std::sin(a) * rad);
                u32 c = app.tc(std::fmod((float)i / (float)n + (float)ring * 0.2f, 1.0f));
                pxDisc(fb, bwid, app.ph, x, y, 1, c, 0.75f);
            }
        }

        // breathing ring
        float rr = base * (0.11f + 0.085f * breath);
        u32 ringCol = mixC(app.tc(0.55f), rgb(255, 255, 255), 0.25f + 0.35f * breath);
        int steps = std::max(24, (int)(rr * 7.0f));
        for (int i = 0; i < steps; ++i) {
            float a = (float)i / (float)steps * 6.2831853f;
            int x = (int)(cx + std::cos(a) * rr);
            int y = (int)(cy + std::sin(a) * rr);
            float pulse = 0.75f + 0.25f * std::sin(t * 2.0f + a * 3.0f);
            pxBlend(fb, bwid, app.ph, x, y, ringCol, 0.85f * pulse);
            pxBlend(fb, bwid, app.ph, x + 1, y, ringCol, 0.28f * pulse);
            pxBlend(fb, bwid, app.ph, x - 1, y, ringCol, 0.28f * pulse);
            pxBlend(fb, bwid, app.ph, x, y + 1, ringCol, 0.28f * pulse);
            pxBlend(fb, bwid, app.ph, x, y - 1, ringCol, 0.28f * pulse);
        }
        pxDisc(fb, bwid, app.ph, (int)cx, (int)cy, (int)(rr * 0.55f),
               app.tc(std::fmod(t * 0.05f, 1.0f)), 0.10f + 0.14f * breath);

        // labels
        std::string label = PH[phase].label;
        float fade = (phase == 0) ? clampf(phaseT / 0.6f, 0, 1)
                   : (phase == 2) ? clampf(1.0f - phaseT / 1.2f, 0, 1)
                                  : 1.0f;
        int tx = app.W / 2 - (int)label.size() / 2;
        int ty = 1 + app.ph / 2;
        drawText(fb, app.W, app.H, tx, ty, label, scaleC(rgb(255, 255, 255), 0.4f + 0.6f * fade), 0, false);

        std::string q = std::string("\"") + ZEN_QUOTES[quote % NZQUOTES] + "\"";
        int qx = app.W / 2 - (int)q.size() / 2;
        drawText(fb, app.W, app.H, qx, app.ph, q, scaleC(app.tc(0.75f), 0.85f), 0, false);
    }
};
const BreathPhase ZenMode::PH[4] = {
    { "BREATHE IN", 4.0f }, { "HOLD", 2.0f }, { "BREATHE OUT", 6.0f }, { "REST", 1.0f }
};

/*=========================== HUD / chrome ==================================*/

static int tb(std::vector<Cell> &fb, App &app, int x, int y, const std::string &s,
              u32 fg, u32 bg, bool solid) {
    if (x >= app.W) return x;
    drawText(fb, app.W, app.H, x, y, s, fg, bg, solid);
    return x + (int)s.size();
}

static void drawHud(App &app, std::vector<Cell> &fb) {
    if (app.W < 8 || app.H < 4) return;
    u32 bar = rgb(13, 15, 22);
    u32 foot = rgb(10, 12, 18);
    u32 dimc = rgb(122, 128, 144);
    u32 bright = rgb(236, 239, 246);
    u32 accent = app.tc(0.5f);

    fillRect(fb, app.W, app.H, 0, 0, app.W, 1, bar);
    int x = 1;
    x = tb(fb, app, x, 0, " SATISFY ", bar, accent, true) + 1;
    x = tb(fb, app, x, 0, fstr("%d/%d", app.modeIdx + 1, (int)app.modes.size()), dimc, bar, false) + 1;
    x = tb(fb, app, x, 0, app.mode()->name(), bright, bar, false) + 2;
    u32 fpsc = app.fps >= 45.0f ? rgb(96, 224, 148) : (app.fps >= 25.0f ? rgb(244, 202, 96) : rgb(244, 112, 112));
    x = tb(fb, app, x, 0, fstr("%.0f fps", app.fps), fpsc, bar, false);
    const char *on = app.mode()->optName();
    if (on && *on)
        x = tb(fb, app, x + 2, 0, fstr("%s=%s", on, app.mode()->optValue(app).c_str()), dimc, bar, false);
    x = tb(fb, app, x + 2, 0, fstr("brush %d", app.brush), dimc, bar, false);
    x = tb(fb, app, x + 2, 0, app.themeName(), accent, bar, false);
    if (app.autoMode) x = tb(fb, app, x + 2, 0, "AUTO", rgb(255, 178, 84), bar, false);
    if (app.paused) x = tb(fb, app, x + 2, 0, "PAUSED", rgb(255, 120, 120), bar, false);

    std::string right = "  h help   q quit  ";
    int rx = app.W - (int)right.size() - 1;
    if (rx > x) drawText(fb, app.W, app.H, rx, 0, right, rgb(92, 98, 114), bar, false);

    fillRect(fb, app.W, app.H, 0, app.H - 1, app.W, 1, foot);
    std::string hint = app.mode()->hint();
    int maxHint = app.W - 4;
    if (maxHint < 0) maxHint = 0;
    if ((int)hint.size() > maxHint) hint.resize((size_t)maxHint);
    drawText(fb, app.W, app.H, 2, app.H - 1, hint, rgb(112, 118, 136), foot, false);
    if (app.toastT > 0.0f && !app.toast.empty()) {
        std::string t = " " + app.toast + " ";
        int tx = app.W - (int)t.size() - 1;
        if (tx < 1) tx = 1;
        drawText(fb, app.W, app.H, tx, app.H - 1, t, rgb(8, 10, 16), accent, true);
    }
}

static void drawHelp(App &app, std::vector<Cell> &fb) {
    static const char *const LINES[] = {
        "@GLOBAL KEYS",
        "   1-9 / Tab ..... switch world      space ..... pause / resume",
        "   h / ? ......... this help         c ......... clear the world",
        "   t ............. next theme        a ......... auto-demo on/off",
        "   [ ] wheel ..... brush size        , . ....... world option",
        "   S ............. screenshot        q / Ctrl-C  quit cleanly",
        "@WORLDS",
        "   1 falling sand    2 game of life    3 water ripple",
        "   4 particles       5 fireworks       6 boids",
        "   7 plasma          8 glyph rain      9 zen",
        "@MOUSE",
        "   left button ... paint, attract     right button ... erase, repel",
        "   mouse move ... brush preview       wheel .......... brush size",
        "@NOTES",
        "   Every world has its own keys - see the bottom bar.",
        "   Resize the terminal whenever you like; worlds adapt.",
        "   The sand box holds 15 materials that react with each other.",
    };
    int n = (int)(sizeof LINES / sizeof LINES[0]);
    if (app.W < 30 || app.H < 8) {
        drawText(fb, app.W, app.H, 1, 0, "help: terminal too small (need 30x8)",
                 rgb(255, 200, 90), 0, false);
        return;
    }
    int wid = 40;
    for (int i = 0; i < n; ++i) wid = std::max(wid, (int)strlen(LINES[i]) + 4);
    wid = std::min(wid, app.W - 2);
    int h = std::min(n + 2, app.H - 2);
    int rows = h - 2;
    if (rows > n) rows = n;
    if (rows < 0) rows = 0;
    int x = (app.W - wid) / 2;
    int y = (app.H - h) / 2;
    u32 bg = rgb(9, 11, 17);
    u32 accent = app.tc(0.5f);
    u32 line = rgb(196, 202, 216);
    fillRect(fb, app.W, app.H, x, y, wid, h, bg);
    drawFrame(fb, app.W, app.H, x, y, wid, h, "KEYS", rgb(78, 84, 100), bg);
    int maxLine = wid - 4;
    if (maxLine < 1) maxLine = 1;
    for (int i = 0; i < rows; ++i) {
        std::string s = LINES[i];
        bool head = !s.empty() && s[0] == '@';
        if (head) s.erase(0, 1);
        if ((int)s.size() > maxLine) s.resize((size_t)maxLine);
        drawText(fb, app.W, app.H, x + 2, y + 1 + i, s, head ? accent : line, bg, false);
    }
}

/*============================ screenshots =================================*/

static bool saveScreenshot(const std::vector<Cell> &fb, int W, int H, std::string &nameOut) {
    std::vector<Cell> junk((size_t)W * (size_t)H);
    for (size_t i = 0; i < junk.size(); ++i) junk[i].ch = 0xFFFFFFFFu;
    std::string body;
    present(fb, junk, W, H, body, true, false);
    time_t now = time(NULL);
    struct tm tmv;
    memset(&tmv, 0, sizeof tmv);
    localtime_r(&now, &tmv);
    char name[80];
    snprintf(name, sizeof name, "satisfy_%02d%02d%02d_%02d.ans",
             tmv.tm_mon + 1, tmv.tm_mday, tmv.tm_hour, tmv.tm_min);
    FILE *f = fopen(name, "wb");
    if (!f) return false;
    const char *head = "\x1b[2J\x1b[H";
    const char *tail = "\x1b[0m\n";
    fwrite(head, 1, strlen(head), f);
    fwrite(body.data(), 1, body.size(), f);
    fwrite(tail, 1, strlen(tail), f);
    fclose(f);
    nameOut = name;
    return true;
}

/*============================== plumbing ==================================*/

static void doResize(App &app, std::vector<Cell> &fb, std::vector<Cell> &prev) {
    int w = 80, hgt = 24;
    struct winsize ws;
    if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0 && ws.ws_row > 0) {
        w = (int)ws.ws_col;
        hgt = (int)ws.ws_row;
    }
    w = clampi(w, 20, 500);
    hgt = clampi(hgt, 6, 200);
    app.W = w;
    app.H = hgt;
    app.ph = std::max(1, hgt - 2);
    fb.assign((size_t)w * (size_t)hgt, Cell());
    prev.assign((size_t)w * (size_t)hgt, Cell());
    for (size_t i = 0; i < app.modes.size(); ++i) app.modes[i]->resize(w, app.ph);
    static const char *clr = "\x1b[2J\x1b[H";
    ssize_t rc = write(1, clr, 7);
    (void)rc;
}

static void switchMode(App &app, int idx) {
    if (idx < 0 || idx >= (int)app.modes.size()) return;
    app.modeIdx = idx;
    app.burst((float)app.W * 0.5f, (float)app.ph, 140);
    app.say(fstr("World %d: %s", idx + 1, app.modes[(size_t)idx]->name()));
}

static void handleKeys(App &app, Input &in, bool &quit) {
    if (in.quit) quit = true;
    for (size_t i = 0; i < in.keys.size(); ++i) {
        int k = in.keys[i];
        if (k == 3) { quit = true; break; }
        if (app.help) {
            if (k == K_ESC || k == 'h' || k == '?' || k == ' ') app.help = false;
            else if (k >= '1' && k <= '9') { app.help = false; switchMode(app, k - '1'); }
            else if (k == K_TAB) { app.help = false; switchMode(app, (app.modeIdx + 1) % (int)app.modes.size()); }
            continue;
        }
        switch (k) {
            case 'q': case 'Q': quit = true; break;
            case 'h': case '?': app.help = true; break;
            case K_TAB: switchMode(app, (app.modeIdx + 1) % (int)app.modes.size()); break;
            case ' ': app.paused = !app.paused; app.say(app.paused ? "Paused" : "Resumed"); break;
            case 'c': app.mode()->clear(); app.say("Cleared"); break;
            case 't':
                app.themeIdx = (app.themeIdx + 1) % NTHEMES;
                app.say(std::string("Theme: ") + app.themeName());
                break;
            case 'a':
                app.autoMode = !app.autoMode;
                app.say(std::string("Auto demo: ") + (app.autoMode ? "on" : "off"));
                break;
            case 'S':
                app.wantShot = true;
                break;
            case '[': case K_DOWN:
                app.brush = clampi(app.brush - 1, 0, 14);
                app.say(fstr("Brush: %d", app.brush));
                break;
            case ']': case K_UP:
                app.brush = clampi(app.brush + 1, 0, 14);
                app.say(fstr("Brush: %d", app.brush));
                break;
            case ',': case K_LEFT: app.mode()->opt(-1, app); break;
            case '.': case K_RIGHT: app.mode()->opt(+1, app); break;
            default:
                if (k >= '1' && k <= '9') switchMode(app, k - '1');
                else app.mode()->key(k, app);
                break;
        }
        if (quit) break;
    }
}

static void printUsage() {
    printf(
        "satisfy - a satisfying terminal toybox\n"
        "usage: satisfy [options]\n"
        "  --mode NAME|N    start world: sand life ripple particles fireworks\n"
        "                   boids plasma rain zen  (or 1..9)\n"
        "  --theme NAME     NEON SUNSET OCEAN CANDY FOREST RAINBOW\n"
        "  --fps N          frame-rate cap (default 60)\n"
        "  --demo           start with auto-demo enabled\n"
        "  --frames N       render N frames then exit (for testing)\n"
        "  --no-mouse       ignore mouse input\n"
        "  --help           this message\n"
        "\n"
        "inside the app:  h help   1-9 / Tab worlds   space pause   q quit\n");
}

int main(int argc, char **argv) {
    double fpsCap = 60.0;
    long frames = -1;
    int startMode = 0, startTheme = -1;
    bool demo = false;

    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--help" || a == "-h") { printUsage(); return 0; }
        else if (a == "--frames" && i + 1 < argc) frames = atol(argv[++i]);
        else if (a == "--fps" && i + 1 < argc) fpsCap = atof(argv[++i]);
        else if (a == "--demo") demo = true;
        else if (a == "--no-mouse") g_mouse = false;
        else if (a == "--theme" && i + 1 < argc) {
            const char *want = argv[++i];
            startTheme = 0;
            for (int t = 0; t < NTHEMES; ++t)
                if (hasCI(THEMES[t].name, want)) { startTheme = t; break; }
        } else if (a == "--mode" && i + 1 < argc) {
            const char *want = argv[++i];
            startMode = atoi(want);
            if (startMode < 1 || startMode > 9) {
                const char *names[] = { "sand", "life", "ripple", "particles", "fireworks",
                                        "boids", "plasma", "rain", "zen" };
                startMode = 1;
                for (int m = 0; m < 9; ++m)
                    if (hasCI(names[m], want)) { startMode = m + 1; break; }
            }
        } else {
            fprintf(stderr, "satisfy: unknown option '%s' (try --help)\n", a.c_str());
            return 1;
        }
    }
    if (fpsCap < 5.0) fpsCap = 5.0;
    if (fpsCap > 240.0) fpsCap = 240.0;

    // unicode detection
    const char *lc = getenv("LC_ALL");
    if (!lc || !*lc) lc = getenv("LC_CTYPE");
    if (!lc || !*lc) lc = getenv("LANG");
    bool utf8 = (lc && *lc) ? hasCI(lc, "UTF-8") : true;
    const char *term = getenv("TERM");
    if (term && strcmp(term, "dumb") == 0) utf8 = false;
    g_utf8 = utf8;
    g_tty = isatty(0) && isatty(1);

    setupTerminal();

    App app;
    app.modes.push_back(new SandMode());
    app.modes.push_back(new LifeMode());
    app.modes.push_back(new RippleMode());
    app.modes.push_back(new ParticlesMode());
    app.modes.push_back(new FireworksMode());
    app.modes.push_back(new BoidsMode());
    app.modes.push_back(new PlasmaMode());
    app.modes.push_back(new RainMode());
    app.modes.push_back(new ZenMode());
    app.modeIdx = clampi(startMode - 1, 0, (int)app.modes.size() - 1);
    if (startTheme >= 0) app.themeIdx = clampi(startTheme, 0, NTHEMES - 1);
    app.autoMode = demo;

    std::vector<Cell> fb, prev;
    std::string out;
    doResize(app, fb, prev);
    app.say("Press h for help - q to quit");

    Input in;
    bool quit = false;
    double dt = 1.0 / fpsCap;
    auto prevT = std::chrono::steady_clock::now();
    long done = 0;
    float fpsSmooth = (float)fpsCap;

    while (!quit) {
        auto t0 = std::chrono::steady_clock::now();
        dt = std::chrono::duration<double>(t0 - prevT).count();
        prevT = t0;
        if (dt < 0.0005) dt = 0.0005;
        if (dt > 0.1) dt = 0.1;

        in.keys.clear();
        in.lpress = in.rpress = in.lrel = false;
        in.wheel = 0;
        in.motion = false;
        in.eof = false;
        in.quit = false;
        readInput(in);
        if (in.eof && frames < 0) quit = true;
        if (g_stopped) quit = true;
        if (g_resized) {
            g_resized = 0;
            doResize(app, fb, prev);
        }

        if (in.wheel != 0) {
            app.brush = clampi(app.brush + in.wheel, 0, 14);
            app.say(fstr("Brush: %d", app.brush));
        }
        app.setMouse(in);
        handleKeys(app, in, quit);
        if (quit) break;
        if (app.help) {                      // never paint through the overlay
            app.inPlay = false;
            app.lmb = app.rmb = app.lpres = app.rpres = false;
        }

        app.time += (float)dt;
        app.mode()->update((float)dt, app);
        app.updateConf((float)dt);
        if (app.toastT > 0.0f) app.toastT -= (float)dt;

        std::fill(fb.begin(), fb.end(), Cell());
        app.mode()->render(app, fb);
        app.renderConf(fb);
        if (app.help) drawHelp(app, fb);
        drawHud(app, fb);
        present(fb, prev, app.W, app.H, out, false, true);
        fwrite(out.data(), 1, out.size(), stdout);
        fflush(stdout);

        if (app.wantShot) {
            app.wantShot = false;
            std::string nm;
            if (saveScreenshot(fb, app.W, app.H, nm)) app.say("Saved " + nm);
            else app.say("Screenshot failed");
        }

        double used = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        sleepFor(1.0 / fpsCap - used);

        if (dt > 0.0008f) fpsSmooth = fpsSmooth * 0.92f + (float)(1.0 / dt) * 0.08f;
        app.fps = fpsSmooth;
        ++done;
        if (frames > 0 && done >= frames) break;
    }

    restoreTerminal();
    for (size_t i = 0; i < app.modes.size(); ++i) delete app.modes[i];
    return 0;
}

// [[MAIN]]
