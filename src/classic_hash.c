/* Portable MD5 and SHA-256 for verifying downloads (no OS crypto library, same result on every host). */
#include "classic_apps.h"
#include <stdio.h>
#include <string.h>

#define ROL(x, n) (((x) << (n)) | ((x) >> (32 - (n))))
#define ROR(x, n) (((x) >> (n)) | ((x) << (32 - (n))))

static void md5_block(uint32_t s[4], const uint8_t *p) {
    static const uint32_t K[64] = {
        0xd76aa478,0xe8c7b756,0x242070db,0xc1bdceee,0xf57c0faf,0x4787c62a,0xa8304613,0xfd469501,
        0x698098d8,0x8b44f7af,0xffff5bb1,0x895cd7be,0x6b901122,0xfd987193,0xa679438e,0x49b40821,
        0xf61e2562,0xc040b340,0x265e5a51,0xe9b6c7aa,0xd62f105d,0x02441453,0xd8a1e681,0xe7d3fbc8,
        0x21e1cde6,0xc33707d6,0xf4d50d87,0x455a14ed,0xa9e3e905,0xfcefa3f8,0x676f02d9,0x8d2a4c8a,
        0xfffa3942,0x8771f681,0x6d9d6122,0xfde5380c,0xa4beea44,0x4bdecfa9,0xf6bb4b60,0xbebfbc70,
        0x289b7ec6,0xeaa127fa,0xd4ef3085,0x04881d05,0xd9d4d039,0xe6db99e5,0x1fa27cf8,0xc4ac5665,
        0xf4292244,0x432aff97,0xab9423a7,0xfc93a039,0x655b59c3,0x8f0ccc92,0xffeff47d,0x85845dd1,
        0x6fa87e4f,0xfe2ce6e0,0xa3014314,0x4e0811a1,0xf7537e82,0xbd3af235,0x2ad7d2bb,0xeb86d391};
    static const uint8_t R[64] = {7,12,17,22,7,12,17,22,7,12,17,22,7,12,17,22,5,9,14,20,5,9,14,20,5,9,14,20,5,9,14,20,
                                  4,11,16,23,4,11,16,23,4,11,16,23,4,11,16,23,6,10,15,21,6,10,15,21,6,10,15,21,6,10,15,21};
    uint32_t m[16], a = s[0], b = s[1], c = s[2], d = s[3];
    for (int i = 0; i < 16; i++)
        m[i] = (uint32_t)p[i*4] | (uint32_t)p[i*4+1] << 8 | (uint32_t)p[i*4+2] << 16 | (uint32_t)p[i*4+3] << 24;
    for (int i = 0; i < 64; i++) {
        uint32_t f; int g;
        if (i < 16) { f = (b & c) | (~b & d); g = i; }
        else if (i < 32) { f = (d & b) | (~d & c); g = (5 * i + 1) & 15; }
        else if (i < 48) { f = b ^ c ^ d; g = (3 * i + 5) & 15; }
        else { f = c ^ (b | ~d); g = (7 * i) & 15; }
        uint32_t t = d; d = c; c = b;
        b = b + ROL(a + f + K[i] + m[g], R[i]);
        a = t;
    }
    s[0] += a; s[1] += b; s[2] += c; s[3] += d;
}

static void sha_block(uint32_t h[8], const uint8_t *p) {
    static const uint32_t K[64] = {
        0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
        0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
        0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
        0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
        0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
        0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
        0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
        0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
    uint32_t w[64], v[8];
    for (int i = 0; i < 16; i++)
        w[i] = (uint32_t)p[i*4] << 24 | (uint32_t)p[i*4+1] << 16 | (uint32_t)p[i*4+2] << 8 | p[i*4+3];
    for (int i = 16; i < 64; i++) {
        uint32_t s0 = ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3);
        uint32_t s1 = ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    memcpy(v, h, sizeof v);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = v[7] + (ROR(v[4], 6) ^ ROR(v[4], 11) ^ ROR(v[4], 25)) + ((v[4] & v[5]) ^ (~v[4] & v[6])) + K[i] + w[i];
        uint32_t t2 = (ROR(v[0], 2) ^ ROR(v[0], 13) ^ ROR(v[0], 22)) + ((v[0] & v[1]) ^ (v[0] & v[2]) ^ (v[1] & v[2]));
        memmove(v + 1, v, 7 * sizeof *v);
        v[4] += t1; v[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) h[i] += v[i];
}

/* Pad the final partial block (used bytes in tail) with the 0x80 marker and the bit length. */
static void finish(const uint8_t *tail, size_t used, uint64_t total, int big, void *st,
                   void (*blk)(void *, const uint8_t *)) {
    uint8_t b[128] = {0};
    uint64_t bits = total * 8;
    size_t len = used < 56 ? 64 : 128;
    memcpy(b, tail, used);
    b[used] = 0x80;
    for (int i = 0; i < 8; i++) b[len - 8 + i] = (uint8_t)(bits >> (big ? 56 - 8 * i : 8 * i));
    for (size_t o = 0; o < len; o += 64) blk(st, b + o);
}
static void md5_cb(void *st, const uint8_t *p) { md5_block(st, p); }
static void sha_cb(void *st, const uint8_t *p) { sha_block(st, p); }

int classic_hash_file(const char *path, char md5_hex[33], char sha256_hex[65], uint64_t *size) {
    FILE *f = path ? fopen(path, "rb") : NULL;
    if (!f) return -1;
    uint32_t m[4] = {0x67452301, 0xefcdab89, 0x98badcfe, 0x10325476};
    uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    uint8_t buf[64];
    uint64_t total = 0;
    size_t got;
    while ((got = fread(buf, 1, 64, f)) == 64) { md5_block(m, buf); sha_block(h, buf); total += 64; }
    int bad = ferror(f);
    fclose(f);
    if (bad) return -1;
    total += got;
    finish(buf, got, total, 0, m, md5_cb);
    finish(buf, got, total, 1, h, sha_cb);
    for (int i = 0; i < 16; i++) snprintf(md5_hex + i * 2, 3, "%02x", (m[i / 4] >> (8 * (i % 4))) & 255);
    for (int i = 0; i < 8; i++) snprintf(sha256_hex + i * 8, 9, "%08x", h[i]);
    if (size) *size = total;
    return 0;
}
