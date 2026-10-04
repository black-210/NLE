/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors */
#include "nle.h"
#include <math.h>

/* Newton-Raphson using the symbolic derivative of f. */
int nl_newton(const Node *f, int var, double x0, double *root) {
    Node *d = ex_simp(ex_diff(f, var));
    double v[4] = {0, 0, 0, 0}, x = x0; int ok = 0;
    for (int i = 0; i < 100; i++) {
        v[var] = x;
        double fx = ex_eval(f, v), dx = ex_eval(d, v);
        if (!isfinite(fx) || !isfinite(dx) || fabs(dx) < 1e-14) break;
        double nx = x - fx / dx;
        if (fabs(nx - x) < 1e-13 * (1 + fabs(x))) { x = nx; ok = 1; break; }
        x = nx;
    }
    v[var] = x;
    ok = ok && fabs(ex_eval(f, v)) < 1e-6;
    ex_free(d); *root = x; return ok;
}

/* Composite Simpson rule. */
double nl_integrate(const Node *f, int var, double a, double b) {
    int n = 2000; double h = (b - a) / n, v[4] = {0, 0, 0, 0}, s;
    v[var] = a; s = ex_eval(f, v);
    v[var] = b; s += ex_eval(f, v);
    for (int i = 1; i < n; i++) { v[var] = a + i * h; s += (i & 1 ? 4 : 2) * ex_eval(f, v); }
    return s * h / 3;
}

/* Classic 4th-order Runge-Kutta step for a system of up to 64 equations. */
void ode_rk4(ode_fn f, double *s, int n, double t, double h, void *u) {
    double k1[64], k2[64], k3[64], k4[64], y[64];
    f(s, t, k1, u);
    for (int i = 0; i < n; i++) y[i] = s[i] + .5 * h * k1[i];
    f(y, t + .5 * h, k2, u);
    for (int i = 0; i < n; i++) y[i] = s[i] + .5 * h * k2[i];
    f(y, t + .5 * h, k3, u);
    for (int i = 0; i < n; i++) y[i] = s[i] + h * k3[i];
    f(y, t + h, k4, u);
    for (int i = 0; i < n; i++) s[i] += h / 6 * (k1[i] + 2 * k2[i] + 2 * k3[i] + k4[i]);
}
