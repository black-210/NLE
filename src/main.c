/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * NLE - Decentralized Neural-Logic Engine. Offline, serverless, AI-free reasoning in 3D. */
#include "nle.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <math.h>
#include <unistd.h>

#define SOURCE_URL "https://github.com/YOUR-NAME/nle"   /* AGPL-3.0 s.13: point users at the source */
static Net *g_net;
static char g_expr[256];
static double g_var[4];

static const char *NOTICE =
"NLE  Copyright (C) 2026 NLE contributors\n"
"This program comes with ABSOLUTELY NO WARRANTY. It is free software under the\n"
"GNU Affero General Public License v3 or later; see the LICENSE file.\n"
"Source code: " SOURCE_URL "\n";

static const char *HELP =
"MATH\n"
"  eval <expr>            evaluate (vars x y z t; use `set x 2`)     eval sin(x)^2+cos(x)^2\n"
"  diff <expr>            exact symbolic partial derivatives         diff x^2*sin(y)\n"
"  root <expr> <x0>       Newton root in x                           root x^2-2 1\n"
"  int <expr> <a> <b>     definite integral in x (Simpson)           int sin(x) 0 pi\n"
"LOGIC\n"
"  logic <formula>        truth table / SAT / entailment             logic (a->b)&a |= b\n"
"  cube <formula>         truth hypercube in 3D                      cube (a&b)|!c\n"
"3D WORLDS (arrows rotate, +/- zoom, space pause, p snapshot, q quit)\n"
"  plot [expr]            surface z=f(x,y,t)                         plot sin(x)*cos(y+t)\n"
"  ode [lorenz|rossler|thomas|fx; fy; fz]   chaotic attractor, 2 trajectories\n"
"  orbit [n]              n-body gravity (2..10)\n"
"  brain [xor|ring]       watch a neural net learn (state persists)\n"
"  predict <x> <y>        ask the trained net\n"
"CAPSULES (encrypted, content-addressed, shareable offline)\n"
"  save <file> <pass>     seal expression + net     load <file> <pass>\n"
"OTHER: license | source | help | quit\n"
"CLI: nle --render out.ppm <command>   (headless 3D snapshot)   nle -e <command>\n";

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}
static Node *parse(const char *s) {
    const char *err; Node *n = ex_parse(s, &err);
    if (!n) printf("error: %s\n", err);
    return n;
}
static void do_save(char *arg, int save) {
    char *file = strtok(arg, " "), *pass = strtok(0, "");
    if (!file || !pass) { puts("usage: save|load <file> <passphrase>"); return; }
    pass = trim(pass);
    if (save) {
        char *txt = malloc(300 + (g_net ? (size_t)g_net->np * 26 : 0)), *p = txt;
        p += sprintf(p, "NLE1\nexpr=%s\n", g_expr);
        if (g_net) {
            p += sprintf(p, "net=%d", g_net->nl);
            for (int i = 0; i < g_net->nl; i++) p += sprintf(p, " %d", g_net->sz[i]);
            for (int i = 0; i < g_net->np; i++) p += sprintf(p, " %a", g_net->p[i]);
            p += sprintf(p, "\n");
        }
        size_t n; uint8_t *c = cap_seal(pass, (uint8_t *)txt, (size_t)(p - txt), &n);
        FILE *o = fopen(file, "wb"); char id[65];
        if (!o) { puts("cannot write file"); }
        else { fwrite(c, 1, n, o); fclose(o); cap_id(c, n, id); printf("sealed %zu bytes -> %s\ncapsule id (sha256): %s\n", n, file, id); }
        free(c); free(txt); return;
    }
    FILE *in = fopen(file, "rb"); if (!in) { puts("cannot read file"); return; }
    fseek(in, 0, SEEK_END); long n = ftell(in); fseek(in, 0, SEEK_SET);
    uint8_t *c = malloc((size_t)n + 1); size_t got = fread(c, 1, (size_t)n, in); fclose(in);
    size_t m; uint8_t *d = cap_open(pass, c, got, &m); free(c);
    if (!d) { puts("capsule rejected: wrong passphrase or corrupted data"); return; }
    char *p = strstr((char *)d, "expr=");
    if (p) { p += 5; size_t l = strcspn(p, "\n"); if (l > 255) l = 255; memcpy(g_expr, p, l); g_expr[l] = 0; printf("expression restored: %s\n", g_expr); }
    p = strstr((char *)d, "net=");
    if (p) {
        int nl, sz[6]; p += 4; nl = (int)strtol(p, &p, 10);
        for (int i = 0; i < nl && i < 6; i++) sz[i] = (int)strtol(p, &p, 10);
        Net *nn = nn_new(nl, sz, 1);
        if (nn) {
            for (int i = 0; i < nn->np; i++) nn->p[i] = strtod(p, &p);
            nn_free(g_net); g_net = nn; puts("neural net restored");
        }
    }
    free(d);
}
static void cmd(char *line) {
    line = trim(line); if (!*line) return;
    char *arg = line; while (*arg && !isspace((unsigned char)*arg)) arg++;
    if (*arg) *arg++ = 0;
    arg = trim(arg);
    const char *c = line; Node *n;
    if (!strcmp(c, "help")) fputs(HELP, stdout);
    else if (!strcmp(c, "license")) fputs(NOTICE, stdout);
    else if (!strcmp(c, "source")) puts(SOURCE_URL);
    else if (!strcmp(c, "set")) {
        char v; double x;
        if (sscanf(arg, " %c %lf", &v, &x) == 2 && strchr("xyzt", v)) { g_var[strchr("xyzt", v) - "xyzt"] = x; printf("%c = %.12g\n", v, x); }
        else puts("usage: set x 2");
    }
    else if (!strcmp(c, "eval")) { if ((n = parse(arg))) { printf("= %.12g\n", ex_eval(n, g_var)); ex_free(n); } }
    else if (!strcmp(c, "diff")) {
        if ((n = parse(arg))) {
            char b[512]; int any = 0;
            for (int v = 0; v < 4; v++) {
                Node *d = ex_simp(ex_diff(n, v)); ex_print(d, b, sizeof b);
                if (strcmp(b, "0")) { printf("d/d%c = %s\n", "xyzt"[v], b); any = 1; }
                ex_free(d);
            }
            if (!any) puts("d/dx = 0");
            ex_free(n);
        }
    }
    else if (!strcmp(c, "root")) {
        double x0 = 0, r; char *sp = strrchr(arg, ' ');
        if (sp) { x0 = atof(sp + 1); *sp = 0; }
        if ((n = parse(arg))) {
            int ok = nl_newton(n, 0, x0, &r);
            printf(ok ? "root x = %.12g\n" : "no convergence (last x = %.12g)\n", r); ex_free(n);
        }
    }
    else if (!strcmp(c, "int")) {
        char *s2 = strrchr(arg, ' '); if (!s2) { puts("usage: int <expr> <a> <b>"); return; }
        double b = atof(s2 + 1); *s2 = 0; s2 = strrchr(arg, ' ');
        if (!s2) { puts("usage: int <expr> <a> <b>"); return; }
        const char *err; Node *na = ex_parse(s2 + 1, &err); double a = na ? ex_eval(na, g_var) : 0; ex_free(na); *s2 = 0;
        if ((n = parse(arg))) { printf("integral = %.12g\n", nl_integrate(n, 0, a, b)); ex_free(n); }
    }
    else if (!strcmp(c, "logic")) lg_analyze(arg, stdout, 0, 0, 0);
    else if (!strcmp(c, "cube")) {
        unsigned char tv[64] = {0}; int nv; char nm[28];
        if (lg_analyze(arg, stdout, tv, &nv, nm) == 0) { if (nv <= 6) view_cube(tv, nv, nm); else puts("cube view supports up to 6 variables"); }
    }
    else if (!strcmp(c, "plot")) {
        if (*arg) snprintf(g_expr, sizeof g_expr, "%s", arg);
        if (!*g_expr) snprintf(g_expr, sizeof g_expr, "sin(sqrt(x^2+y^2)*2-t*3)*exp(-0.15*(x^2+y^2))*2");
        if ((n = parse(g_expr))) { view_surface(n); ex_free(n); }
    }
    else if (!strcmp(c, "ode")) view_ode(arg);
    else if (!strcmp(c, "orbit")) view_orbit(*arg ? atoi(arg) : 6);
    else if (!strcmp(c, "brain")) view_brain(&g_net, arg);
    else if (!strcmp(c, "predict")) {
        double in[2], o[1];
        if (!g_net || g_net->sz[0] != 2) puts("train first: brain");
        else if (sscanf(arg, "%lf %lf", &in[0], &in[1]) != 2) puts("usage: predict <x> <y>");
        else { nn_forward(g_net, in, o); printf("net says %.4f\n", o[0]); }
    }
    else if (!strcmp(c, "save")) do_save(arg, 1);
    else if (!strcmp(c, "load")) do_save(arg, 0);
    else if (!strcmp(c, "quit") || !strcmp(c, "exit")) { nn_free(g_net); exit(0); }
    else printf("unknown command '%s' (try: help)\n", c);
}
int main(int argc, char **argv) {
    if (argc > 3 && !strcmp(argv[1], "--render")) {
        char line[512] = ""; g_ppm = argv[2];
        for (int i = 3; i < argc; i++) { strncat(line, argv[i], sizeof line - strlen(line) - 2); strcat(line, " "); }
        cmd(line); return 0;
    }
    if (argc > 2 && !strcmp(argv[1], "-e")) { char line[512] = ""; strncat(line, argv[2], 500); cmd(line); return 0; }
    int tty = isatty(0);
    if (tty) { fputs("== NLE: Decentralized Neural-Logic Engine ==\n", stdout); fputs(NOTICE, stdout); puts("type `help`\n"); }
    char line[600];
    while ((tty ? fputs("nle> ", stdout) : 0), fflush(stdout), fgets(line, sizeof line, stdin)) cmd(line);
    nn_free(g_net); return 0;
}
