/* SPDX-License-Identifier: AGPL-3.0-or-later
 * NLE - Decentralized Neural-Logic Engine. Copyright (C) 2026 NLE contributors.
 * Pure C99, zero dependencies, zero network, zero central AI. */
#ifndef NLE_H
#define NLE_H
#include <stdint.h>
#include <stddef.h>
#include <stdio.h>

/* ---- expr.c : symbolic math over variables x y z t (indices 0..3) ---- */
typedef struct Node Node;
Node  *ex_parse(const char *src, const char **err);
double ex_eval(const Node *n, const double *v);
Node  *ex_diff(const Node *n, int var);
Node  *ex_simp(Node *n);                 /* consumes n, returns simplified */
void   ex_print(const Node *n, char *out, size_t cap);
void   ex_free(Node *n);

/* ---- solve.c : numerical solvers ---- */
typedef void (*ode_fn)(const double *s, double t, double *ds, void *u);
int    nl_newton(const Node *f, int var, double x0, double *root);
double nl_integrate(const Node *f, int var, double a, double b);
void   ode_rk4(ode_fn f, double *s, int n, double t, double h, void *u);

/* ---- nn.c : tiny multilayer perceptron with backprop ---- */
#define NN_MAX 32
typedef struct { int nl, sz[6], np; double *p; uint64_t rng; } Net;
Net   *nn_new(int nl, const int *sz, uint64_t seed);
void   nn_forward(const Net *n, const double *in, double *out);
double nn_train(Net *n, const double *X, const double *Y, int cnt, double lr);
void   nn_free(Net *n);

/* ---- crypto.c : SHA-256, HMAC, ChaCha20, sealed "capsules" ---- */
void     sha256(const void *d, size_t n, uint8_t out[32]);
void     hmac256(const uint8_t *key, size_t kl, const uint8_t *m, size_t n, uint8_t out[32]);
void     chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t ctr, uint8_t *buf, size_t n);
uint8_t *cap_seal(const char *pass, const uint8_t *d, size_t n, size_t *outn);
uint8_t *cap_open(const char *pass, const uint8_t *c, size_t n, size_t *outn);
void     cap_id(const uint8_t *c, size_t n, char hex[65]);

/* ---- logic.c : propositional logic (a-z, T, F, ! & | -> <->) ---- */
int lg_analyze(const char *line, FILE *o, unsigned char *tv, int *nv, char *names);

/* ---- gfx.c : software 3D rasterizer + terminal ---- */
typedef struct { double x, y, z; } P3;
typedef struct { int w, h; uint8_t *rgb; float *z; } Fb;
typedef struct { double yaw, pitch, dist; } Cam;
enum { K_UP = 1001, K_DOWN, K_RIGHT, K_LEFT };
Fb  *fb_new(int w, int h);
void fb_free(Fb *f);
void fb_clear(Fb *f);
void fb_px(Fb *f, int x, int y, float z, const uint8_t *c);
void fb_dot(Fb *f, P3 p, int r, const uint8_t *c);
void fb_line(Fb *f, P3 a, P3 b, const uint8_t *c);
void fb_tri(Fb *f, P3 a, P3 b, P3 c, const uint8_t *col);
int  cam_proj(const Cam *c, const Fb *f, double x, double y, double z, P3 *o);
void fb_present(const Fb *f);
int  fb_ppm(const Fb *f, const char *path);
void term_raw(int on);
void term_size(int *cols, int *rows);
int  term_key(void);

/* ---- view.c : interactive 3D worlds ---- */
extern const char *g_ppm;   /* offline render target (NULL = interactive) */
extern int g_steps;         /* simulation steps before offline snapshot   */
int view_surface(const Node *f);
int view_ode(const char *spec);
int view_orbit(int n);
int view_brain(Net **net, const char *dataset);
int view_cube(const unsigned char *tv, int nv, const char *names);
#endif
