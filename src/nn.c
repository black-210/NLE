/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors */
#include "nle.h"
#include <math.h>
#include <stdlib.h>

static double rnd(Net *n) {              /* xorshift64*, uniform in [0,1) */
    n->rng ^= n->rng >> 12; n->rng ^= n->rng << 25; n->rng ^= n->rng >> 27;
    return (double)((n->rng * 2685821657736338717ULL) >> 11) / 9007199254740992.0;
}
static int off(const Net *n, int l) {
    int o = 0; for (int i = 0; i < l; i++) o += n->sz[i + 1] * (n->sz[i] + 1); return o;
}
Net *nn_new(int nl, const int *sz, uint64_t seed) {
    if (nl < 2 || nl > 6) return 0;
    Net *n = calloc(1, sizeof *n); n->nl = nl; n->rng = seed ? seed : 88172645463325252ULL;
    for (int i = 0; i < nl; i++) { if (sz[i] < 1 || sz[i] > NN_MAX) { free(n); return 0; } n->sz[i] = sz[i]; }
    n->np = off(n, nl - 1); n->p = calloc((size_t)n->np, sizeof(double));
    for (int l = 0; l < nl - 1; l++) {
        int o = off(n, l), cnt = n->sz[l + 1] * n->sz[l];
        double lim = sqrt(6.0 / (n->sz[l] + n->sz[l + 1]));          /* Xavier */
        for (int i = 0; i < cnt; i++) n->p[o + i] = (rnd(n) * 2 - 1) * lim;
    }
    return n;
}
void nn_free(Net *n) { if (n) { free(n->p); free(n); } }

static void fwd(const Net *n, const double *in, double a[][NN_MAX]) {
    int L = n->nl - 1;
    for (int j = 0; j < n->sz[0]; j++) a[0][j] = in[j];
    for (int l = 0; l < L; l++) {
        int o = off(n, l), ni = n->sz[l], no = n->sz[l + 1];
        for (int k = 0; k < no; k++) {
            double s = n->p[o + no * ni + k];
            for (int j = 0; j < ni; j++) s += n->p[o + k * ni + j] * a[l][j];
            a[l + 1][k] = (l == L - 1) ? 1.0 / (1.0 + exp(-s)) : tanh(s);
        }
    }
}
void nn_forward(const Net *n, const double *in, double *out) {
    double a[6][NN_MAX]; fwd(n, in, a);
    for (int j = 0; j < n->sz[n->nl - 1]; j++) out[j] = a[n->nl - 1][j];
}
/* One epoch of stochastic gradient descent (cnt random samples). Returns MSE. */
double nn_train(Net *n, const double *X, const double *Y, int cnt, double lr) {
    int L = n->nl - 1; double err = 0;
    for (int it = 0; it < cnt; it++) {
        int i = (int)(rnd(n) * cnt) % cnt;
        const double *x = X + i * n->sz[0], *y = Y + i * n->sz[L];
        double a[6][NN_MAX], d[6][NN_MAX];
        fwd(n, x, a);
        for (int j = 0; j < n->sz[L]; j++) {
            double e = a[L][j] - y[j]; err += e * e;
            d[L][j] = e * a[L][j] * (1 - a[L][j]);
        }
        for (int l = L - 1; l >= 1; l--) {
            int o = off(n, l);
            for (int j = 0; j < n->sz[l]; j++) {
                double s = 0;
                for (int k = 0; k < n->sz[l + 1]; k++) s += n->p[o + k * n->sz[l] + j] * d[l + 1][k];
                d[l][j] = s * (1 - a[l][j] * a[l][j]);
            }
        }
        for (int l = 0; l < L; l++) {
            int o = off(n, l), ni = n->sz[l], no = n->sz[l + 1];
            for (int k = 0; k < no; k++) {
                double g = lr * d[l + 1][k];
                for (int j = 0; j < ni; j++) n->p[o + k * ni + j] -= g * a[l][j];
                n->p[o + no * ni + k] -= g;
            }
        }
    }
    return err / (cnt * n->sz[L])



}








double nn_test(const Net *n, const double *X, const double *Y, int cnt) {
    int L = n->nl - 1; double err = 0;
    for (int i = 0; i < cnt; i++) {
        const double *x = X + i * n->sz[0], *y = Y + i * n->sz[L];
        double a[6][NN_MAX];
        fwd(n, x, a);
        for (int j = 0; j < n->sz[L]; j++) err += (a[L][j] - y[j]) * (a[L][j] - y[j]);
        for (int j = 0; j < n->sz[L]; j++) printf("%g ", a[L][j]); puts("");
        for (int j = 0; j < n->sz[n->nl - 1]; j++) printf("%g ", a[n->nl - 1][j]); puts("");
        if (i == 0) break;
        
    
    }

    return err / (cnt * n->sz[L]);
}
double nn_predict(const Net *n, const double *x) {
    double a[6][NN_MAX]; fwd(n, x, a);
    return a[n->nl - 1][0];
}
void nn_save(const Net *n, FILE *f) {
    fwrite(&n->nl, sizeof n->nl, 1, f);
    fwrite(n->sz, sizeof n->sz[0], n->nl, f);
    fwrite(n->p, sizeof n->p[0], n->np, f);
}
Net *nn_load(FILE *f) {
    Net *n = calloc(1, sizeof *n);
    fread(&n->nl, sizeof n->nl, 1, f);
    n->sz = malloc((size_t)n->nl * sizeof n->sz[0]);
    fread(n->sz, sizeof n->sz[0], n->nl, f);
    n->np = off(n, n->nl - 1);
    n->p = malloc((size_t)n->np * sizeof n->p[0]);
    fread(n->p, sizeof n->p[0], n->np, f);
    return n;
}
Net *nn_copy(const Net *n) {
    Net *m = calloc(1, sizeof *m);
    m->nl = n->nl; m->rng = n->rng;
    m->sz = malloc((size_t)m->nl * sizeof m->sz[0]);
    memcpy(m->sz, n->sz, (size_t)m->nl * sizeof m->sz[0]);
    m->np = off(m, m->nl - 1);
    m->p = malloc((size_t)m->np * sizeof m->p[0]);
    memcpy(m->p, n->p, (size_t)m->np * sizeof m->p[0]);
    return m;
}