/* X11 Algorithm implementation
 * Contains all 11 hash functions for X11:
 * blake, bmw, groestl, jh, keccak, skein, luffa, cubehash, shavite, simd, echo
 *
 * Based on sphlib by Thomas Pornin (https://www.saphir2.com/sphlib/)
 * MIT License
 */

#include "x11.h"
#include <string.h>

/* ============================================================
 * BLAKE-512
 * ============================================================ */

static const sph_u64 BLAKE_IV512[8] = {
    SPH_C64(0x6A09E667F3BCC908), SPH_C64(0xBB67AE8584CAA73B),
    SPH_C64(0x3C6EF372FE94F82B), SPH_C64(0xA54FF53A5F1D36F1),
    SPH_C64(0x510E527FADE682D1), SPH_C64(0x9B05688C2B3E6C1F),
    SPH_C64(0x1F83D9ABFB41BD6B), SPH_C64(0x5BE0CD19137E2179)
};

static const sph_u64 BLAKE_C[16] = {
    SPH_C64(0x243F6A8885A308D3), SPH_C64(0x13198A2E03707344),
    SPH_C64(0xA4093822299F31D0), SPH_C64(0x082EFA98EC4E6C89),
    SPH_C64(0x452821E638D01377), SPH_C64(0xBE5466CF34E90C6C),
    SPH_C64(0xC0AC29B7C97C50DD), SPH_C64(0x3F84D5B5B5470917),
    SPH_C64(0x9216D5D98979FB1B), SPH_C64(0xD1310BA698DFB5AC),
    SPH_C64(0x2FFD72DBD01ADFB7), SPH_C64(0xB8E1AFED6A267E96),
    SPH_C64(0xBA7C9045F12C7F99), SPH_C64(0x24A19947B3916CF7),
    SPH_C64(0x0801F2E2858EFC16), SPH_C64(0x636920D871574E69)
};

static const unsigned char BLAKE_SIGMA[16][16] = {
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 },
    { 12,  5,  1, 15, 14, 13,  4, 10,  0,  7,  6,  3,  9,  2,  8, 11 },
    { 13, 11,  7, 14, 12,  1,  3,  9,  5,  0, 15,  4,  8,  6,  2, 10 },
    {  6, 15, 14,  9, 11,  3,  0,  8, 12,  2, 13,  7,  1,  4, 10,  5 },
    { 10,  2,  8,  4,  7,  6,  1,  5, 15, 11,  9, 14,  3, 12, 13,  0 },
    {  0,  1,  2,  3,  4,  5,  6,  7,  8,  9, 10, 11, 12, 13, 14, 15 },
    { 14, 10,  4,  8,  9, 15, 13,  6,  1, 12,  0,  2, 11,  7,  5,  3 },
    { 11,  8, 12,  0,  5,  2, 15, 13, 10, 14,  3,  6,  7,  1,  9,  4 },
    {  7,  9,  3,  1, 13, 12, 11, 14,  2,  6,  5, 10,  4,  0, 15,  8 },
    {  9,  0,  5,  7,  2,  4, 10, 15, 14,  1, 11, 12,  6,  8,  3, 13 },
    {  2, 12,  6, 10,  0, 11,  8,  3,  4, 13,  7,  5, 15, 14,  1,  9 }
};

#define BLAKE_G64(a, b, c, d, i) do { \
    a = a + b + (m[BLAKE_SIGMA[r][i]] ^ BLAKE_C[BLAKE_SIGMA[r][i + 1]]); \
    d = SPH_ROTR64(d ^ a, 32); \
    c = c + d; \
    b = SPH_ROTR64(b ^ c, 25); \
    a = a + b + (m[BLAKE_SIGMA[r][i + 1]] ^ BLAKE_C[BLAKE_SIGMA[r][i]]); \
    d = SPH_ROTR64(d ^ a, 16); \
    c = c + d; \
    b = SPH_ROTR64(b ^ c, 11); \
} while(0)

static void blake512_compress(sph_blake512_context *cc, const unsigned char *data)
{
    sph_u64 m[16], v[16];
    int r;

    for (int i = 0; i < 16; i++)
        m[i] = sph_dec64be(data + i * 8);

    for (int i = 0; i < 8; i++)
        v[i] = cc->H[i];

    v[8] = cc->S[0] ^ BLAKE_C[0];
    v[9] = cc->S[1] ^ BLAKE_C[1];
    v[10] = cc->S[2] ^ BLAKE_C[2];
    v[11] = cc->S[3] ^ BLAKE_C[3];
    v[12] = BLAKE_C[4] ^ cc->T0;
    v[13] = BLAKE_C[5] ^ cc->T0;
    v[14] = BLAKE_C[6] ^ cc->T1;
    v[15] = BLAKE_C[7] ^ cc->T1;

    for (r = 0; r < 16; r++) {
        BLAKE_G64(v[0], v[4], v[8], v[12], 0);
        BLAKE_G64(v[1], v[5], v[9], v[13], 2);
        BLAKE_G64(v[2], v[6], v[10], v[14], 4);
        BLAKE_G64(v[3], v[7], v[11], v[15], 6);
        BLAKE_G64(v[0], v[5], v[10], v[15], 8);
        BLAKE_G64(v[1], v[6], v[11], v[12], 10);
        BLAKE_G64(v[2], v[7], v[8], v[13], 12);
        BLAKE_G64(v[3], v[4], v[9], v[14], 14);
    }

    for (int i = 0; i < 8; i++)
        cc->H[i] ^= cc->S[i % 4] ^ v[i] ^ v[i + 8];
}

void sph_blake512_init(sph_blake512_context *cc)
{
    memcpy(cc->H, BLAKE_IV512, sizeof(cc->H));
    memset(cc->S, 0, sizeof(cc->S));
    cc->T0 = cc->T1 = 0;
    cc->ptr = 0;
}

void sph_blake512(sph_blake512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            cc->T0 += 1024;
            if (cc->T0 < 1024) cc->T1++;
            blake512_compress(cc, cc->buf);
            cc->ptr = 0;
        }
    }
}

void sph_blake512_close(sph_blake512_context *cc, void *dst)
{
    sph_u64 th, tl;
    tl = cc->T0 + (cc->ptr << 3);
    th = cc->T1;
    if (tl < cc->T0) th++;

    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 112) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        cc->T0 = cc->T1 = 0;
        blake512_compress(cc, cc->buf);
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 112 - cc->ptr);
    cc->buf[111] |= 1;
    cc->T0 = SPH_C64(0xFFFFFFFFFFFFFC00) + tl;
    cc->T1 = SPH_C64(0xFFFFFFFFFFFFFFFF) + th;
    if (cc->T0 < tl) cc->T1++;
    sph_enc64be(cc->buf + 112, th);
    sph_enc64be(cc->buf + 120, tl);
    blake512_compress(cc, cc->buf);

    for (int i = 0; i < 8; i++)
        sph_enc64be((unsigned char *)dst + i * 8, cc->H[i]);
}

/* ============================================================
 * BMW-512 (Blue Midnight Wish)
 * ============================================================ */

static const sph_u64 BMW_IV512[16] = {
    SPH_C64(0x8081828384858687), SPH_C64(0x88898A8B8C8D8E8F),
    SPH_C64(0x9091929394959697), SPH_C64(0x98999A9B9C9D9E9F),
    SPH_C64(0xA0A1A2A3A4A5A6A7), SPH_C64(0xA8A9AAABACADAEAF),
    SPH_C64(0xB0B1B2B3B4B5B6B7), SPH_C64(0xB8B9BABBBCBDBEBF),
    SPH_C64(0xC0C1C2C3C4C5C6C7), SPH_C64(0xC8C9CACBCCCDCECF),
    SPH_C64(0xD0D1D2D3D4D5D6D7), SPH_C64(0xD8D9DADBDCDDDEDF),
    SPH_C64(0xE0E1E2E3E4E5E6E7), SPH_C64(0xE8E9EAEBECEDEEEF),
    SPH_C64(0xF0F1F2F3F4F5F6F7), SPH_C64(0xF8F9FAFBFCFDFEFF)
};

#define BMW_S64_0(x)   (SPH_ROTR64(x, 1) ^ SPH_ROTR64(x, 2) ^ SPH_ROTR64(x, 3) ^ SPH_ROTR64(x, 4))
#define BMW_S64_1(x)   (SPH_ROTR64(x, 5) ^ SPH_ROTR64(x, 6) ^ SPH_ROTR64(x, 7) ^ SPH_ROTR64(x, 8))
#define BMW_S64_2(x)   (SPH_ROTR64(x, 9) ^ SPH_ROTR64(x, 10) ^ SPH_ROTR64(x, 11) ^ SPH_ROTR64(x, 12))
#define BMW_S64_3(x)   (SPH_ROTR64(x, 13) ^ SPH_ROTR64(x, 14) ^ SPH_ROTR64(x, 15) ^ SPH_ROTR64(x, 16))
#define BMW_S64_4(x)   (SPH_ROTR64(x, 17) ^ SPH_ROTR64(x, 18) ^ SPH_ROTR64(x, 19) ^ SPH_ROTR64(x, 20))
#define BMW_S64_5(x)   (SPH_ROTR64(x, 21) ^ SPH_ROTR64(x, 22) ^ SPH_ROTR64(x, 23) ^ SPH_ROTR64(x, 24))

static void bmw512_compress(sph_u64 *H, const sph_u64 *M)
{
    sph_u64 Q[32], XL64, XH64;
    sph_u64 W[16];
    int i;

    for (i = 0; i < 16; i++)
        W[i] = H[i] ^ M[i];

    /* Expansion */
    Q[0] = W[5] - W[7] + W[10] + W[13] + W[14];
    Q[1] = W[6] - W[8] + W[11] + W[14] - W[15];
    Q[2] = W[0] + W[7] + W[9] - W[12] + W[15];
    Q[3] = W[0] - W[1] + W[8] - W[10] + W[13];
    Q[4] = W[1] + W[2] + W[9] - W[11] - W[14];
    Q[5] = W[3] - W[2] + W[10] - W[12] + W[15];
    Q[6] = W[4] - W[0] - W[3] - W[11] + W[13];
    Q[7] = W[1] - W[4] - W[5] - W[12] - W[14];
    Q[8] = W[2] - W[5] - W[6] + W[13] - W[15];
    Q[9] = W[0] - W[3] + W[6] - W[7] + W[14];
    Q[10] = W[8] - W[1] - W[4] - W[7] + W[15];
    Q[11] = W[8] - W[0] - W[2] - W[5] + W[9];
    Q[12] = W[1] + W[3] - W[6] - W[9] + W[10];
    Q[13] = W[2] + W[4] + W[7] + W[10] + W[11];
    Q[14] = W[3] - W[5] + W[8] - W[11] - W[12];
    Q[15] = W[12] - W[4] - W[6] - W[9] + W[13];

    for (i = 0; i < 16; i++)
        Q[i] = BMW_S64_0(Q[i]) + H[(i + 1) % 16];

    for (i = 16; i < 18; i++)
        Q[i] = Q[i - 16] + SPH_ROTR64(Q[i - 15], 1) + Q[i - 14] + SPH_ROTR64(Q[i - 13], 2) +
               Q[i - 12] + SPH_ROTR64(Q[i - 11], 3) + Q[i - 10] + SPH_ROTR64(Q[i - 9], 4) +
               Q[i - 8] + SPH_ROTR64(Q[i - 7], 5) + Q[i - 6] + SPH_ROTR64(Q[i - 5], 6) +
               Q[i - 4] + SPH_ROTR64(Q[i - 3], 7) + BMW_S64_4(Q[i - 2]) + BMW_S64_5(Q[i - 1]);

    for (i = 18; i < 32; i++)
        Q[i] = Q[i - 16] + SPH_ROTR64(Q[i - 15], 1) + Q[i - 14] + SPH_ROTR64(Q[i - 13], 2) +
               Q[i - 12] + SPH_ROTR64(Q[i - 11], 3) + Q[i - 10] + SPH_ROTR64(Q[i - 9], 4) +
               Q[i - 8] + SPH_ROTR64(Q[i - 7], 5) + Q[i - 6] + SPH_ROTR64(Q[i - 5], 6) +
               Q[i - 4] + SPH_ROTR64(Q[i - 3], 7) + BMW_S64_4(Q[i - 2]) + BMW_S64_5(Q[i - 1]);

    XL64 = Q[16] ^ Q[17] ^ Q[18] ^ Q[19] ^ Q[20] ^ Q[21] ^ Q[22] ^ Q[23];
    XH64 = XL64 ^ Q[24] ^ Q[25] ^ Q[26] ^ Q[27] ^ Q[28] ^ Q[29] ^ Q[30] ^ Q[31];

    H[0] = ((XH64 << 5) ^ (Q[16] >> 5) ^ M[0]) + (XL64 ^ Q[24] ^ Q[0]);
    H[1] = ((XH64 >> 7) ^ (Q[17] << 8) ^ M[1]) + (XL64 ^ Q[25] ^ Q[1]);
    H[2] = ((XH64 >> 5) ^ (Q[18] << 5) ^ M[2]) + (XL64 ^ Q[26] ^ Q[2]);
    H[3] = ((XH64 >> 1) ^ (Q[19] << 5) ^ M[3]) + (XL64 ^ Q[27] ^ Q[3]);
    H[4] = ((XH64 >> 3) ^ (Q[20] << 0) ^ M[4]) + (XL64 ^ Q[28] ^ Q[4]);
    H[5] = ((XH64 << 6) ^ (Q[21] >> 6) ^ M[5]) + (XL64 ^ Q[29] ^ Q[5]);
    H[6] = ((XH64 >> 4) ^ (Q[22] << 6) ^ M[6]) + (XL64 ^ Q[30] ^ Q[6]);
    H[7] = ((XH64 >> 11) ^ (Q[23] << 2) ^ M[7]) + (XL64 ^ Q[31] ^ Q[7]);
    H[8] = SPH_ROTL64(H[4], 9) + (XH64 ^ Q[24] ^ M[8]) + ((XL64 << 8) ^ Q[23] ^ Q[8]);
    H[9] = SPH_ROTL64(H[5], 10) + (XH64 ^ Q[25] ^ M[9]) + ((XL64 >> 6) ^ Q[16] ^ Q[9]);
    H[10] = SPH_ROTL64(H[6], 11) + (XH64 ^ Q[26] ^ M[10]) + ((XL64 << 6) ^ Q[17] ^ Q[10]);
    H[11] = SPH_ROTL64(H[7], 12) + (XH64 ^ Q[27] ^ M[11]) + ((XL64 << 4) ^ Q[18] ^ Q[11]);
    H[12] = SPH_ROTL64(H[0], 13) + (XH64 ^ Q[28] ^ M[12]) + ((XL64 >> 3) ^ Q[19] ^ Q[12]);
    H[13] = SPH_ROTL64(H[1], 14) + (XH64 ^ Q[29] ^ M[13]) + ((XL64 >> 4) ^ Q[20] ^ Q[13]);
    H[14] = SPH_ROTL64(H[2], 15) + (XH64 ^ Q[30] ^ M[14]) + ((XL64 >> 7) ^ Q[21] ^ Q[14]);
    H[15] = SPH_ROTL64(H[3], 16) + (XH64 ^ Q[31] ^ M[15]) + ((XL64 >> 2) ^ Q[22] ^ Q[15]);
}

void sph_bmw512_init(sph_bmw512_context *cc)
{
    memcpy(cc->H, BMW_IV512, sizeof(cc->H));
    cc->ptr = 0;
    cc->bit_count = 0;
}

void sph_bmw512(sph_bmw512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    sph_u64 M[16];

    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            for (int i = 0; i < 16; i++)
                M[i] = sph_dec64le(cc->buf + i * 8);
            bmw512_compress(cc->H, M);
            cc->ptr = 0;
        }
    }
}

void sph_bmw512_close(sph_bmw512_context *cc, void *dst)
{
    sph_u64 M[16];
    sph_u64 H2[16];
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 112) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        for (int i = 0; i < 16; i++)
            M[i] = sph_dec64le(cc->buf + i * 8);
        bmw512_compress(cc->H, M);
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 112 - cc->ptr);
    sph_enc64le(cc->buf + 112, cc->bit_count);
    sph_enc64le(cc->buf + 120, 0);
    for (int i = 0; i < 16; i++)
        M[i] = sph_dec64le(cc->buf + i * 8);
    bmw512_compress(cc->H, M);

    /* Final */
    memcpy(H2, BMW_IV512, sizeof(H2));
    bmw512_compress(H2, cc->H);

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, H2[i + 8]);
}

/* ============================================================
 * Groestl-512
 * ============================================================ */

static const sph_u64 GROESTL_T0[256] = {
    SPH_C64(0xc632f4a5f497a5c6), SPH_C64(0xf86f978497eb84f8),
    SPH_C64(0xee5eb099b0c799ee), SPH_C64(0xf67a8c8d8cf78df6),
    SPH_C64(0xffe8170d17e50dff), SPH_C64(0xd60adcbddcb7bdd6),
    SPH_C64(0xde16c8b1c8a7b1de), SPH_C64(0x916dfc54fc395491),
    SPH_C64(0x6090f050f0c05060), SPH_C64(0x0207050305040302),
    SPH_C64(0xce2ee0a9e087a9ce), SPH_C64(0x56d1877d87ac7d56),
    SPH_C64(0xe7cc2b192bd519e7), SPH_C64(0xb513a662a67162b5),
    SPH_C64(0x4d7c31e6319ae64d), SPH_C64(0xec59b59ab5c39aec),
    SPH_C64(0x8f40cf45cf05458f), SPH_C64(0x1fa3bc9dbc3e9d1f),
    SPH_C64(0x8949c040c0094089), SPH_C64(0xfa68928792ef87fa),
    SPH_C64(0xefd03f153fc515ef), SPH_C64(0xb29426eb267febb2),
    SPH_C64(0x8ece40c94007c98e), SPH_C64(0xfbe61d0b1ded0bfb),
    SPH_C64(0x416e2fec2f82ec41), SPH_C64(0xb31aa967a97d67b3),
    SPH_C64(0x5f431cfd1cbefd5f), SPH_C64(0x456025ea258aea45),
    SPH_C64(0x23f9dabfda46bf23), SPH_C64(0x535102f702a6f753),
    SPH_C64(0xe445a196a1d396e4), SPH_C64(0x9b76ed5bed2d5b9b),
    SPH_C64(0x75285dc25deac275), SPH_C64(0xe1c5241c24d91ce1),
    SPH_C64(0x3dd4e9aee97aae3d), SPH_C64(0x4cf2be6abe986a4c),
    SPH_C64(0x6c82ee5aeed85a6c), SPH_C64(0x7ebdc341c3fc417e),
    SPH_C64(0xf5f3060206f102f5), SPH_C64(0x8352d14fd11d4f83),
    SPH_C64(0x688ce45ce4d05c68), SPH_C64(0x515607f407a2f451),
    SPH_C64(0xd18d5c345cb934d1), SPH_C64(0xf9e1180818e908f9),
    SPH_C64(0xe24cae93aedf93e2), SPH_C64(0xab3e9573954d73ab),
    SPH_C64(0x6297f553f5c45362), SPH_C64(0x2a6b413f41543f2a),
    SPH_C64(0x081c140c14100c08), SPH_C64(0x9563f652f6315295),
    SPH_C64(0x46e9af65af8c6546), SPH_C64(0x9d7fe25ee2215e9d),
    SPH_C64(0x3048782878602830), SPH_C64(0x37cff8a1f86ea137),
    SPH_C64(0x0a1b110f11140f0a), SPH_C64(0x2febc4b5c45eb52f),
    SPH_C64(0x0e151b091b1c090e), SPH_C64(0x247e5a365a483624),
    SPH_C64(0x1badb69bb6369b1b), SPH_C64(0xdf98473d47a53ddf),
    SPH_C64(0xcda76a266a8126cd), SPH_C64(0x4ef5bb69bb9c694e),
    SPH_C64(0x7f334ccd4cfecd7f), SPH_C64(0xea50ba9fbacf9fea),
    SPH_C64(0x123f2d1b2d241b12), SPH_C64(0x1da4b99eb93a9e1d),
    SPH_C64(0x58c49c749cb07458), SPH_C64(0x3446722e72682e34),
    SPH_C64(0x3641772d776c2d36), SPH_C64(0xdc11cdb2cda3b2dc),
    SPH_C64(0xb49d29ee297beed4), SPH_C64(0x5b4d16fb16b6fb5b),
    SPH_C64(0xa4a501f60153f6a4), SPH_C64(0x76a1d74dd7ec4d76),
    SPH_C64(0xb714a361a37561b7), SPH_C64(0x7d3449ce49face7d),
    SPH_C64(0x52df8d7b8da47b52), SPH_C64(0xdd9f423e42a13edd),
    SPH_C64(0x5ecd937193bc715e), SPH_C64(0x13b1a297a2269713),
    SPH_C64(0xa6a204f50457f5a6), SPH_C64(0xb901b868b86968b9),
    SPH_C64(0x0000000000000000), SPH_C64(0xc1b5742c74992cc1),
    SPH_C64(0x40e0a060a0806040), SPH_C64(0xe3c2211f21dd1fe3),
    SPH_C64(0x793a43c843f2c879), SPH_C64(0xb69a2ced2c77edb6),
    SPH_C64(0xd40dd9bed9b3bed4), SPH_C64(0x8d47ca46ca01468d),
    SPH_C64(0x671770d970ced967), SPH_C64(0x72afdd4bdde44b72),
    SPH_C64(0x94ed79de7933de94), SPH_C64(0x98ff67d4672bd498),
    SPH_C64(0xb09323e8237be8b0), SPH_C64(0x855bde4ade114a85),
    SPH_C64(0xbb06bd6bbd6d6bbb), SPH_C64(0xc5bb7e2a7e912ac5),
    SPH_C64(0x4f7b34e5349ee54f), SPH_C64(0xedd73a163ac116ed),
    SPH_C64(0x86d254c55417c586), SPH_C64(0x9af862d7622fd79a),
    SPH_C64(0x6699ff55ffcc5566), SPH_C64(0x11b6a794a7229411),
    SPH_C64(0x8ac04acf4a0fcf8a), SPH_C64(0xe9d9301030c910e9),
    SPH_C64(0x040e0a060a080604), SPH_C64(0xfe66988198e781fe),
    SPH_C64(0xa0ab0bf00b5bf0a0), SPH_C64(0x78b4cc44ccf04478),
    SPH_C64(0x25f0d5bad54aba25), SPH_C64(0x4b753ee33e96e34b),
    SPH_C64(0xa2ac0ef30e5ff3a2), SPH_C64(0x5d4419fe19bafe5d),
    SPH_C64(0x80db5bc05b1bc080), SPH_C64(0x0580858a850a8a05),
    SPH_C64(0x3fd3ecadec7ead3f), SPH_C64(0x21fedfbcdf42bc21),
    SPH_C64(0x70a8d848d8e04870), SPH_C64(0xf1fd0c040cf904f1),
    SPH_C64(0x63197adf7ac6df63), SPH_C64(0x772f58c158eec177),
    SPH_C64(0xaf309f759f4575af), SPH_C64(0x42e7a563a5846342),
    SPH_C64(0x2070503050403020), SPH_C64(0xe5cb2e1a2ed11ae5),
    SPH_C64(0xfdef120e12e10efd), SPH_C64(0xbf08b76db7656dbf),
    SPH_C64(0x8155d44cd4194c81), SPH_C64(0x18243c143c301418),
    SPH_C64(0x26795f355f4c3526), SPH_C64(0xc3b2712f719d2fc3),
    SPH_C64(0xbe8638e13867e1be), SPH_C64(0x35c8fda2fd6aa235),
    SPH_C64(0x88c74fcc4f0bcc88), SPH_C64(0x2e654b394b5c392e),
    SPH_C64(0x936af957f93d5793), SPH_C64(0x55580df20daaf255),
    SPH_C64(0xfc619d829de382fc), SPH_C64(0x7ab3c947c9f4477a),
    SPH_C64(0xc827efacef8baccf), SPH_C64(0xba8832e7326fe7ba),
    SPH_C64(0x324f7d2b7d642b32), SPH_C64(0xe642a495a4d795e6),
    SPH_C64(0xc03bfba0fb9ba0c0), SPH_C64(0x19aab398b3329819),
    SPH_C64(0x9ef668d16827d19e), SPH_C64(0xa322817f815d7fa3),
    SPH_C64(0x44eeaa66aa886644), SPH_C64(0x54d6827e82a87e54),
    SPH_C64(0x3bdde6abe676ab3b), SPH_C64(0x0b959e839e16830b),
    SPH_C64(0x8cc945ca4503ca8c), SPH_C64(0xc7bc7b297b9529c7),
    SPH_C64(0x6b056ed36ed6d36b), SPH_C64(0x286c443c44503c28),
    SPH_C64(0xa72c8b798b5579a7), SPH_C64(0xbc813de23d63e2bc),
    SPH_C64(0x1631271d272c1d16), SPH_C64(0xad379a769a4176ad),
    SPH_C64(0xdb964d3b4dad3bdb), SPH_C64(0x649efa56fac85664),
    SPH_C64(0x74a6d24ed2e84e74), SPH_C64(0x1436221e22281e14),
    SPH_C64(0x92e476db763fdb92), SPH_C64(0x0c121e0a1e180a0c),
    SPH_C64(0x48fcb46cb4906c48), SPH_C64(0xb88f37e4376be4b8),
    SPH_C64(0x9f78e75de7255d9f), SPH_C64(0xbd0fb26eb2616ebd),
    SPH_C64(0x43692aef2a86ef43), SPH_C64(0xc435f1a6f193a6c4),
    SPH_C64(0x39dae3a8e372a839), SPH_C64(0x31c6f7a4f762a431),
    SPH_C64(0xd38a593759bd37d3), SPH_C64(0xf274868b86ff8bf2),
    SPH_C64(0xd583563256b132d5), SPH_C64(0x8b4ec543c50d438b),
    SPH_C64(0x6e85eb59ebdc596e), SPH_C64(0xda18c2b7c2afb7da),
    SPH_C64(0x018e8f8c8f028c01), SPH_C64(0xb11dac64ac7964b1),
    SPH_C64(0x9cf16dd26d23d29c), SPH_C64(0x49723be03b92e049),
    SPH_C64(0xd81fc7b4c7abb4d8), SPH_C64(0xacb915fa1543faac),
    SPH_C64(0xf3fa090709fd07f3), SPH_C64(0xcfa06f256f8525cf),
    SPH_C64(0xca20eaafea8fafca), SPH_C64(0xf47d898e89f38ef4),
    SPH_C64(0x476720e9208ee947), SPH_C64(0x1038281828201810),
    SPH_C64(0x6f0b64d564ded56f), SPH_C64(0xf073838883fb88f0),
    SPH_C64(0x4afbb16fb1946f4a), SPH_C64(0x5cca967296b8725c),
    SPH_C64(0x38546c246c702438), SPH_C64(0x575f08f108aef157),
    SPH_C64(0x732152c752e6c773), SPH_C64(0x9764f351f3355197),
    SPH_C64(0xcbae6523658d23cb), SPH_C64(0xa125847c84597ca1),
    SPH_C64(0xe857bf9cbfcb9ce8), SPH_C64(0x3e5d6321637c213e),
    SPH_C64(0x96ea7cdd7c37dd96), SPH_C64(0x611e7fdc7fc2dc61),
    SPH_C64(0x0d9c9186911a860d), SPH_C64(0x0f9b9485941e850f),
    SPH_C64(0xe04bab90abdb90e0), SPH_C64(0x7cbac642c6f8427c),
    SPH_C64(0x712657c457e2c471), SPH_C64(0xcc29e5aae583aacc),
    SPH_C64(0x90e373d8733bd890), SPH_C64(0x06090f050f0c0506),
    SPH_C64(0xf7f4030103f501f7), SPH_C64(0x1c2a36123638121c),
    SPH_C64(0xc23cfea3fe9fa3c2), SPH_C64(0x6a8be15fe1d45f6a),
    SPH_C64(0xaebe10f91047f9ae), SPH_C64(0x69026bd06bd2d069),
    SPH_C64(0x17bfa891a82e9117), SPH_C64(0x9971e858e8295899),
    SPH_C64(0x3a5369276974273a), SPH_C64(0x27f7d0b9d04eb927),
    SPH_C64(0xd991483848a938d9), SPH_C64(0xebde351335cd13eb),
    SPH_C64(0x2be5ceb3ce56b32b), SPH_C64(0x2277553355443322),
    SPH_C64(0xd204d6bbd6bfbbd2), SPH_C64(0xa9399070904970a9),
    SPH_C64(0x07878089800e8907), SPH_C64(0x33c1f2a7f266a733),
    SPH_C64(0x2decc1b6c15ab62d), SPH_C64(0x3c5a66226678223c),
    SPH_C64(0x15b8ad92ad2a9215), SPH_C64(0xc9a96020608920c9),
    SPH_C64(0x875cdb49db154987), SPH_C64(0xaab01aff1a4fffaa),
    SPH_C64(0x50d8887888a07850), SPH_C64(0xa52b8e7a8e517aa5),
    SPH_C64(0x03898a8f8a068f03), SPH_C64(0x594a13f813b2f859),
    SPH_C64(0x09929b809b128009), SPH_C64(0x1a2339173934171a),
    SPH_C64(0x651075da75cada65), SPH_C64(0xd784533153b531d7),
    SPH_C64(0x84d551c65113c684), SPH_C64(0xd003d3b8d3bbb8d0),
    SPH_C64(0x82dc5ec35e1fc382), SPH_C64(0x29e2cbb0cb52b029),
    SPH_C64(0x5ac3997799b4775a), SPH_C64(0x1e2d3311333c111e),
    SPH_C64(0x7b3d46cb46f6cb7b), SPH_C64(0xa8b71ffc1f4bfca8),
    SPH_C64(0x6d0c61d661dad66d), SPH_C64(0x2c624e3a4e583a2c)
};

static void groestl512_compress(sph_groestl512_context *cc, const unsigned char *data)
{
    sph_u64 M[16], H[16], Q[16];
    int r;

    for (int i = 0; i < 16; i++) {
        M[i] = sph_dec64le(data + i * 8);
        H[i] = cc->H[i] ^ M[i];
    }

    /* P permutation */
    memcpy(Q, H, sizeof(Q));
    for (r = 0; r < 14; r++) {
        for (int i = 0; i < 16; i++)
            Q[i] ^= ((sph_u64)(i * 0x10) ^ ((sph_u64)r << 56));

        /* SubBytes, ShiftBytes, MixBytes (simplified) */
        sph_u64 T[16];
        for (int i = 0; i < 16; i++) {
            T[i] = GROESTL_T0[(unsigned char)Q[i]];
            for (int j = 1; j < 8; j++)
                T[i] ^= SPH_ROTL64(GROESTL_T0[(unsigned char)(Q[(i + j) % 16] >> (j * 8))], j * 8);
        }
        memcpy(Q, T, sizeof(Q));
    }

    /* Q permutation */
    sph_u64 R[16];
    memcpy(R, M, sizeof(R));
    for (r = 0; r < 14; r++) {
        for (int i = 0; i < 16; i++)
            R[i] ^= SPH_C64(0xFFFFFFFFFFFFFFFF) ^ ((sph_u64)(i * 0x10) ^ ((sph_u64)r << 56));

        sph_u64 T[16];
        for (int i = 0; i < 16; i++) {
            T[i] = GROESTL_T0[(unsigned char)R[i]];
            for (int j = 1; j < 8; j++)
                T[i] ^= SPH_ROTL64(GROESTL_T0[(unsigned char)(R[(i + j) % 16] >> (j * 8))], j * 8);
        }
        memcpy(R, T, sizeof(R));
    }

    for (int i = 0; i < 16; i++)
        cc->H[i] ^= Q[i] ^ R[i];
}

void sph_groestl512_init(sph_groestl512_context *cc)
{
    memset(cc->H, 0, sizeof(cc->H));
    cc->H[15] = SPH_C64(0x0002000000000000);
    cc->ptr = 0;
    cc->count = 0;
}

void sph_groestl512(sph_groestl512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            groestl512_compress(cc, cc->buf);
            cc->count++;
            cc->ptr = 0;
        }
    }
}

void sph_groestl512_close(sph_groestl512_context *cc, void *dst)
{
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 120) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        groestl512_compress(cc, cc->buf);
        cc->count++;
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 120 - cc->ptr);
    cc->count++;
    sph_enc64le(cc->buf + 120, cc->count);
    groestl512_compress(cc, cc->buf);

    /* Output transformation */
    sph_u64 H[16];
    memcpy(H, cc->H, sizeof(H));
    for (int r = 0; r < 14; r++) {
        for (int i = 0; i < 16; i++)
            H[i] ^= ((sph_u64)(i * 0x10) ^ ((sph_u64)r << 56));
        sph_u64 T[16];
        for (int i = 0; i < 16; i++) {
            T[i] = GROESTL_T0[(unsigned char)H[i]];
            for (int j = 1; j < 8; j++)
                T[i] ^= SPH_ROTL64(GROESTL_T0[(unsigned char)(H[(i + j) % 16] >> (j * 8))], j * 8);
        }
        memcpy(H, T, sizeof(H));
    }

    for (int i = 0; i < 16; i++)
        cc->H[i] ^= H[i];

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, cc->H[i + 8]);
}

/* ============================================================
 * JH-512
 * ============================================================ */

static const sph_u64 JH_IV512[16] = {
    SPH_C64(0x6fd14b963e00aa17), SPH_C64(0x636a2e057a15d543),
    SPH_C64(0x8a225e8d0c97ef0b), SPH_C64(0xe9341259f2b3c361),
    SPH_C64(0x891da0c1536f801e), SPH_C64(0x2aa9056bea2b6d80),
    SPH_C64(0x588eccdb2075baa6), SPH_C64(0xa90f3a76baf83bf7),
    SPH_C64(0x0169e60541e34a69), SPH_C64(0x46b58a8e2e6fe65a),
    SPH_C64(0x1047a7d0c1843c24), SPH_C64(0x3b6e71b12d5ac199),
    SPH_C64(0xcf57f6ec9db1f856), SPH_C64(0xa706887c5716b156),
    SPH_C64(0xe3c2fcdfe68517fb), SPH_C64(0x545a4678cc8cdd4b)
};

static void jh512_compress(sph_jh512_context *cc, const unsigned char *data)
{
    sph_u64 H[16], M[8];
    int i;

    memcpy(H, cc->H, sizeof(H));

    for (i = 0; i < 8; i++)
        M[i] = sph_dec64le(data + i * 8);

    /* XOR message with upper half of state */
    for (i = 0; i < 8; i++)
        H[i] ^= M[i];

    /* Simplified JH round function */
    for (int r = 0; r < 42; r++) {
        /* S-box layer */
        for (i = 0; i < 16; i += 2) {
            sph_u64 t = ~H[i];
            H[i] ^= H[i + 1] & t;
            H[i + 1] ^= H[i] & (~H[i + 1]);
        }
        /* Linear diffusion */
        for (i = 0; i < 8; i++) {
            sph_u64 t = H[i];
            H[i] = SPH_ROTL64(H[i + 8], 1) ^ H[i];
            H[i + 8] = SPH_ROTL64(t, 7);
        }
    }

    /* XOR message with lower half */
    for (i = 0; i < 8; i++)
        H[i + 8] ^= M[i];

    memcpy(cc->H, H, sizeof(cc->H));
}

void sph_jh512_init(sph_jh512_context *cc)
{
    memcpy(cc->H, JH_IV512, sizeof(cc->H));
    cc->ptr = 0;
    cc->block_count = 0;
}

void sph_jh512(sph_jh512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    while (len > 0) {
        size_t clen = 64 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 64) {
            jh512_compress(cc, cc->buf);
            cc->block_count++;
            cc->ptr = 0;
        }
    }
}

void sph_jh512_close(sph_jh512_context *cc, void *dst)
{
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 56) {
        memset(cc->buf + cc->ptr, 0, 64 - cc->ptr);
        jh512_compress(cc, cc->buf);
        cc->block_count++;
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 56 - cc->ptr);
    sph_u64 bc = (cc->block_count << 9) + ((cc->ptr - 1) << 3);
    sph_enc64be(cc->buf + 56, bc);
    jh512_compress(cc, cc->buf);

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, cc->H[i + 8]);
}

/* ============================================================
 * Keccak-512
 * ============================================================ */

static const sph_u64 KECCAK_RC[24] = {
    SPH_C64(0x0000000000000001), SPH_C64(0x0000000000008082),
    SPH_C64(0x800000000000808a), SPH_C64(0x8000000080008000),
    SPH_C64(0x000000000000808b), SPH_C64(0x0000000080000001),
    SPH_C64(0x8000000080008081), SPH_C64(0x8000000000008009),
    SPH_C64(0x000000000000008a), SPH_C64(0x0000000000000088),
    SPH_C64(0x0000000080008009), SPH_C64(0x000000008000000a),
    SPH_C64(0x000000008000808b), SPH_C64(0x800000000000008b),
    SPH_C64(0x8000000000008089), SPH_C64(0x8000000000008003),
    SPH_C64(0x8000000000008002), SPH_C64(0x8000000000000080),
    SPH_C64(0x000000000000800a), SPH_C64(0x800000008000000a),
    SPH_C64(0x8000000080008081), SPH_C64(0x8000000000008080),
    SPH_C64(0x0000000080000001), SPH_C64(0x8000000080008008)
};

static void keccak_f1600(sph_u64 *A)
{
    sph_u64 C[5], D[5], B[25];
    int r, x, y;

    for (r = 0; r < 24; r++) {
        /* Theta */
        for (x = 0; x < 5; x++)
            C[x] = A[x] ^ A[x + 5] ^ A[x + 10] ^ A[x + 15] ^ A[x + 20];
        for (x = 0; x < 5; x++) {
            D[x] = C[(x + 4) % 5] ^ SPH_ROTL64(C[(x + 1) % 5], 1);
            for (y = 0; y < 25; y += 5)
                A[y + x] ^= D[x];
        }
        /* Rho and Pi */
        B[0] = A[0];
        B[10] = SPH_ROTL64(A[1], 1);
        B[20] = SPH_ROTL64(A[2], 62);
        B[5] = SPH_ROTL64(A[3], 28);
        B[15] = SPH_ROTL64(A[4], 27);
        B[16] = SPH_ROTL64(A[5], 36);
        B[1] = SPH_ROTL64(A[6], 44);
        B[11] = SPH_ROTL64(A[7], 6);
        B[21] = SPH_ROTL64(A[8], 55);
        B[6] = SPH_ROTL64(A[9], 20);
        B[7] = SPH_ROTL64(A[10], 3);
        B[17] = SPH_ROTL64(A[11], 10);
        B[2] = SPH_ROTL64(A[12], 43);
        B[12] = SPH_ROTL64(A[13], 25);
        B[22] = SPH_ROTL64(A[14], 39);
        B[23] = SPH_ROTL64(A[15], 41);
        B[8] = SPH_ROTL64(A[16], 45);
        B[18] = SPH_ROTL64(A[17], 15);
        B[3] = SPH_ROTL64(A[18], 21);
        B[13] = SPH_ROTL64(A[19], 8);
        B[14] = SPH_ROTL64(A[20], 18);
        B[24] = SPH_ROTL64(A[21], 2);
        B[9] = SPH_ROTL64(A[22], 61);
        B[19] = SPH_ROTL64(A[23], 56);
        B[4] = SPH_ROTL64(A[24], 14);
        /* Chi */
        for (y = 0; y < 25; y += 5) {
            for (x = 0; x < 5; x++)
                A[y + x] = B[y + x] ^ ((~B[y + ((x + 1) % 5)]) & B[y + ((x + 2) % 5)]);
        }
        /* Iota */
        A[0] ^= KECCAK_RC[r];
    }
}

void sph_keccak512_init(sph_keccak512_context *cc)
{
    memset(cc->A, 0, sizeof(cc->A));
    cc->ptr = 0;
}

void sph_keccak512(sph_keccak512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    size_t rate = 72; /* 576 bits for Keccak-512 */

    while (len > 0) {
        size_t clen = rate - cc->ptr;
        if (clen > len) clen = len;
        for (size_t i = 0; i < clen; i++) {
            cc->A[(cc->ptr + i) / 8] ^= ((sph_u64)buf[i]) << (((cc->ptr + i) % 8) * 8);
        }
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == rate) {
            keccak_f1600(cc->A);
            cc->ptr = 0;
        }
    }
}

void sph_keccak512_close(sph_keccak512_context *cc, void *dst)
{
    size_t rate = 72;
    /* Padding: 0x01 || 0x00...0x00 || 0x80 */
    cc->A[cc->ptr / 8] ^= ((sph_u64)0x01) << ((cc->ptr % 8) * 8);
    cc->A[(rate - 1) / 8] ^= ((sph_u64)0x80) << (((rate - 1) % 8) * 8);
    keccak_f1600(cc->A);

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, cc->A[i]);
}

/* ============================================================
 * Skein-512
 * ============================================================ */

static const sph_u64 SKEIN_IV512[8] = {
    SPH_C64(0x4903ADFF749C51CE), SPH_C64(0x0D95DE399746DF03),
    SPH_C64(0x8FD1934127C79BCE), SPH_C64(0x9A255629FF352CB1),
    SPH_C64(0x5DB62599DF6CA7B0), SPH_C64(0xEABE394CA9D5C3F4),
    SPH_C64(0x991112C71A75B523), SPH_C64(0xAE18A40B660FCC33)
};

#define SKEIN_KS_PARITY SPH_C64(0x1BD11BDAA9FC1A22)

static void skein512_process(sph_skein512_context *cc, const unsigned char *data, sph_u64 tweak0, sph_u64 tweak1)
{
    sph_u64 M[8], K[9], T[3];

    for (int i = 0; i < 8; i++)
        M[i] = sph_dec64le(data + i * 8);

    memcpy(K, cc->h, 64);
    K[8] = SKEIN_KS_PARITY;
    for (int i = 0; i < 8; i++)
        K[8] ^= K[i];

    T[0] = tweak0;
    T[1] = tweak1;
    T[2] = T[0] ^ T[1];

    sph_u64 X[8];
    for (int i = 0; i < 8; i++)
        X[i] = M[i] + K[i];
    X[5] += T[0];
    X[6] += T[1];

    /* Simplified Threefish-512 rounds */
    for (int r = 0; r < 72; r += 8) {
        X[0] += X[1]; X[1] = SPH_ROTL64(X[1], 46) ^ X[0];
        X[2] += X[3]; X[3] = SPH_ROTL64(X[3], 36) ^ X[2];
        X[4] += X[5]; X[5] = SPH_ROTL64(X[5], 19) ^ X[4];
        X[6] += X[7]; X[7] = SPH_ROTL64(X[7], 37) ^ X[6];

        X[2] += X[1]; X[1] = SPH_ROTL64(X[1], 33) ^ X[2];
        X[4] += X[7]; X[7] = SPH_ROTL64(X[7], 27) ^ X[4];
        X[6] += X[5]; X[5] = SPH_ROTL64(X[5], 14) ^ X[6];
        X[0] += X[3]; X[3] = SPH_ROTL64(X[3], 42) ^ X[0];

        X[4] += X[1]; X[1] = SPH_ROTL64(X[1], 17) ^ X[4];
        X[6] += X[3]; X[3] = SPH_ROTL64(X[3], 49) ^ X[6];
        X[0] += X[5]; X[5] = SPH_ROTL64(X[5], 36) ^ X[0];
        X[2] += X[7]; X[7] = SPH_ROTL64(X[7], 39) ^ X[2];

        X[6] += X[1]; X[1] = SPH_ROTL64(X[1], 44) ^ X[6];
        X[0] += X[7]; X[7] = SPH_ROTL64(X[7], 9) ^ X[0];
        X[2] += X[5]; X[5] = SPH_ROTL64(X[5], 54) ^ X[2];
        X[4] += X[3]; X[3] = SPH_ROTL64(X[3], 56) ^ X[4];

        /* Key injection */
        int s = r / 4 + 1;
        for (int i = 0; i < 8; i++)
            X[i] += K[(s + i) % 9];
        X[5] += T[s % 3];
        X[6] += T[(s + 1) % 3];
        X[7] += (sph_u64)s;
    }

    for (int i = 0; i < 8; i++)
        cc->h[i] = X[i] ^ M[i];
}

void sph_skein512_init(sph_skein512_context *cc)
{
    memcpy(cc->h, SKEIN_IV512, sizeof(cc->h));
    cc->ptr = 0;
    cc->bcount = 0;
}

void sph_skein512(sph_skein512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    while (len > 0) {
        size_t clen = 64 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 64) {
            sph_u64 t0 = (cc->bcount + 1) * 64;
            sph_u64 t1 = (cc->bcount == 0) ? SPH_C64(0x7000000000000000) : SPH_C64(0x3000000000000000);
            skein512_process(cc, cc->buf, t0, t1);
            cc->bcount++;
            cc->ptr = 0;
        }
    }
}

void sph_skein512_close(sph_skein512_context *cc, void *dst)
{
    memset(cc->buf + cc->ptr, 0, 64 - cc->ptr);
    sph_u64 t0 = cc->bcount * 64 + cc->ptr;
    sph_u64 t1 = (cc->bcount == 0) ? SPH_C64(0xF000000000000000) : SPH_C64(0xB000000000000000);
    skein512_process(cc, cc->buf, t0, t1);

    /* Output */
    memset(cc->buf, 0, 64);
    skein512_process(cc, cc->buf, 8, SPH_C64(0xFF00000000000000));

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, cc->h[i]);
}

/* ============================================================
 * Luffa-512
 * ============================================================ */

static const sph_u32 LUFFA_IV[5][8] = {
    { 0x6d251e69, 0x44b051e0, 0x4eaa6fb4, 0xdbf78465,
      0x6e292011, 0x90152df4, 0xee058139, 0xdef610bb },
    { 0xc3b44b95, 0xd9d2f256, 0x70eee9a0, 0xde099fa3,
      0x5d9b0557, 0x8fc944b3, 0xcf1ccf0e, 0x746cd581 },
    { 0xf7efc89d, 0x5dba5781, 0x04016ce5, 0xad659c05,
      0x0306194f, 0x666d1836, 0x24aa230a, 0x8b264ae7 },
    { 0x858075d5, 0x36d79cce, 0xe571f7d7, 0x204b1f67,
      0x35870c6a, 0x57e9e923, 0x14bcb808, 0x7cde72ce },
    { 0x6c68e9be, 0x5ec41e22, 0xc825b7c7, 0xaffb4363,
      0xf5df3999, 0x0fc688f1, 0xb07224cc, 0x03e86cea }
};

static void luffa512_round(sph_u32 V[5][8], const sph_u32 M[8])
{
    int i, j;
    sph_u32 T[8];

    /* MI - Message Injection */
    for (i = 0; i < 8; i++)
        T[i] = M[i];

    for (j = 0; j < 5; j++) {
        for (i = 0; i < 8; i++)
            V[j][i] ^= T[i];
        /* Simplified step function */
        for (i = 0; i < 8; i++) {
            sph_u32 t = V[j][i];
            V[j][i] = SPH_ROTL32(V[j][(i + 1) % 8], 1) ^ (V[j][(i + 2) % 8] & V[j][(i + 3) % 8]);
            V[j][(i + 4) % 8] ^= t;
        }
        /* Update T for next chain */
        for (i = 0; i < 8; i++)
            T[i] = SPH_ROTL32(T[i], 1) ^ T[(i + 1) % 8];
    }
}

void sph_luffa512_init(sph_luffa512_context *cc)
{
    memcpy(cc->V, LUFFA_IV, sizeof(cc->V));
    cc->ptr = 0;
}

void sph_luffa512(sph_luffa512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;
    sph_u32 M[8];

    while (len > 0) {
        size_t clen = 32 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 32) {
            for (int i = 0; i < 8; i++)
                M[i] = sph_dec32be(cc->buf + i * 4);
            luffa512_round(cc->V, M);
            cc->ptr = 0;
        }
    }
}

void sph_luffa512_close(sph_luffa512_context *cc, void *dst)
{
    sph_u32 M[8];
    unsigned char pad[32];

    memset(pad, 0, 32);
    pad[0] = 0x80;
    sph_luffa512(cc, pad, 32 - cc->ptr);

    /* Finalization */
    memset(M, 0, sizeof(M));
    luffa512_round(cc->V, M);
    luffa512_round(cc->V, M);

    /* Output */
    for (int j = 0; j < 4; j++) {
        for (int i = 0; i < 2; i++)
            sph_enc32be((unsigned char *)dst + j * 16 + i * 4, cc->V[j][i]);
        for (int i = 0; i < 2; i++)
            sph_enc32be((unsigned char *)dst + j * 16 + 8 + i * 4, cc->V[j][i + 4]);
    }
}

/* ============================================================
 * Cubehash-512
 * ============================================================ */

static void cubehash_round(sph_u32 *x)
{
    int i;
    sph_u32 y[16];

    for (i = 0; i < 16; i++)
        x[i + 16] += x[i];
    for (i = 0; i < 16; i++)
        y[i ^ 8] = x[i];
    for (i = 0; i < 16; i++)
        x[i] = SPH_ROTL32(y[i], 7);
    for (i = 0; i < 16; i++)
        x[i] ^= x[i + 16];
    for (i = 0; i < 16; i++)
        y[i ^ 2] = x[i + 16];
    for (i = 0; i < 16; i++)
        x[i + 16] = y[i];
    for (i = 0; i < 16; i++)
        x[i + 16] += x[i];
    for (i = 0; i < 16; i++)
        y[i ^ 4] = x[i];
    for (i = 0; i < 16; i++)
        x[i] = SPH_ROTL32(y[i], 11);
    for (i = 0; i < 16; i++)
        x[i] ^= x[i + 16];
    for (i = 0; i < 16; i++)
        y[i ^ 1] = x[i + 16];
    for (i = 0; i < 16; i++)
        x[i + 16] = y[i];
}

static const sph_u32 CUBEHASH_IV512[32] = {
    0x2AEA2A61, 0x50F494D4, 0x2D538B8B, 0x4167D83E,
    0x3FEE2313, 0xC701CF8C, 0xCC39968E, 0x50AC5695,
    0x4D42C787, 0xA647A8B3, 0x97CF0BEF, 0x825B4537,
    0xEEF864D2, 0xF22090C4, 0xD0E5CD33, 0xA23911AE,
    0xFCD398D9, 0x148FE485, 0x1B017BEF, 0xB6444532,
    0x6A536159, 0x2FF5781C, 0x91FA7934, 0x0DBADEA9,
    0xD65C8A2B, 0xA5A70E75, 0xB1C62456, 0xBC796576,
    0x1921C8F7, 0xE7989AF1, 0x7795D246, 0xD43E3B44
};

void sph_cubehash512_init(sph_cubehash512_context *cc)
{
    memcpy(cc->state, CUBEHASH_IV512, sizeof(cc->state));
    cc->ptr = 0;
}

void sph_cubehash512(sph_cubehash512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;

    while (len > 0) {
        size_t clen = 32 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 32) {
            for (int i = 0; i < 8; i++)
                cc->state[i] ^= sph_dec32le(cc->buf + i * 4);
            for (int i = 0; i < 16; i++)
                cubehash_round(cc->state);
            cc->ptr = 0;
        }
    }
}

void sph_cubehash512_close(sph_cubehash512_context *cc, void *dst)
{
    cc->buf[cc->ptr++] = 0x80;
    memset(cc->buf + cc->ptr, 0, 32 - cc->ptr);
    for (int i = 0; i < 8; i++)
        cc->state[i] ^= sph_dec32le(cc->buf + i * 4);
    for (int i = 0; i < 16; i++)
        cubehash_round(cc->state);
    cc->state[31] ^= 1;
    for (int i = 0; i < 32; i++)
        cubehash_round(cc->state);

    for (int i = 0; i < 16; i++)
        sph_enc32le((unsigned char *)dst + i * 4, cc->state[i]);
}

/* ============================================================
 * Shavite-512
 * ============================================================ */

static const sph_u32 SHAVITE_IV512[16] = {
    0x72FCCDD8, 0x79CA4727, 0x128A077B, 0x40D55AEC,
    0xD1901A06, 0x430AE307, 0xB29F5CD1, 0xDF07FBFC,
    0x8E45D73D, 0x681AB538, 0xBDE86578, 0xDD577E47,
    0xE275EADE, 0x502D9FCD, 0xB9357178, 0x022A4B9A
};

static void shavite512_compress(sph_shavite512_context *cc, const unsigned char *data)
{
    sph_u32 M[32], P[16], Q[16];
    int i;

    for (i = 0; i < 32; i++)
        M[i] = sph_dec32le(data + i * 4);

    memcpy(P, cc->h, sizeof(P));
    memcpy(Q, cc->h, sizeof(Q));

    /* Simplified Shavite round */
    for (int r = 0; r < 14; r++) {
        for (i = 0; i < 16; i++) {
            P[i] ^= M[i % 32];
            P[i] = SPH_ROTL32(P[i], 7) + P[(i + 1) % 16];
        }
        for (i = 0; i < 16; i++) {
            Q[i] ^= M[(i + 16) % 32];
            Q[i] = SPH_ROTL32(Q[i], 11) + Q[(i + 1) % 16];
        }
        for (i = 0; i < 16; i++)
            P[i] ^= Q[(16 - i) % 16];
    }

    for (i = 0; i < 16; i++)
        cc->h[i] ^= P[i] ^ Q[i];
}

void sph_shavite512_init(sph_shavite512_context *cc)
{
    memcpy(cc->h, SHAVITE_IV512, sizeof(cc->h));
    cc->ptr = 0;
    cc->count0 = cc->count1 = cc->count2 = cc->count3 = 0;
}

void sph_shavite512(sph_shavite512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;

    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            cc->count0 += 1024;
            if (cc->count0 < 1024) cc->count1++;
            shavite512_compress(cc, cc->buf);
            cc->ptr = 0;
        }
    }
}

void sph_shavite512_close(sph_shavite512_context *cc, void *dst)
{
    cc->count0 += cc->ptr * 8;
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 110) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        shavite512_compress(cc, cc->buf);
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 110 - cc->ptr);
    sph_enc32le(cc->buf + 110, cc->count0);
    sph_enc32le(cc->buf + 114, cc->count1);
    sph_enc32le(cc->buf + 118, cc->count2);
    sph_enc32le(cc->buf + 122, cc->count3);
    cc->buf[126] = 0;
    cc->buf[127] = 0;
    shavite512_compress(cc, cc->buf);

    for (int i = 0; i < 16; i++)
        sph_enc32le((unsigned char *)dst + i * 4, cc->h[i]);
}

/* ============================================================
 * SIMD-512
 * ============================================================ */

static const sph_u32 SIMD_IV512[32] = {
    0x0BA16B95, 0x72F999AD, 0x9FECC2AE, 0xBA3264FC,
    0x5E894929, 0x8E9F30E5, 0x2F1DAA37, 0xF0F2C558,
    0xAC506643, 0xA90635A5, 0xE25B878B, 0xAAB7878F,
    0x88817F7A, 0x0A02892B, 0x559A7550, 0x598F657E,
    0x7EEF60A1, 0x6B70E3E8, 0x9C1714D1, 0xB958E2A8,
    0xAB02675E, 0xED1C014F, 0xCD8D65BB, 0xFDB7A257,
    0x09254899, 0xD699C7BC, 0x9019B6DC, 0x2B9022E4,
    0x8FA14956, 0x21BF9BD3, 0xB94D0943, 0x6FFDDC22
};

static void simd512_compress(sph_simd512_context *cc, const unsigned char *data)
{
    sph_u32 M[32], A[32];
    int i;

    for (i = 0; i < 32; i++)
        M[i] = sph_dec32le(data + i * 4);

    memcpy(A, cc->state, sizeof(A));

    /* Simplified SIMD round */
    for (int r = 0; r < 4; r++) {
        for (i = 0; i < 32; i++) {
            A[i] += M[i % 32];
            A[i] = SPH_ROTL32(A[i], 13);
            A[i] ^= A[(i + 1) % 32];
        }
    }

    for (i = 0; i < 32; i++)
        cc->state[i] ^= A[i] ^ M[i];
}

void sph_simd512_init(sph_simd512_context *cc)
{
    memcpy(cc->state, SIMD_IV512, sizeof(cc->state));
    cc->ptr = 0;
    cc->count_low = cc->count_high = 0;
}

void sph_simd512(sph_simd512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;

    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            cc->count_low += 1024;
            if (cc->count_low < 1024) cc->count_high++;
            simd512_compress(cc, cc->buf);
            cc->ptr = 0;
        }
    }
}

void sph_simd512_close(sph_simd512_context *cc, void *dst)
{
    cc->count_low += cc->ptr * 8;
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 120) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        simd512_compress(cc, cc->buf);
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 120 - cc->ptr);
    sph_enc32le(cc->buf + 120, cc->count_low);
    sph_enc32le(cc->buf + 124, cc->count_high);
    simd512_compress(cc, cc->buf);

    for (int i = 0; i < 16; i++)
        sph_enc32le((unsigned char *)dst + i * 4, cc->state[i]);
}

/* ============================================================
 * Echo-512
 * ============================================================ */

static const sph_u64 ECHO_IV512[16] = {
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000),
    SPH_C64(0x0000000000000200), SPH_C64(0x0000000000000000)
};

static void echo512_compress(sph_echo512_context *cc, const unsigned char *data)
{
    sph_u64 W[16], K[16];
    int i;

    for (i = 0; i < 16; i++)
        W[i] = sph_dec64le(data + i * 8);

    memcpy(K, cc->state, sizeof(K));

    /* Simplified Echo round */
    for (int r = 0; r < 10; r++) {
        for (i = 0; i < 16; i++) {
            W[i] ^= K[i];
            W[i] = SPH_ROTL64(W[i], 13) + W[(i + 1) % 16];
            K[i] = SPH_ROTL64(K[i], 29) ^ W[i];
        }
    }

    for (i = 0; i < 16; i++)
        cc->state[i] ^= W[i];
}

void sph_echo512_init(sph_echo512_context *cc)
{
    memcpy(cc->state, ECHO_IV512, sizeof(cc->state));
    cc->ptr = 0;
    cc->C[0] = cc->C[1] = 0;
}

void sph_echo512(sph_echo512_context *cc, const void *data, size_t len)
{
    const unsigned char *buf = data;

    while (len > 0) {
        size_t clen = 128 - cc->ptr;
        if (clen > len) clen = len;
        memcpy(cc->buf + cc->ptr, buf, clen);
        cc->ptr += clen;
        buf += clen;
        len -= clen;
        if (cc->ptr == 128) {
            cc->C[0] += 1024;
            if (cc->C[0] < 1024) cc->C[1]++;
            echo512_compress(cc, cc->buf);
            cc->ptr = 0;
        }
    }
}

void sph_echo512_close(sph_echo512_context *cc, void *dst)
{
    cc->C[0] += cc->ptr * 8;
    cc->buf[cc->ptr++] = 0x80;
    if (cc->ptr > 112) {
        memset(cc->buf + cc->ptr, 0, 128 - cc->ptr);
        echo512_compress(cc, cc->buf);
        cc->ptr = 0;
    }
    memset(cc->buf + cc->ptr, 0, 112 - cc->ptr);
    sph_enc64le(cc->buf + 112, cc->C[0]);
    sph_enc64le(cc->buf + 120, cc->C[1]);
    echo512_compress(cc, cc->buf);

    for (int i = 0; i < 8; i++)
        sph_enc64le((unsigned char *)dst + i * 8, cc->state[i]);
}

/* ============================================================
 * X11 Combined Hash
 * ============================================================ */

void x11_hash(const void *input, size_t len, void *output)
{
    unsigned char hash[64];

    sph_blake512_context blake;
    sph_bmw512_context bmw;
    sph_groestl512_context groestl;
    sph_jh512_context jh;
    sph_keccak512_context keccak;
    sph_skein512_context skein;
    sph_luffa512_context luffa;
    sph_cubehash512_context cubehash;
    sph_shavite512_context shavite;
    sph_simd512_context simd;
    sph_echo512_context echo;

    /* 1. Blake */
    sph_blake512_init(&blake);
    sph_blake512(&blake, input, len);
    sph_blake512_close(&blake, hash);

    /* 2. BMW */
    sph_bmw512_init(&bmw);
    sph_bmw512(&bmw, hash, 64);
    sph_bmw512_close(&bmw, hash);

    /* 3. Groestl */
    sph_groestl512_init(&groestl);
    sph_groestl512(&groestl, hash, 64);
    sph_groestl512_close(&groestl, hash);

    /* 4. JH */
    sph_jh512_init(&jh);
    sph_jh512(&jh, hash, 64);
    sph_jh512_close(&jh, hash);

    /* 5. Keccak */
    sph_keccak512_init(&keccak);
    sph_keccak512(&keccak, hash, 64);
    sph_keccak512_close(&keccak, hash);

    /* 6. Skein */
    sph_skein512_init(&skein);
    sph_skein512(&skein, hash, 64);
    sph_skein512_close(&skein, hash);

    /* 7. Luffa */
    sph_luffa512_init(&luffa);
    sph_luffa512(&luffa, hash, 64);
    sph_luffa512_close(&luffa, hash);

    /* 8. Cubehash */
    sph_cubehash512_init(&cubehash);
    sph_cubehash512(&cubehash, hash, 64);
    sph_cubehash512_close(&cubehash, hash);

    /* 9. Shavite */
    sph_shavite512_init(&shavite);
    sph_shavite512(&shavite, hash, 64);
    sph_shavite512_close(&shavite, hash);

    /* 10. SIMD */
    sph_simd512_init(&simd);
    sph_simd512(&simd, hash, 64);
    sph_simd512_close(&simd, hash);

    /* 11. Echo */
    sph_echo512_init(&echo);
    sph_echo512(&echo, hash, 64);
    sph_echo512_close(&echo, hash);

    /* Output is first 32 bytes (256 bits) */
    memcpy(output, hash, 32);
}
