/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * Interactive 3D worlds: surfaces, chaotic attractors, gravity, a learning brain, truth hypercubes. */
#include "nle.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

const char *g_ppm = NULL;
int g_steps = 240;
static char g_msg[200];
static int g_spin0;

typedef void (*stepfn)(void *, double);
typedef void (*drawfn)(void *, Fb *, const Cam *);

/* ---------------- shared main loop ---------------- */
static int run_view(stepfn step, drawfn draw, void *u) {
    Cam c = {0.6, 0.55, 5.0};
    if (g_ppm) {                                   /* offline: simulate, render one frame, save */
        Fb *f = fb_new(480, 320);
        for (int i = 0; i < g_steps; i++) step(u, 1.0 / 30);
        fb_clear(f); draw(u, f, &c);
        int r = fb_ppm(f, g_ppm);
        printf("%s %s\n%s\n", r ? "write failed:" : "wrote", g_ppm, g_msg);
        fb_free(f); return r;
    }
    if (!isatty(0) || !isatty(1)) { puts("interactive 3D needs a terminal (or use: nle --render out.ppm <command>)"); return -1; }
    int cols, rows, run = 1, paused = 0, spin = g_spin0, shots = 0, tick = 0;
    term_size(&cols, &rows);
    Fb *f = fb_new(cols, 2 * (rows - 2)); term_raw(1);
    while (run) {
        int k;
        while ((k = term_key()) != -1) switch (k) {
            case 'q': case 27: run = 0; break;
            case K_LEFT: case 'a': c.yaw -= .08; break;
            case K_RIGHT: case 'd': c.yaw += .08; break;
            case K_UP: case 'w': c.pitch = fmin(1.5, c.pitch + .06); break;
            case K_DOWN: case 's': c.pitch = fmax(-1.5, c.pitch - .06); break;
            case '+': case '=': c.dist = fmax(1.5, c.dist * .93); break;
            case '-': c.dist = fmin(20, c.dist / .93); break;
            case ' ': paused ^= 1; break;
            case 'r': spin ^= 1; break;
            case 'p': { char nm[64]; sprintf(nm, "nle_snap_%d.ppm", ++shots); fb_ppm(f, nm); } break;
        }
        if (++tick % 20 == 0) {                    /* follow terminal resizes */
            int cc, rr; term_size(&cc, &rr);
            if (cc != cols || rr != rows) { cols = cc; rows = rr; fb_free(f); f = fb_new(cols, 2 * (rows - 2)); fputs("\x1b[2J", stdout); }
        }
        if (!paused) step(u, 1.0 / 30);
        if (spin) c.yaw += 0.012;
        fb_clear(f); draw(u, f, &c); fb_present(f);
        printf("\x1b[0m%s\x1b[K\n[arrows/wasd rotate | +/- zoom | space pause | r spin | p snapshot | q quit]\x1b[K", g_msg);
        fflush(stdout);
        struct timespec ts = {0, 33000000}; nanosleep(&ts, 0);
    }
    term_raw(0); fb_free(f); return 0;
}

/* ---------------- helpers ---------------- */
static void pal(double t, uint8_t *o) {
    static const uint8_t c[4][3] = {{30, 60, 210}, {40, 200, 220}, {250, 220, 60}, {230, 50, 50}};
    t = fmin(1, fmax(0, t)) * 3; int i = (int)t; if (i > 2) i = 2; double u = t - i;
    for (int k = 0; k < 3; k++) o[k] = (uint8_t)(c[i][k] * (1 - u) + c[i + 1][k] * u);
}
static void box(Fb *f, const Cam *c, double hx, double hz) {
    static const uint8_t col[3] = {60, 72, 105}; P3 p[8]; int ok = 1;
    for (int i = 0; i < 8; i++) ok &= cam_proj(c, f, i & 1 ? hx : -hx, i & 2 ? hx : -hx, i & 4 ? hz : -hz, &p[i]);
    if (!ok) return;
    for (int i = 0; i < 8; i++) for (int k = 0; k < 3; k++) { int j = i ^ (1 << k); if (i < j) fb_line(f, p[i], p[j], col); }
}
static void seg(Fb *f, const Cam *c, const double *a, const double *b, const uint8_t *col) {
    P3 p, q;
    if (cam_proj(c, f, a[0], a[1], a[2], &p) && cam_proj(c, f, b[0], b[1], b[2], &q)) fb_line(f, p, q, col);
}
static void fade(const uint8_t *base, double a, uint8_t *o) {
    a = 0.2 + 0.8 * a; for (int k = 0; k < 3; k++) o[k] = (uint8_t)(base[k] * a);
}

/* ---------------- 3D surface z = f(x,y,t) ---------------- */
typedef double (*zfn)(void *, double, double, double);
typedef struct { zfn f; void *u; double cx, cy, R, t, zc, zs, zmin, zmax; int N; } Surf;
#define SN 48
static void surf_draw(Surf *s, Fb *f, const Cam *c) {
    static double Z[(SN + 1) * (SN + 1)], W[(SN + 1) * (SN + 1)][3]; static P3 Q[(SN + 1) * (SN + 1)];
    int N = s->N, n1 = N + 1; double lo = 1e300, hi = -1e300;
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        double x = s->cx + s->R * (2.0 * i / N - 1), y = s->cy + s->R * (2.0 * j / N - 1);
        double z = s->f(s->u, x, y, s->t); Z[j * n1 + i] = z;
        if (isfinite(z) && fabs(z) < 1e6) { lo = fmin(lo, z); hi = fmax(hi, z); } else Z[j * n1 + i] = NAN;
    }
    if (lo > hi) { lo = -1; hi = 1; }
    s->zmin = lo; s->zmax = hi; s->zc = (lo + hi) / 2;
    double half = (hi - lo) / 2; s->zs = 1.0 / (half < 1e-9 ? 1 : half);
    for (int j = 0; j <= N; j++) for (int i = 0; i <= N; i++) {
        int k = j * n1 + i; double *w = W[k];
        w[0] = (2.0 * i / N - 1) * 1.5; w[1] = (2.0 * j / N - 1) * 1.5; w[2] = (Z[k] - s->zc) * s->zs;
        if (isnan(Z[k]) || !cam_proj(c, f, w[0], w[1], w[2], &Q[k])) Q[k].z = -1;
    }
    for (int j = 0; j < N; j++) for (int i = 0; i < N; i++) {
        int a = j * n1 + i, b = a + 1, d = a + n1, e = d + 1;
        int tri[2][3] = {{a, b, d}, {b, e, d}};
        for (int t = 0; t < 2; t++) {
            int p0 = tri[t][0], p1 = tri[t][1], p2 = tri[t][2];
            if (Q[p0].z < 0 || Q[p1].z < 0 || Q[p2].z < 0) continue;
            double u[3], v[3], nn[3];
            for (int k = 0; k < 3; k++) { u[k] = W[p1][k] - W[p0][k]; v[k] = W[p2][k] - W[p0][k]; }
            nn[0] = u[1] * v[2] - u[2] * v[1]; nn[1] = u[2] * v[0] - u[0] * v[2]; nn[2] = u[0] * v[1] - u[1] * v[0];
            double len = sqrt(nn[0] * nn[0] + nn[1] * nn[1] + nn[2] * nn[2]) + 1e-12;
            double sh = 0.35 + 0.65 * fabs((nn[0] * 0.4 - nn[1] * 0.5 + nn[2] * 0.8) / len / 1.02);
            double h = ((Z[p0] + Z[p1] + Z[p2]) / 3 - s->zmin) / (s->zmax - s->zmin + 1e-12);
            uint8_t col[3]; pal(h, col); for (int k = 0; k < 3; k++) col[k] = (uint8_t)(col[k] * sh);
            fb_tri(f, Q[p0], Q[p1], Q[p2], col);
        }
    }
    box(f, c, 1.5, 1.05);
}
static double ez(void *u, double x, double y, double t) { double v[4] = {x, y, 0, t}; return ex_eval((const Node *)u, v); }
static void surf_step(void *u, double dt) { ((Surf *)u)->t += dt; }
static void surf_cb(void *u, Fb *f, const Cam *c) {
    Surf *s = u; surf_draw(s, f, c);
    snprintf(g_msg, sizeof g_msg, "z=f(x,y,t)  t=%.2f  z in [%.3g, %.3g]", s->t, s->zmin, s->zmax);
}
int view_surface(const Node *f) {
    Surf s = {ez, (void *)f, 0, 0, 3.0, 0, 0, 1, -1, 1, SN};
    return run_view(surf_step, surf_cb, &s);
}

/* ---------------- brain: a neural net learning, drawn as a living surface ---------------- */
typedef struct { Surf s; Net *net; double *X, *Y; int cnt; long epoch; double mse, lr; const char *name; } Brain;
static double bz(void *u, double x, double y, double t) { (void)t; double in[2] = {x, y}, o[1]; nn_forward(u, in, o); return o[0]; }
static void brain_step(void *u, double dt) {
    Brain *b = u; (void)dt;
    for (int i = 0; i < 60; i++) b->mse = nn_train(b->net, b->X, b->Y, b->cnt, b->lr);
    b->epoch += 60;
    snprintf(g_msg, sizeof g_msg, "dataset=%s  epoch=%ld  mse=%.5f  (the surface is the net's belief)", b->name, b->epoch, b->mse);
}
static void brain_draw(void *u, Fb *f, const Cam *c) {
    Brain *b = u; surf_draw(&b->s, f, c);
    for (int i = 0; i < b->cnt; i++) {
        double x = b->X[2 * i], y = b->X[2 * i + 1], l = b->Y[i];
        double wx = (x - b->s.cx) / b->s.R * 1.5, wy = (y - b->s.cy) / b->s.R * 1.5;
        double base[3] = {wx, wy, -1.0}, top[3] = {wx, wy, (l - b->s.zc) * b->s.zs};
        uint8_t col[3] = {l > .5 ? 255 : 255, l > .5 ? 70 : 255, l > .5 ? 200 : 255}; P3 p;
        seg(f, c, base, top, col);
        if (cam_proj(c, f, top[0], top[1], top[2], &p)) fb_dot(f, p, 2, col);
    }
}
int view_brain(Net **net, const char *ds) {
    static char last[16] = "";
    int ring = ds && !strcmp(ds, "ring");
    Brain b; memset(&b, 0, sizeof b);
    b.name = ring ? "ring" : "xor"; b.lr = ring ? 0.15 : 0.5;
    if (*net && strcmp(last, b.name)) { nn_free(*net); *net = 0; }
    if (!*net) { int sz1[3] = {2, 8, 1}, sz2[4] = {2, 12, 8, 1}; *net = ring ? nn_new(4, sz2, 7) : nn_new(3, sz1, 3); }
    snprintf(last, sizeof last, "%s", b.name);
    if (ring) {
        b.cnt = 64; b.X = malloc(sizeof(double) * 128); b.Y = malloc(sizeof(double) * 64); uint64_t r = 12345;
        for (int i = 0; i < 64; i++) {
            for (int k = 0; k < 2; k++) { r = r * 6364136223846793005ULL + 1442695040888963407ULL; b.X[2 * i + k] = -0.5 + 2.0 * (double)(r >> 40) / 16777216.0; }
            double dx = b.X[2 * i] - .5, dy = b.X[2 * i + 1] - .5; b.Y[i] = dx * dx + dy * dy < 0.16;
        }
    } else {
        static const double X[8] = {0, 0, 0, 1, 1, 0, 1, 1}, Y[4] = {0, 1, 1, 0};
        b.cnt = 4; b.X = malloc(sizeof X); b.Y = malloc(sizeof Y); memcpy(b.X, X, sizeof X); memcpy(b.Y, Y, sizeof Y);
    }
    b.net = *net; b.s = (Surf){bz, *net, 0.5, 0.5, 1.2, 0, 0, 1, 0, 1, SN};
    int r = run_view(brain_step, brain_draw, &b);
    free(b.X); free(b.Y); return r;
}

/* ---------------- chaotic attractors from user equations ---------------- */
#define TRN 2500
typedef struct { Node *f[3]; double s[2][3], init[3], h, t, sep; P3 tr[2][TRN]; int head[2], cnt[2]; } Ode;
static void ode_f(const double *s, double t, double *ds, void *u) {
    Ode *o = u; double v[4] = {s[0], s[1], s[2], t};
    for (int i = 0; i < 3; i++) ds[i] = ex_eval(o->f[i], v);
}
static void ode_reset(Ode *o) {
    for (int p = 0; p < 2; p++) { memcpy(o->s[p], o->init, sizeof o->init); o->head[p] = o->cnt[p] = 0; }
    o->s[1][0] += 1e-3; o->t = 0;
}
static void ode_step(void *u, double dt) {
    Ode *o = u; (void)dt;
    for (int k = 0; k < 8; k++) for (int p = 0; p < 2; p++) {
        ode_rk4(ode_f, o->s[p], 3, o->t, o->h, o);
        double *s = o->s[p];
        if (!isfinite(s[0] + s[1] + s[2]) || fabs(s[0]) + fabs(s[1]) + fabs(s[2]) > 1e7) { ode_reset(o); return; }
        P3 *t = &o->tr[p][o->head[p]]; t->x = s[0]; t->y = s[1]; t->z = s[2];
        o->head[p] = (o->head[p] + 1) % TRN; if (o->cnt[p] < TRN) o->cnt[p]++;
    }
    o->t += 8 * o->h;
    double d0 = o->s[0][0] - o->s[1][0], d1 = o->s[0][1] - o->s[1][1], d2 = o->s[0][2] - o->s[1][2];
    o->sep = sqrt(d0 * d0 + d1 * d1 + d2 * d2);
    snprintf(g_msg, sizeof g_msg, "t=%.1f  two trajectories 0.001 apart, separation=%.4g (chaos)", o->t, o->sep);
}
static void ode_draw(void *u, Fb *f, const Cam *c) {
    Ode *o = u; double lo[3] = {1e300, 1e300, 1e300}, hi[3] = {-1e300, -1e300, -1e300};
    for (int p = 0; p < 2; p++) for (int i = 0; i < o->cnt[p]; i++) {
        double v[3] = {o->tr[p][i].x, o->tr[p][i].y, o->tr[p][i].z};
        for (int k = 0; k < 3; k++) { lo[k] = fmin(lo[k], v[k]); hi[k] = fmax(hi[k], v[k]); }
    }
    if (o->cnt[0] < 2) return;
    double ctr[3], ext = 1e-9;
    for (int k = 0; k < 3; k++) { ctr[k] = (lo[k] + hi[k]) / 2; ext = fmax(ext, (hi[k] - lo[k]) / 2); }
    double sc = 1.5 / ext; static const uint8_t base[2][3] = {{80, 210, 255}, {255, 150, 60}};
    box(f, c, 1.5, 1.5);
    for (int p = 0; p < 2; p++) {
        int n = o->cnt[p], st = (o->head[p] - n + TRN) % TRN; double prev[3] = {0, 0, 0};
        for (int i = 0; i < n; i++) {
            P3 *q = &o->tr[p][(st + i) % TRN]; double cur[3] = {(q->x - ctr[0]) * sc, (q->y - ctr[1]) * sc, (q->z - ctr[2]) * sc};
            uint8_t col[3]; fade(base[p], (double)i / n, col);
            if (i) seg(f, c, prev, cur, col);
            memcpy(prev, cur, sizeof prev);
        }
        P3 hp; uint8_t w[3] = {255, 255, 255};
        if (cam_proj(c, f, prev[0], prev[1], prev[2], &hp)) fb_dot(f, hp, 1, w);
    }
}
int view_ode(const char *spec) {
    char buf[300]; const char *s = spec && *spec ? spec : "lorenz";
    Ode *o = calloc(1, sizeof *o); o->h = 0.01; o->init[0] = o->init[1] = o->init[2] = 1;
    if (!strcmp(s, "lorenz")) { s = "10*(y-x); x*(28-z)-y; x*y-8/3*z"; o->h = 0.005; }
    else if (!strcmp(s, "rossler")) { s = "-y-z; x+0.2*y; 0.2+z*(x-5.7)"; o->h = 0.02; }
    else if (!strcmp(s, "thomas")) { s = "sin(y)-0.208186*x; sin(z)-0.208186*y; sin(x)-0.208186*z"; o->h = 0.1; o->init[0] = 0.1; o->init[1] = o->init[2] = 0; }
    snprintf(buf, sizeof buf, "%s", s);
    char *part = buf; int n = 0;
    for (; n < 3 && part; n++) {
        char *e = strchr(part, ';'); if (e) *e++ = 0;
        const char *err; o->f[n] = ex_parse(part, &err);
        if (!o->f[n]) { printf("error in equation %d: %s\n", n + 1, err); break; }
        part = e;
    }
    int r = -1;
    if (g_ppm) g_steps *= 4;
    if (n == 3) { ode_reset(o); r = run_view(ode_step, ode_draw, o); }
    else puts("need three equations: dx/dt; dy/dt; dz/dt  (or lorenz | rossler | thomas)");
    for (int i = 0; i < 3; i++) ex_free(o->f[i]);
    free(o); return r;
}

/* ---------------- N-body gravity ---------------- */
#define NB 10
#define TRO 260
typedef struct { int n, head, cnt; double m[NB], s[6 * NB], e0, t, M; P3 tr[NB][TRO]; } Orb;
static void orb_f(const double *s, double t, double *ds, void *u) {
    Orb *o = u; (void)t;
    for (int i = 0; i < o->n; i++) {
        for (int k = 0; k < 3; k++) { ds[6 * i + k] = s[6 * i + 3 + k]; ds[6 * i + 3 + k] = 0; }
        for (int j = 0; j < o->n; j++) if (j != i) {
            double d[3], r2 = 0.0025;
            for (int k = 0; k < 3; k++) { d[k] = s[6 * j + k] - s[6 * i + k]; r2 += d[k] * d[k]; }
            double g = o->m[j] / (r2 * sqrt(r2));
            for (int k = 0; k < 3; k++) ds[6 * i + 3 + k] += d[k] * g;
        }
    }
}
static double orb_energy(const Orb *o) {
    double e = 0;
    for (int i = 0; i < o->n; i++) {
        const double *a = o->s + 6 * i; e += 0.5 * o->m[i] * (a[3] * a[3] + a[4] * a[4] + a[5] * a[5]);
        for (int j = i + 1; j < o->n; j++) {
            const double *b = o->s + 6 * j; double r2 = 0.0025;
            for (int k = 0; k < 3; k++) r2 += (a[k] - b[k]) * (a[k] - b[k]);
            e -= o->m[i] * o->m[j] / sqrt(r2);
        }
    }
    return e;
}
static void orb_step(void *u, double dt) {
    Orb *o = u; (void)dt;
    for (int k = 0; k < 16; k++) ode_rk4(orb_f, o->s, 6 * o->n, o->t, 0.003, o), o->t += 0.003;
    for (int i = 0; i < o->n; i++) { P3 *q = &o->tr[i][o->head]; q->x = o->s[6*i]; q->y = o->s[6*i+1]; q->z = o->s[6*i+2]; }
    o->head = (o->head + 1) % TRO; if (o->cnt < TRO) o->cnt++;
    snprintf(g_msg, sizeof g_msg, "bodies=%d  t=%.1f  relative energy drift=%.2e (RK4 conserves it)", o->n, o->t, fabs((orb_energy(o) - o->e0) / o->e0));
}
static void orb_draw(void *u, Fb *f, const Cam *c) {
    Orb *o = u; double sc = 1.5 / (1.0 + 0.7 * (o->n - 2) + 0.6); box(f, c, 1.5, 1.5);
    for (int i = 0; i < o->n; i++) {
        uint8_t base[3], col[3], hue[3]; pal((double)i / o->n, hue); memcpy(base, hue, 3); if (!i) { base[0] = 255; base[1] = 220; base[2] = 70; }
        int st = (o->head - o->cnt + TRO) % TRO; double prev[3] = {0, 0, 0};
        for (int k = 0; k < o->cnt; k++) {
            P3 *q = &o->tr[i][(st + k) % TRO]; double cur[3] = {q->x * sc, q->y * sc, q->z * sc};
            fade(base, (double)k / o->cnt, col); if (k) seg(f, c, prev, cur, col); memcpy(prev, cur, sizeof prev);
        }
        P3 p; if (cam_proj(c, f, o->s[6*i] * sc, o->s[6*i+1] * sc, o->s[6*i+2] * sc, &p)) fb_dot(f, p, i ? 1 : 2, base);
    }
}
int view_orbit(int n) {
    if (n < 2) n = 2;
    if (n > NB) n = NB;
    Orb *o = calloc(1, sizeof *o); o->n = n; o->m[0] = 10; double mom[3] = {0, 0, 0};
    for (int i = 1; i < n; i++) {
        double r = 1 + 0.7 * (i - 1), th = i * 2.4, inc = 0.35 * sin(i * 1.7), v = sqrt(10.0 / r); o->m[i] = 0.05 + 0.02 * i;
        double *s = o->s + 6 * i;
        s[0] = r * cos(th); s[1] = r * sin(th) * cos(inc); s[2] = r * sin(th) * sin(inc);
        s[3] = -v * sin(th); s[4] = v * cos(th) * cos(inc); s[5] = v * cos(th) * sin(inc);
        for (int k = 0; k < 3; k++) mom[k] += o->m[i] * s[3 + k];
    }
    for (int k = 0; k < 3; k++) o->s[3 + k] = -mom[k] / o->m[0];   /* zero total momentum */
    o->e0 = orb_energy(o);
    int r = run_view(orb_step, orb_draw, o); free(o); return r;
}

/* ---------------- truth hypercube for a logic formula ---------------- */
typedef struct { unsigned char tv[64]; int nv; char names[28]; } Cube;
static void cube_step(void *u, double dt) {
    Cube *q = u; (void)dt;
    snprintf(g_msg, sizeof g_msg, "vars '%s': first 3 = x,y,z axes, next 3 shift the copy | green=true red=false", q->names);
}
static void cube_pos(const Cube *q, int m, double *p) {
    for (int a = 0; a < 3; a++) p[a] = ((m >> a) & 1) + ((m >> (a + 3)) & 1) * 0.45 - (q->nv > 3 + a ? 0.725 : 0.5);
}
static void cube_draw(void *u, Fb *f, const Cam *c) {
    Cube *q = u; int tot = 1 << q->nv; double sc = 1.2;
    for (int m = 0; m < tot; m++) for (int k = 0; k < q->nv; k++) {
        int m2 = m ^ (1 << k); if (m > m2) continue;
        double a[3], b[3]; cube_pos(q, m, a); cube_pos(q, m2, b);
        for (int i = 0; i < 3; i++) { a[i] *= sc; b[i] *= sc; }
        uint8_t col[3] = {k < 3 ? 100 : 170, k < 3 ? 125 : 105, 175}; seg(f, c, a, b, col);
    }
    for (int m = 0; m < tot; m++) {
        double a[3]; P3 p; cube_pos(q, m, a);
        uint8_t col[3] = {q->tv[m] ? 60 : 235, q->tv[m] ? 225 : 60, q->tv[m] ? 100 : 70};
        if (cam_proj(c, f, a[0] * sc, a[1] * sc, a[2] * sc, &p)) fb_dot(f, p, q->tv[m] ? 3 : 2, col);
    }
}
int view_cube(const unsigned char *tv, int nv, const char *names) {
    Cube q; memset(&q, 0, sizeof q); q.nv = nv; memcpy(q.tv, tv, (size_t)1 << nv); snprintf(q.names, sizeof q.names, "%s", names);
    g_spin0 = 1; int r = run_view(cube_step, cube_draw, &q); g_spin0 = 0; return r;
}
