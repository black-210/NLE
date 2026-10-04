/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors */
#include "nle.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

#define PI_ 3.14159265358979323846
struct Node { int k, op; double v; Node *a, *b; };
enum { N_NUM, N_VAR, N_ADD, N_SUB, N_MUL, N_DIV, N_POW, N_NEG, N_FN };
enum { F_SIN, F_COS, F_TAN, F_EXP, F_LOG, F_SQRT, F_ABS, F_TANH, F_ATAN, F_N };
static const char *FN[F_N] = {"sin","cos","tan","exp","log","sqrt","abs","tanh","atan"};

static Node *mk(int k, int op, double v, Node *a, Node *b) {
    Node *n = calloc(1, sizeof *n);
    n->k = k; n->op = op; n->v = v; n->a = a; n->b = b; return n;
}
static Node *num(double v) { return mk(N_NUM, 0, v, 0, 0); }
static Node *bin(int k, Node *a, Node *b) { return mk(k, 0, 0, a, b); }
static Node *fn(int f, Node *a) { return mk(N_FN, f, 0, a, 0); }
static Node *cl(const Node *n) {
    if (!n) return 0;
    Node *r = mk(n->k, n->op, n->v, cl(n->a), cl(n->b)); return r;
}
void ex_free(Node *n) { if (n) { ex_free(n->a); ex_free(n->b); free(n); } }

/* ---------------- parser (recursive descent) ---------------- */
static const char *P, *perr;
static void ws(void) { while (isspace((unsigned char)*P)) P++; }
static Node *pe(void), *un(void);
static Node *fail(const char *m, Node *a, Node *b) { if (!perr) perr = m; ex_free(a); ex_free(b); return 0; }

static Node *atom(void) {
    ws();
    if (isdigit((unsigned char)*P) || *P == '.') { char *e; double v = strtod(P, &e); P = e; return num(v); }
    if (isalpha((unsigned char)*P)) {
        char id[16]; int n = 0;
        while (isalnum((unsigned char)*P) && n < 15) id[n++] = *P++;
        id[n] = 0; ws();
        if (*P == '(') {
            for (int f = 0; f < F_N; f++) if (!strcmp(id, FN[f])) {
                P++; Node *a = pe(); ws();
                if (!a) return 0;
                if (*P != ')') return fail("expected ')'", a, 0);
                P++; return fn(f, a);
            }
            return fail("unknown function", 0, 0);
        }
        if (!strcmp(id, "pi")) return num(PI_);
        if (!strcmp(id, "e")) return num(2.71828182845904523536);
        if (n == 1) { const char *q = strchr("xyzt", id[0]); if (q) return mk(N_VAR, (int)(q - "xyzt"), 0, 0, 0); }
        return fail("unknown symbol", 0, 0);
    }
    if (*P == '(') {
        P++; Node *a = pe(); ws();
        if (!a) return 0;
        if (*P != ')') return fail("expected ')'", a, 0);
        P++; return a;
    }
    return fail("unexpected token", 0, 0);
}
static Node *pw(void) {
    Node *a = atom(); if (!a) return 0; ws();
    if (*P == '^') { P++; Node *b = un(); if (!b) return fail(0, a, 0); return bin(N_POW, a, b); }
    return a;
}
static Node *un(void) {
    ws();
    if (*P == '-') { P++; Node *a = un(); return a ? mk(N_NEG, 0, 0, a, 0) : 0; }
    if (*P == '+') { P++; return un(); }
    return pw();
}
static Node *term(void) {
    Node *a = un();
    while (a) {
        ws(); int k;
        if (*P == '*') { P++; k = N_MUL; }
        else if (*P == '/') { P++; k = N_DIV; }
        else if (isalpha((unsigned char)*P) || *P == '(') k = N_MUL;   /* implicit: 2x, x(y+1) */
        else break;
        Node *b = un(); if (!b) return fail(0, a, 0);
        a = bin(k, a, b);
    }
    return a;
}
static Node *pe(void) {
    Node *a = term();
    while (a) {
        ws(); int k;
        if (*P == '+') k = N_ADD; else if (*P == '-') k = N_SUB; else break;
        P++; Node *b = term(); if (!b) return fail(0, a, 0);
        a = bin(k, a, b);
    }
    return a;
}
Node *ex_parse(const char *src, const char **err) {
    P = src; perr = 0;
    Node *r = pe(); ws();
    if (r && *P) r = fail("trailing input", r, 0);
    if (err) *err = r ? 0 : (perr ? perr : "syntax error");
    return r;
}

/* ---------------- evaluation ---------------- */
double ex_eval(const Node *n, const double *v) {
    switch (n->k) {
    case N_NUM: return n->v;
    case N_VAR: return v[n->op];
    case N_ADD: return ex_eval(n->a, v) + ex_eval(n->b, v);
    case N_SUB: return ex_eval(n->a, v) - ex_eval(n->b, v);
    case N_MUL: return ex_eval(n->a, v) * ex_eval(n->b, v);
    case N_DIV: return ex_eval(n->a, v) / ex_eval(n->b, v);
    case N_POW: return pow(ex_eval(n->a, v), ex_eval(n->b, v));
    case N_NEG: return -ex_eval(n->a, v);
    case N_FN: {
        double a = ex_eval(n->a, v);
        switch (n->op) {
        case F_SIN: return sin(a);   case F_COS: return cos(a);
        case F_TAN: return tan(a);   case F_EXP: return exp(a);
        case F_LOG: return log(a);   case F_SQRT: return sqrt(a);
        case F_ABS: return fabs(a);  case F_TANH: return tanh(a);
        case F_ATAN: return atan(a);
        } }
    }
    return 0;
}

/* ---------------- symbolic differentiation ---------------- */
Node *ex_diff(const Node *n, int var) {
    const Node *a = n->a, *b = n->b;
    switch (n->k) {
    case N_NUM: return num(0);
    case N_VAR: return num(n->op == var);
    case N_ADD: return bin(N_ADD, ex_diff(a, var), ex_diff(b, var));
    case N_SUB: return bin(N_SUB, ex_diff(a, var), ex_diff(b, var));
    case N_NEG: return mk(N_NEG, 0, 0, ex_diff(a, var), 0);
    case N_MUL: return bin(N_ADD, bin(N_MUL, ex_diff(a, var), cl(b)), bin(N_MUL, cl(a), ex_diff(b, var)));
    case N_DIV: return bin(N_DIV, bin(N_SUB, bin(N_MUL, ex_diff(a, var), cl(b)), bin(N_MUL, cl(a), ex_diff(b, var))),
                           bin(N_POW, cl(b), num(2)));
    case N_POW:
        if (b->k == N_NUM)   /* power rule */
            return bin(N_MUL, bin(N_MUL, num(b->v), bin(N_POW, cl(a), num(b->v - 1))), ex_diff(a, var));
        return bin(N_MUL, cl(n), bin(N_ADD, bin(N_MUL, ex_diff(b, var), fn(F_LOG, cl(a))),
                   bin(N_DIV, bin(N_MUL, cl(b), ex_diff(a, var)), cl(a))));
    case N_FN: {
        Node *da = ex_diff(a, var), *r = 0; Node *x = cl(a);
        switch (n->op) {
        case F_SIN:  r = fn(F_COS, x); break;
        case F_COS:  r = mk(N_NEG, 0, 0, fn(F_SIN, x), 0); break;
        case F_TAN:  r = bin(N_DIV, num(1), bin(N_POW, fn(F_COS, x), num(2))); break;
        case F_EXP:  r = fn(F_EXP, x); break;
        case F_LOG:  r = bin(N_DIV, num(1), x); break;
        case F_SQRT: r = bin(N_DIV, num(1), bin(N_MUL, num(2), fn(F_SQRT, x))); break;
        case F_ABS:  r = bin(N_DIV, x, fn(F_ABS, cl(a))); break;
        case F_TANH: r = bin(N_SUB, num(1), bin(N_POW, fn(F_TANH, x), num(2))); break;
        case F_ATAN: r = bin(N_DIV, num(1), bin(N_ADD, num(1), bin(N_POW, x, num(2)))); break;
        }
        return bin(N_MUL, r, da); }
    }
    return num(0);
}

/* ---------------- simplification ---------------- */
static int isn(const Node *n, double v) { return n->k == N_NUM && n->v == v; }
static Node *pick(Node *n, Node *keep, Node *drop) { ex_free(drop); free(n); return keep; }
static Node *fold(Node *n) { double z[4] = {0}; Node *r = num(ex_eval(n, z)); ex_free(n); return r; }
Node *ex_simp(Node *n) {
    if (!n) return n;
    n->a = ex_simp(n->a); n->b = ex_simp(n->b);
    Node *a = n->a, *b = n->b;
    switch (n->k) {
    case N_ADD: case N_SUB: case N_MUL: case N_DIV: case N_POW:
        if (a->k == N_NUM && b->k == N_NUM) return fold(n);
    }
    switch (n->k) {
    case N_ADD: if (isn(a, 0)) return pick(n, b, a); if (isn(b, 0)) return pick(n, a, b); break;
    case N_SUB: if (isn(b, 0)) return pick(n, a, b); break;
    case N_MUL:
        if (isn(a, 0) || isn(b, 0)) { ex_free(n); return num(0); }
        if (isn(a, 1)) return pick(n, b, a);
        if (isn(b, 1)) return pick(n, a, b);
        break;
    case N_DIV: if (isn(a, 0)) { ex_free(n); return num(0); } if (isn(b, 1)) return pick(n, a, b); break;
    case N_POW: if (isn(b, 0)) { ex_free(n); return num(1); } if (isn(b, 1)) return pick(n, a, b); break;
    case N_NEG:
        if (a->k == N_NUM) return fold(n);
        if (a->k == N_NEG) { Node *r = a->a; a->a = 0; ex_free(a); free(n); return r; }
        break;
    case N_FN: if (a->k == N_NUM) return fold(n); break;
    }
    return n;
}

/* ---------------- printing ---------------- */
typedef struct { char *s; size_t n, c; } Buf;
static void bp(Buf *b, const char *f, ...) {
    if (b->n + 1 >= b->c) return;
    va_list ap; va_start(ap, f);
    int w = vsnprintf(b->s + b->n, b->c - b->n, f, ap); va_end(ap);
    if (w > 0) b->n += (size_t)w;
    if (b->n >= b->c) b->n = b->c - 1;
}
static int prec(const Node *n) {
    switch (n->k) {
    case N_ADD: case N_SUB: return 1;
    case N_MUL: case N_DIV: return 2;
    case N_NEG: return 3;
    case N_POW: return 4;
    case N_NUM: return n->v < 0 ? 3 : 5;
    }
    return 5;
}
static void prn(const Node *n, Buf *b, int pp) {
    int par = prec(n) < pp; if (par) bp(b, "(");
    switch (n->k) {
    case N_NUM: bp(b, "%.10g", n->v); break;
    case N_VAR: bp(b, "%c", "xyzt"[n->op]); break;
    case N_ADD: prn(n->a, b, 1); bp(b, " + "); prn(n->b, b, 1); break;
    case N_SUB: prn(n->a, b, 1); bp(b, " - "); prn(n->b, b, 2); break;
    case N_MUL: prn(n->a, b, 2); bp(b, " * "); prn(n->b, b, 2); break;
    case N_DIV: prn(n->a, b, 2); bp(b, " / "); prn(n->b, b, 3); break;
    case N_POW: prn(n->a, b, 5); bp(b, "^"); prn(n->b, b, 4); break;
    case N_NEG: bp(b, "-"); prn(n->a, b, 4); break;
    case N_FN: bp(b, "%s(", FN[n->op]); prn(n->a, b, 0); bp(b, ")"); break;
    }
    if (par) bp(b, ")");
}
void ex_print(const Node *n, char *out, size_t cap) {
    Buf b = {out, 0, cap}; if (cap) out[0] = 0;
    prn(n, &b, 0); if (cap) out[b.n < cap ? b.n : cap - 1] = 0;
}
