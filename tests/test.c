/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * Self-tests with published vectors (SHA-256 FIPS 180, ChaCha20 RFC 8439). */
#include "../src/nle.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

static int fails, total;
#define CHECK(c, name) do { total++; if (!(c)) { fails++; printf("FAIL: %s\n", name); } else printf("ok   : %s\n", name); } while (0)
static double ev(const char *s, double x, double y) { Node *n = ex_parse(s, 0); double v[4] = {x, y, 0, 0}, r = ex_eval(n, v); ex_free(n); return r; }
static void rhs(const double *s, double t, double *d, void *u) { (void)t; (void)u; d[0] = s[1]; d[1] = -s[0]; }

int main(void) {
    char hex[65]; uint8_t h[32];
    sha256("abc", 3, h); for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
    CHECK(!strcmp(hex, "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"), "sha256(abc)");
    uint8_t big[1000]; memset(big, 'a', sizeof big); sha256(big, 1000, h); sprintf(hex, "%02x%02x%02x%02x", h[0], h[1], h[2], h[3]);
    CHECK(!strcmp(hex, "41edece4"), "sha256(1000 x 'a')");
    uint8_t key[32], nonce[12] = {0, 0, 0, 0x09, 0, 0, 0, 0x4a, 0, 0, 0, 0}, z[64] = {0};
    for (int i = 0; i < 32; i++) key[i] = (uint8_t)i;
    chacha20(key, nonce, 1, z, 64);
    CHECK(z[0] == 0x10 && z[1] == 0xf1 && z[2] == 0xe7 && z[3] == 0xe4 && z[4] == 0xd1 && z[7] == 0x15 && z[15] == 0xc4, "chacha20 RFC 8439 keystream");
    size_t n, m; const char *msg = "capsule payload: x^2+y^2";
    uint8_t *c = cap_seal("pw", (const uint8_t *)msg, strlen(msg), &n), *d = cap_open("pw", c, n, &m);
    CHECK(d && m == strlen(msg) && !memcmp(d, msg, m), "capsule roundtrip");
    CHECK(!cap_open("wrong", c, n, &m), "capsule rejects wrong passphrase");
    c[40] ^= 1; CHECK(!cap_open("pw", c, n, &m), "capsule detects tampering");
    free(c); free(d);

    CHECK(fabs(ev("2+3*4^2/8 - -1", 0, 0) - 9) < 1e-12, "parser precedence");
    CHECK(fabs(ev("-x^2", 3, 0) + 9) < 1e-12, "unary minus vs power");
    CHECK(fabs(ev("2x+sin(pi/2)", 4, 0) - 9) < 1e-12, "implicit multiplication");
    CHECK(!ex_parse("2+*3", 0) && !ex_parse("foo(1)", 0) && !ex_parse("(1+2", 0), "syntax errors rejected");
    const char *fs[] = {"x^3*sin(x)+exp(-x)/x", "sqrt(x^2+1)*atan(x)", "tanh(x*y)+x^y", "log(x)/cos(x)^2"};
    int okd = 1;
    for (int i = 0; i < 4; i++) {
        Node *f = ex_parse(fs[i], 0), *g = ex_simp(ex_diff(f, 0)); double v[4] = {1.3, 0.7, 0, 0}, w[4] = {1.3 + 1e-6, 0.7, 0, 0}, u[4] = {1.3 - 1e-6, 0.7, 0, 0};
        double num = (ex_eval(f, w) - ex_eval(f, u)) / 2e-6, sym = ex_eval(g, v);
        if (fabs(num - sym) > 1e-5 * (1 + fabs(sym))) okd = 0;
        ex_free(f); ex_free(g);
    }
    CHECK(okd, "symbolic derivative matches finite differences");
    Node *f = ex_parse("x^2-2", 0); double r; int ok = nl_newton(f, 0, 1, &r);
    CHECK(ok && fabs(r - sqrt(2.0)) < 1e-12, "newton finds sqrt(2)"); ex_free(f);
    f = ex_parse("sin(x)", 0); CHECK(fabs(nl_integrate(f, 0, 0, 3.14159265358979323846) - 2) < 1e-9, "integral of sin on [0,pi] = 2"); ex_free(f);
    double s[2] = {1, 0}; for (int i = 0; i < 6283; i++) ode_rk4(rhs, s, 2, 0, 0.001, 0);
    CHECK(fabs(s[0] - cos(6.283)) < 1e-9, "rk4 harmonic oscillator, one period");

    int sz[3] = {2, 6, 1}; Net *net = nn_new(3, sz, 3);
    const double X[8] = {0, 0, 0, 1, 1, 0, 1, 1}, Y[4] = {0, 1, 1, 0}; double mse = 1;
    for (int e = 0; e < 8000 && mse > 1e-4; e++) mse = nn_train(net, X, Y, 4, 0.5);
    int good = 1; for (int i = 0; i < 4; i++) { double o; nn_forward(net, X + 2 * i, &o); if (fabs(o - Y[i]) > 0.2) good = 0; }
    CHECK(good, "neural net learns XOR by backprop"); nn_free(net);

    FILE *nul = fopen("/dev/null", "w"); unsigned char tv[64]; int nv; char nm[28];
    CHECK(lg_analyze("(a->b)&a |= b", nul, 0, 0, 0) == 0, "modus ponens parses");
    FILE *buf = tmpfile(); char out[512];
    lg_analyze("(a->b)&a |= b", buf, 0, 0, 0); rewind(buf); out[fread(out, 1, 511, buf)] = 0; fclose(buf);
    CHECK(strstr(out, "VALID"), "modus ponens is valid");
    buf = tmpfile(); lg_analyze("(a->b)&b |= a", buf, 0, 0, 0); rewind(buf); out[fread(out, 1, 511, buf)] = 0; fclose(buf);
    CHECK(strstr(out, "NOT entailed"), "affirming the consequent is a fallacy");
    buf = tmpfile(); lg_analyze("a|!a", buf, 0, 0, 0); rewind(buf); out[fread(out, 1, 511, buf)] = 0; fclose(buf);
    CHECK(strstr(out, "TAUTOLOGY"), "excluded middle is a tautology");
    buf = tmpfile(); lg_analyze("a&!a", buf, 0, 0, 0); rewind(buf); out[fread(out, 1, 511, buf)] = 0; fclose(buf);
    CHECK(strstr(out, "CONTRADICTION"), "a and not a is a contradiction");
    lg_analyze("a&b|c", nul, tv, &nv, nm);
    CHECK(nv == 3 && tv[7] == 1 && tv[0] == 0 && tv[4] == 1 && tv[3] == 1 && tv[1] == 0, "truth vector of a&b|c");
    fclose(nul);

    Fb *fb = fb_new(64, 48); fb_clear(fb); uint8_t col[3] = {255, 0, 0};
    P3 a = {5, 5, 1}, b = {50, 8, 1}, cc = {20, 40, 1}; fb_tri(fb, a, b, cc, col);
    CHECK(fb->rgb[3 * (15 * 64 + 20)] == 255, "rasterizer fills triangle"); fb_free(fb);
    printf("\n%d/%d tests passed\n", total - fails, total);
    return fails != 0;
}
