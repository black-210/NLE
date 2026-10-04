/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * Propositional logic: truth tables, satisfiability, entailment.
 * Syntax: variables a-z, constants T F 1 0, operators ! & | -> <-> and ( ).
 * Several formulas separated by ';' form a knowledge base; "p ; q |= r" tests entailment. */
#include "nle.h"
#include <string.h>

typedef struct { int op, a, b; } LN;
static LN pool[256];
static int np, pos[26];
static unsigned used;
static const char *P, *perr;
static int roots[16], nr, concl, entail;

static int nd(int op, int a, int b) {
    if (np >= 256) { perr = "formula too large"; return -1; }
    pool[np].op = op; pool[np].a = a; pool[np].b = b; return np++;
}
static void ws(void) { while (*P == ' ' || *P == '\t') P++; }
static int piff(void);
static int atom(void) {
    ws(); int r;
    if (*P == '(') { P++; r = piff(); ws(); if (r < 0) return -1; if (*P != ')') { perr = "expected ')'"; return -1; } P++; return r; }
    if (*P >= 'a' && *P <= 'z') { int c = *P++ - 'a'; used |= 1u << c; return nd('v', c, 0); }
    if (*P == 'T' || *P == '1') { P++; return nd('T', 0, 0); }
    if (*P == 'F' || *P == '0') { P++; return nd('F', 0, 0); }
    perr = "unexpected token"; return -1;
}
static int pnot(void) {
    ws();
    if (*P == '!' || *P == '~') { P++; int a = pnot(); return a < 0 ? -1 : nd('!', a, 0); }
    return atom();
}
static int pand(void) {
    int a = pnot();
    while (a >= 0) { ws(); if (*P != '&') break; P++; int b = pnot(); if (b < 0) return -1; a = nd('&', a, b); }
    return a;
}
static int por(void) {
    int a = pand();
    while (a >= 0) { ws(); if (*P != '|') break; P++; int b = pand(); if (b < 0) return -1; a = nd('|', a, b); }
    return a;
}
static int pimp(void) {
    int a = por(); if (a < 0) return -1; ws();
    if (P[0] == '-' && P[1] == '>') { P += 2; int b = pimp(); return b < 0 ? -1 : nd('>', a, b); }
    return a;
}
static int piff(void) {
    int a = pimp();
    while (a >= 0) {
        ws(); if (!(P[0] == '<' && P[1] == '-' && P[2] == '>')) break;
        P += 3; int b = pimp(); if (b < 0) return -1; a = nd('=', a, b);
    }
    return a;
}
static int ev(int i, unsigned m) {
    LN *n = &pool[i];
    switch (n->op) {
    case 'v': return (m >> pos[n->a]) & 1;
    case 'T': return 1;
    case 'F': return 0;
    case '!': return !ev(n->a, m);
    case '&': return ev(n->a, m) && ev(n->b, m);
    case '|': return ev(n->a, m) || ev(n->b, m);
    case '>': return !ev(n->a, m) || ev(n->b, m);
    case '=': return ev(n->a, m) == ev(n->b, m);
    }
    return 0;
}
static int val(unsigned m) {
    int kb = 1;
    for (int i = 0; i < nr; i++) kb = kb && ev(roots[i], m);
    return entail ? (!kb || ev(concl, m)) : kb;
}
static int parse_one(char *s, int *root) {
    P = s; *root = piff(); ws();
    if (*root >= 0 && *P) { perr = "trailing input"; *root = -1; }
    return *root >= 0 ? 0 : -1;
}

/* Returns 0 on success. tv (64 bytes) / nv / names optionally receive the truth vector. */
int lg_analyze(const char *line, FILE *o, unsigned char *tv, int *nv_out, char *names) {
    char buf[512]; strncpy(buf, line, sizeof buf - 1); buf[sizeof buf - 1] = 0;
    nr = 0; concl = -1; entail = 0;
    np = 0; used = 0; perr = 0;
    char *ent = strstr(buf, "|="), *conc = 0;
    if (ent) { *ent = 0; conc = ent + 2; entail = 1; }
    for (char *s = buf, *e; s; s = e) {
        e = strchr(s, ';'); if (e) *e++ = 0;
        if (nr >= 15) { fprintf(o, "error: too many formulas\n"); return -1; }
        if (parse_one(s, &roots[nr++])) { fprintf(o, "error: %s\n", perr); return -1; }
    }
    if (entail && parse_one(conc, &concl)) { fprintf(o, "error: %s\n", perr); return -1; }
    int nv = 0; char nm[27];
    for (int c = 0; c < 26; c++) if (used >> c & 1) { pos[c] = nv; nm[nv++] = (char)('a' + c); }
    nm[nv] = 0;
    if (nv > 22) { fprintf(o, "error: too many variables (max 22)\n"); return -1; }
    unsigned long total = 1ul << nv, models = 0, first = 0, cex = 0; int have = 0, hcex = 0;
    for (unsigned long m = 0; m < total; m++) {
        int v = val((unsigned)m);
        if (v) { if (!have) { first = m; have = 1; } models++; }
        else if (!hcex) { cex = m; hcex = 1; }
        if (tv && nv <= 6) tv[m] = (unsigned char)v;
    }
    if (nv_out) *nv_out = nv;
    if (names) strcpy(names, nm);
    if (nv <= 4) {   /* print the truth table */
        fprintf(o, "  %s | result\n", nm);
        for (unsigned long m = 0; m < total; m++) {
            fputs("  ", o);
            for (int i = 0; i < nv; i++) fputc('0' + (int)((m >> i) & 1), o);
            fprintf(o, " | %d\n", val((unsigned)m));
        }
    }
    fprintf(o, "variables: %s   models: %lu / %lu\n", nv ? nm : "(none)", models, total);
    if (entail) {
        if (models == total) fprintf(o, "VALID: the premises entail the conclusion.\n");
        else {
            fprintf(o, "NOT entailed. Counterexample:");
            for (int i = 0; i < nv; i++) fprintf(o, " %c=%d", nm[i], (int)((cex >> i) & 1));
            fputc('\n', o);
        }
    } else if (models == total) fprintf(o, "TAUTOLOGY (always true)\n");
    else if (models == 0) fprintf(o, "CONTRADICTION (unsatisfiable)\n");
    else {
        fprintf(o, "SATISFIABLE. Example:");
        for (int i = 0; i < nv; i++) fprintf(o, " %c=%d", nm[i], (int)((first >> i) & 1));
        fputc('\n', o);
    }
    return 0;
}
