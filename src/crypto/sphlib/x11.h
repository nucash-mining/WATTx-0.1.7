/* X11 Algorithm header
 * Provides all 11 hash functions needed for X11:
 * blake, bmw, groestl, jh, keccak, skein, luffa, cubehash, shavite, simd, echo
 *
 * Based on sphlib by Thomas Pornin
 */

#ifndef X11_H__
#define X11_H__

#include "sph_types.h"
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============== Blake ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u64 H[8];
    sph_u64 S[4];
    sph_u64 T0, T1;
} sph_blake512_context;

void sph_blake512_init(sph_blake512_context *cc);
void sph_blake512(sph_blake512_context *cc, const void *data, size_t len);
void sph_blake512_close(sph_blake512_context *cc, void *dst);

/* ============== BMW (Blue Midnight Wish) ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u64 H[16];
    sph_u64 bit_count;
} sph_bmw512_context;

void sph_bmw512_init(sph_bmw512_context *cc);
void sph_bmw512(sph_bmw512_context *cc, const void *data, size_t len);
void sph_bmw512_close(sph_bmw512_context *cc, void *dst);

/* ============== Groestl ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u64 H[16];
    sph_u64 count;
} sph_groestl512_context;

void sph_groestl512_init(sph_groestl512_context *cc);
void sph_groestl512(sph_groestl512_context *cc, const void *data, size_t len);
void sph_groestl512_close(sph_groestl512_context *cc, void *dst);

/* ============== JH ============== */

typedef struct {
    unsigned char buf[64];
    size_t ptr;
    sph_u64 H[16];
    sph_u64 block_count;
} sph_jh512_context;

void sph_jh512_init(sph_jh512_context *cc);
void sph_jh512(sph_jh512_context *cc, const void *data, size_t len);
void sph_jh512_close(sph_jh512_context *cc, void *dst);

/* ============== Keccak ============== */

typedef struct {
    unsigned char buf[144];
    size_t ptr;
    sph_u64 A[25];
} sph_keccak512_context;

void sph_keccak512_init(sph_keccak512_context *cc);
void sph_keccak512(sph_keccak512_context *cc, const void *data, size_t len);
void sph_keccak512_close(sph_keccak512_context *cc, void *dst);

/* ============== Skein ============== */

typedef struct {
    unsigned char buf[64];
    size_t ptr;
    sph_u64 h[8];
    sph_u64 bcount;
} sph_skein512_context;

void sph_skein512_init(sph_skein512_context *cc);
void sph_skein512(sph_skein512_context *cc, const void *data, size_t len);
void sph_skein512_close(sph_skein512_context *cc, void *dst);

/* ============== Luffa ============== */

typedef struct {
    unsigned char buf[32];
    size_t ptr;
    sph_u32 V[5][8];
} sph_luffa512_context;

void sph_luffa512_init(sph_luffa512_context *cc);
void sph_luffa512(sph_luffa512_context *cc, const void *data, size_t len);
void sph_luffa512_close(sph_luffa512_context *cc, void *dst);

/* ============== Cubehash ============== */

typedef struct {
    unsigned char buf[32];
    size_t ptr;
    sph_u32 state[32];
} sph_cubehash512_context;

void sph_cubehash512_init(sph_cubehash512_context *cc);
void sph_cubehash512(sph_cubehash512_context *cc, const void *data, size_t len);
void sph_cubehash512_close(sph_cubehash512_context *cc, void *dst);

/* ============== Shavite ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u32 h[16];
    sph_u32 count0, count1, count2, count3;
} sph_shavite512_context;

void sph_shavite512_init(sph_shavite512_context *cc);
void sph_shavite512(sph_shavite512_context *cc, const void *data, size_t len);
void sph_shavite512_close(sph_shavite512_context *cc, void *dst);

/* ============== SIMD ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u32 state[32];
    sph_u32 count_low, count_high;
} sph_simd512_context;

void sph_simd512_init(sph_simd512_context *cc);
void sph_simd512(sph_simd512_context *cc, const void *data, size_t len);
void sph_simd512_close(sph_simd512_context *cc, void *dst);

/* ============== Echo ============== */

typedef struct {
    unsigned char buf[128];
    size_t ptr;
    sph_u64 state[16];
    sph_u64 C[2];
} sph_echo512_context;

void sph_echo512_init(sph_echo512_context *cc);
void sph_echo512(sph_echo512_context *cc, const void *data, size_t len);
void sph_echo512_close(sph_echo512_context *cc, void *dst);

/* ============== X11 Combined Function ============== */

/**
 * Compute X11 hash of input data.
 * Output is 256 bits (32 bytes).
 *
 * @param input  Input data
 * @param len    Length of input data
 * @param output Output buffer (at least 32 bytes)
 */
void x11_hash(const void *input, size_t len, void *output);

#ifdef __cplusplus
}
#endif

#endif /* X11_H__ */
