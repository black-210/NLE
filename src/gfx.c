/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * Software 3D rasterizer (z-buffer, flat shading) + ANSI truecolor terminal output. */
#include "nle.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <termios.h>
#include <sys/ioctl.h>

Fb *fb_new(int w, int h) {
    Fb *f = calloc(1, sizeof *f); f->w = w; f->h = h;
    f->rgb = calloc((size_t)w * h, 3); f->z = malloc(sizeof(float) * (size_t)w * h);
    return f;
}
void fb_free(Fb *f) { if (f) { free(f->rgb); free(f->z); free(f); } }
void fb_clear(Fb *f) {
    for (int y = 0; y < f->h; y++) for (int x = 0; x < f->w; x++) {   /* dark vertical gradient */
        uint8_t *p = f->rgb + 3 * ((size_t)y * f->w + x); int g = 8 + 18 * y / f->h;
        p[0] = (uint8_t)g; p[1] = (uint8_t)g; p[2] = (uint8_t)(g + 14);
        f->z[(size_t)y * f->w + x] = 1e30f;
    }
}
void fb_px(Fb *f, int x, int y, float z, const uint8_t *c) {
    if (x < 0 || y < 0 || x >= f->w || y >= f->h) return;
    size_t i = (size_t)y * f->w + x;
    if (z < f->z[i]) { f->z[i] = z; memcpy(f->rgb + 3 * i, c, 3); }
}
void fb_dot(Fb *f, P3 p, int r, const uint8_t *c) {
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++)
        fb_px(f, (int)lround(p.x) + dx, (int)lround(p.y) + dy, (float)p.z - 0.05f, c);
}
void fb_line(Fb *f, P3 a, P3 b, const uint8_t *c) {
    double dx = b.x - a.x, dy = b.y - a.y, m = fabs(dx) > fabs(dy) ? fabs(dx) : fabs(dy);
    if (!(m < 5000)) return;
    int n = (int)m + 1;
    for (int i = 0; i <= n; i++) {
        double t = (double)i / n;
        fb_px(f, (int)lround(a.x + dx * t), (int)lround(a.y + dy * t), (float)(a.z + (b.z - a.z) * t) - 0.02f, c);
    }
}
void fb_tri(Fb *f, P3 a, P3 b, P3 c, const uint8_t *col) {
    double minx = fmin(a.x, fmin(b.x, c.x)), maxx = fmax(a.x, fmax(b.x, c.x));
    double miny = fmin(a.y, fmin(b.y, c.y)), maxy = fmax(a.y, fmax(b.y, c.y));
    int x0 = (int)fmax(0, floor(minx)), x1 = (int)fmin(f->w - 1, ceil(maxx));
    int y0 = (int)fmax(0, floor(miny)), y1 = (int)fmin(f->h - 1, ceil(maxy));
    double d = (b.y - c.y) * (a.x - c.x) + (c.x - b.x) * (a.y - c.y);
    if (fabs(d) < 1e-9) return;
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
        double px = x + .5, py = y + .5;
        double u = ((b.y - c.y) * (px - c.x) + (c.x - b.x) * (py - c.y)) / d;
        double v = ((c.y - a.y) * (px - c.x) + (a.x - c.x) * (py - c.y)) / d;
        double w = 1 - u - v;
        if (u >= -1e-6 && v >= -1e-6 && w >= -1e-6) fb_px(f, x, y, (float)(u * a.z + v * b.z + w * c.z), col);
    }
}
/* Orbit camera: yaw about z, pitch about x, perspective projection. */
int cam_proj(const Cam *c, const Fb *f, double x, double y, double z, P3 *o) {
    double x1 = x * cos(c->yaw) - y * sin(c->yaw), y1 = x * sin(c->yaw) + y * cos(c->yaw);
    double y2 = y1 * cos(c->pitch) - z * sin(c->pitch), z2 = y1 * sin(c->pitch) + z * cos(c->pitch);
    double depth = y2 + c->dist;
    if (depth < 0.1) return 0;
    double s = f->h * 1.15 / depth;
    o->x = f->w / 2.0 + x1 * s; o->y = f->h / 2.0 - z2 * s; o->z = depth;
    return 1;
}
int fb_ppm(const Fb *f, const char *path) {
    FILE *o = fopen(path, "wb"); if (!o) return -1;
    fprintf(o, "P6\n%d %d\n255\n", f->w, f->h);
    fwrite(f->rgb, 3, (size_t)f->w * f->h, o); fclose(o); return 0;
}
/* Two pixels per terminal cell using the upper-half block. */
void fb_present(const Fb *f) {
    size_t cap = (size_t)f->w * (f->h / 2 + 1) * 44 + 64, n = 0;
    char *b = malloc(cap); int lt[3] = {-1, -1, -1}, lb[3] = {-1, -1, -1};
    n += (size_t)sprintf(b, "\x1b[H");
    for (int y = 0; y + 1 < f->h + 1; y += 2) {
        for (int x = 0; x < f->w; x++) {
            const uint8_t *t = f->rgb + 3 * ((size_t)y * f->w + x);
            const uint8_t *u = y + 1 < f->h ? f->rgb + 3 * ((size_t)(y + 1) * f->w + x) : t;
            if (t[0] != lt[0] || t[1] != lt[1] || t[2] != lt[2])
                n += (size_t)sprintf(b + n, "\x1b[38;2;%d;%d;%dm", t[0], t[1], t[2]);
            if (u[0] != lb[0] || u[1] != lb[1] || u[2] != lb[2])
                n += (size_t)sprintf(b + n, "\x1b[48;2;%d;%d;%dm", u[0], u[1], u[2]);
            for (int k = 0; k < 3; k++) { lt[k] = t[k]; lb[k] = u[k]; }
            memcpy(b + n, "\xe2\x96\x80", 3); n += 3;
        }
        n += (size_t)sprintf(b + n, "\x1b[0m\n"); lt[0] = lb[0] = -1;
    }
    fwrite(b, 1, n, stdout); fflush(stdout); free(b);
}

/* ---------------- terminal ---------------- */
static struct termios old; static int raw_on;
static void onsig(int s) { (void)s; term_raw(0); _exit(130); }
void term_raw(int on) {
    if (on && !raw_on) {
        struct termios t; tcgetattr(0, &old); t = old;
        t.c_lflag &= ~(tcflag_t)(ICANON | ECHO); t.c_cc[VMIN] = 0; t.c_cc[VTIME] = 0;
        tcsetattr(0, TCSANOW, &t); signal(SIGINT, onsig); signal(SIGTERM, onsig);
        fputs("\x1b[?1049h\x1b[?25l\x1b[2J", stdout); fflush(stdout); raw_on = 1;
    } else if (!on && raw_on) {
        tcsetattr(0, TCSANOW, &old); fputs("\x1b[0m\x1b[?25h\x1b[?1049l", stdout); fflush(stdout); raw_on = 0;
    }
}
void term_size(int *cols, int *rows) {
    struct winsize w;
    if (ioctl(1, TIOCGWINSZ, &w) == 0 && w.ws_col > 0) { *cols = w.ws_col; *rows = w.ws_row; }
    else { *cols = 100; *rows = 36; }
}
int term_key(void) {
    unsigned char c;
    if (read(0, &c, 1) != 1) return -1;
    if (c == 27) {
        unsigned char s[2];
        if (read(0, s, 2) == 2 && s[0] == '[') {
            switch (s[1]) { case 'A': return K_UP; case 'B': return K_DOWN; case 'C': return K_RIGHT; case 'D': return K_LEFT; }
        }
        return 27;
    }
    return c;
}
