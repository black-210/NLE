/* SPDX-License-Identifier: AGPL-3.0-or-later  (c) 2026 NLE contributors
 * Sealed capsules: self-contained, content-addressed, encrypted state.
 * They can travel by any medium (USB, mesh, paper QR) - no server needed. */
#include "nle.h"
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ---------------- SHA-256 ---------------- */
static const uint32_t K[64] = {
0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
#define ROR(x,n) (((x) >> (n)) | ((x) << (32 - (n))))
static void blk(uint32_t *h, const uint8_t *p) {
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | p[4*i+1] << 16 | p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    memcpy(v, h, 32);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 28); v[4] += t1; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
}
void sha256(const void *d, size_t n, uint8_t out[32]) {
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    const uint8_t *p = d; size_t i = n;
    while (i >= 64) { blk(h, p); p += 64; i -= 64; }
    uint8_t t[128] = {0}; memcpy(t, p, i); t[i] = 0x80;
    size_t m = i < 56 ? 64 : 128; uint64_t bits = (uint64_t)n * 8;
    for (int k = 0; k < 8; k++) t[m - 1 - k] = (uint8_t)(bits >> (8 * k));
    blk(h, t); if (m == 128) blk(h, t + 64);
    for (int j = 0; j < 8; j++) for (int k = 0; k < 4; k++) out[4*j + k] = (uint8_t)(h[j] >> (24 - 8*k));
}
void hmac256(const uint8_t *key, size_t kl, const uint8_t *m, size_t n, uint8_t out[32]) {
    uint8_t k[64] = {0}, in[32], *b = malloc(64 + (n > 32 ? n : 32));
    if (kl > 64) sha256(key, kl, k); else memcpy(k, key, kl);
    for (int i = 0; i < 64; i++) b[i] = k[i] ^ 0x36;
    memcpy(b + 64, m, n); sha256(b, 64 + n, in);
    for (int i = 0; i < 64; i++) b[i] = k[i] ^ 0x5c;
    memcpy(b + 64, in, 32); sha256(b, 96, out); free(b);
}

/* ---------------- ChaCha20 (RFC 8439) ---------------- */
#define ROL(x,n) (((x) << (n)) | ((x) >> (32 - (n))))
#define QR(a,b,c,d) a += b; d ^= a; d = ROL(d,16); c += d; b ^= c; b = ROL(b,12); \
                    a += b; d ^= a; d = ROL(d,8);  c += d; b ^= c; b = ROL(b,7);
static uint32_t ld(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
void chacha20(const uint8_t key[32], const uint8_t nonce[12], uint32_t ctr, uint8_t *buf, size_t n) {
    uint32_t s[16] = {0x61707865, 0x3320646e, 0x79622d32, 0x6b206574}, x[16];
    for (int i = 0; i < 8; i++) s[4 + i] = ld(key + 4 * i);
    s[12] = ctr; for (int i = 0; i < 3; i++) s[13 + i] = ld(nonce + 4 * i);
    for (size_t o = 0; o < n; o += 64, s[12]++) {
        memcpy(x, s, 64);
        for (int r = 0; r < 10; r++) {
            QR(x[0],x[4],x[8],x[12]) QR(x[1],x[5],x[9],x[13]) QR(x[2],x[6],x[10],x[14]) QR(x[3],x[7],x[11],x[15])
            QR(x[0],x[5],x[10],x[15]) QR(x[1],x[6],x[11],x[12]) QR(x[2],x[7],x[8],x[13]) QR(x[3],x[4],x[9],x[14])
        }
        for (int i = 0; i < 16; i++) x[i] += s[i];
        for (size_t i = 0; i < 64 && o + i < n; i++) buf[o + i] ^= (uint8_t)(x[i / 4] >> (8 * (i % 4)));
    }
}

/* ---------------- capsules ---------------- */
static void rbytes(uint8_t *b, size_t n) {
    FILE *f = fopen("/dev/urandom", "rb");
    if (f && fread(b, 1, n, f) == n) { fclose(f); return; }
    if (f) fclose(f);
    uint64_t x = (uint64_t)time(0) * 6364136223846793005ULL ^ (uint64_t)clock();   /* weak fallback */
    for (size_t i = 0; i < n; i++) { x = x * 6364136223846793005ULL + 1442695040888963407ULL; b[i] = (uint8_t)(x >> 56); }
}
static void kdf(const char *pass, const uint8_t *salt, uint8_t ek[32], uint8_t mk[32]) {
    size_t pl = strlen(pass); uint8_t *b = malloc(pl + 16 + 33), k[32];
    memcpy(b, pass, pl); memcpy(b + pl, salt, 16); sha256(b, pl + 16, k);
    for (int i = 0; i < 40000; i++) { memcpy(b, k, 32); memcpy(b + 32, pass, pl); memcpy(b + 32 + pl, salt, 16); sha256(b, 48 + pl, k); }
    memcpy(b, k, 32); b[32] = 'E'; sha256(b, 33, ek);
    b[32] = 'M'; sha256(b, 33, mk); free(b);
}
/* layout: "NLEC1" | salt16 | nonce12 | ciphertext | hmac32 */
#define HDR 33
uint8_t *cap_seal(const char *pass, const uint8_t *d, size_t n, size_t *outn) {
    uint8_t *o = malloc(HDR + n + 32), ek[32], mk[32];
    memcpy(o, "NLEC1", 5); rbytes(o + 5, 28); kdf(pass, o + 5, ek, mk);
    memcpy(o + HDR, d, n); chacha20(ek, o + 21, 1, o + HDR, n);
    hmac256(mk, 32, o, HDR + n, o + HDR + n); *outn = HDR + n + 32; return o;
}
uint8_t *cap_open(const char *pass, const uint8_t *c, size_t n, size_t *outn) {
    if (n < HDR + 32 || memcmp(c, "NLEC1", 5)) return 0;
    uint8_t ek[32], mk[32], tag[32], diff = 0; size_t m = n - HDR - 32;
    kdf(pass, c + 5, ek, mk); hmac256(mk, 32, c, HDR + m, tag);
    for (int i = 0; i < 32; i++) diff |= tag[i] ^ c[HDR + m + i];   /* constant time */
    if (diff) return 0;
    uint8_t *o = malloc(m + 1); memcpy(o, c + HDR, m); chacha20(ek, c + 21, 1, o, m);
    o[m] = 0; *outn = m; return o;
}
void cap_id(const uint8_t *c, size_t n, char hex[65]) {
    uint8_t h[32]; sha256(c, n, h);
    for (int i = 0; i < 32; i++) sprintf(hex + 2 * i, "%02x", h[i]);
}
