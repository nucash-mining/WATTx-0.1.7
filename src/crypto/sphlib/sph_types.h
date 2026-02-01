/* $Id: sph_types.h 260 2011-07-21 01:02:38Z tp $ */
/**
 * Basic type definitions.
 *
 * This header file defines the generic integer types that will be used
 * for the implementation of hash functions; it also contains helper
 * functions which encode and decode multi-byte integer values, using
 * either little-endian or big-endian conventions.
 *
 * This file contains a compile-time test on the size of a byte
 * (the <code>unsigned char</code> C type). If bytes are not octets,
 * i.e. if they do not have a size of exactly 8 bits, then compilation
 * is aborted. Architectures where bytes are not octets are relatively
 * rare, even in the embedded devices market. We forbid non-octet bytes
 * because there is no really portable way to handle them.
 *
 * ==========================(LICENSE BEGIN)============================
 *
 * Copyright (c) 2007-2010  Projet RNRT SAPHIR
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
 * IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
 * CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
 * TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
 * SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * ===========================(LICENSE END)=============================
 *
 * @file     sph_types.h
 * @author   Thomas Pornin &lt;thomas.pornin@cryptolog.com&gt;
 */

#ifndef SPH_TYPES_H__
#define SPH_TYPES_H__

#include <limits.h>
#include <stdint.h>

/*
 * All our I/O functions are defined over octet streams. We do not know
 * how to handle input data if bytes are not octets.
 */
#if CHAR_BIT != 8
#error This code requires 8-bit bytes
#endif

/* ============== Detect endianness ============== */

/*
 * On most GCC-compatible compilers, we can use __BYTE_ORDER__ to detect
 * endianness at compile time.
 */
#if defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && \
    defined(__ORDER_BIG_ENDIAN__)

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define SPH_LITTLE_ENDIAN  1
#define SPH_BIG_ENDIAN     0
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SPH_LITTLE_ENDIAN  0
#define SPH_BIG_ENDIAN     1
#endif

#elif defined(__LITTLE_ENDIAN__) || defined(__i386__) || defined(__x86_64__) || \
      defined(_M_IX86) || defined(_M_X64) || defined(_M_AMD64) || \
      defined(__ARMEL__) || defined(__AARCH64EL__)
#define SPH_LITTLE_ENDIAN  1
#define SPH_BIG_ENDIAN     0

#elif defined(__BIG_ENDIAN__) || defined(__ARMEB__) || defined(__AARCH64EB__)
#define SPH_LITTLE_ENDIAN  0
#define SPH_BIG_ENDIAN     1

#else
/* Default to little-endian */
#define SPH_LITTLE_ENDIAN  1
#define SPH_BIG_ENDIAN     0
#endif

/* ============== Integer types ============== */

typedef uint32_t sph_u32;
typedef int32_t sph_s32;
typedef uint64_t sph_u64;
typedef int64_t sph_s64;

#define SPH_C32(x)    ((sph_u32)(x##UL))
#define SPH_C64(x)    ((sph_u64)(x##ULL))

#define SPH_T32(x)    ((x) & SPH_C32(0xFFFFFFFF))
#define SPH_T64(x)    ((x) & SPH_C64(0xFFFFFFFFFFFFFFFF))

#define SPH_ROTL32(x, n)   SPH_T32(((x) << (n)) | ((x) >> (32 - (n))))
#define SPH_ROTR32(x, n)   SPH_ROTL32(x, (32 - (n)))
#define SPH_ROTL64(x, n)   SPH_T64(((x) << (n)) | ((x) >> (64 - (n))))
#define SPH_ROTR64(x, n)   SPH_ROTL64(x, (64 - (n)))

/* ============== Byte swap ============== */

#if defined(__GNUC__) && (__GNUC__ > 4 || (__GNUC__ == 4 && __GNUC_MINOR__ >= 3))
#define SPH_BSWAP32(x)    __builtin_bswap32(x)
#define SPH_BSWAP64(x)    __builtin_bswap64(x)
#else
static inline sph_u32 sph_bswap32(sph_u32 x)
{
    x = ((x & SPH_C32(0x00FF00FF)) << 8)
      | ((x & SPH_C32(0xFF00FF00)) >> 8);
    return (x << 16) | (x >> 16);
}
#define SPH_BSWAP32(x)    sph_bswap32(x)

static inline sph_u64 sph_bswap64(sph_u64 x)
{
    x = ((x & SPH_C64(0x00FF00FF00FF00FF)) << 8)
      | ((x & SPH_C64(0xFF00FF00FF00FF00)) >> 8);
    x = ((x & SPH_C64(0x0000FFFF0000FFFF)) << 16)
      | ((x & SPH_C64(0xFFFF0000FFFF0000)) >> 16);
    return (x << 32) | (x >> 32);
}
#define SPH_BSWAP64(x)    sph_bswap64(x)
#endif

/* ============== Encode/Decode functions ============== */

static inline void
sph_enc16be(void *dst, unsigned val)
{
    ((unsigned char *)dst)[0] = (val >> 8);
    ((unsigned char *)dst)[1] = val;
}

static inline unsigned
sph_dec16be(const void *src)
{
    return ((unsigned)(((const unsigned char *)src)[0]) << 8)
        | (unsigned)(((const unsigned char *)src)[1]);
}

static inline void
sph_enc16le(void *dst, unsigned val)
{
    ((unsigned char *)dst)[0] = val;
    ((unsigned char *)dst)[1] = (val >> 8);
}

static inline unsigned
sph_dec16le(const void *src)
{
    return (unsigned)(((const unsigned char *)src)[0])
        | ((unsigned)(((const unsigned char *)src)[1]) << 8);
}

static inline void
sph_enc32be(void *dst, sph_u32 val)
{
    ((unsigned char *)dst)[0] = (val >> 24);
    ((unsigned char *)dst)[1] = (val >> 16);
    ((unsigned char *)dst)[2] = (val >> 8);
    ((unsigned char *)dst)[3] = val;
}

static inline void
sph_enc32be_aligned(void *dst, sph_u32 val)
{
#if SPH_LITTLE_ENDIAN
    *(sph_u32 *)dst = SPH_BSWAP32(val);
#else
    *(sph_u32 *)dst = val;
#endif
}

static inline sph_u32
sph_dec32be(const void *src)
{
    return ((sph_u32)(((const unsigned char *)src)[0]) << 24)
        | ((sph_u32)(((const unsigned char *)src)[1]) << 16)
        | ((sph_u32)(((const unsigned char *)src)[2]) << 8)
        | (sph_u32)(((const unsigned char *)src)[3]);
}

static inline sph_u32
sph_dec32be_aligned(const void *src)
{
#if SPH_LITTLE_ENDIAN
    return SPH_BSWAP32(*(const sph_u32 *)src);
#else
    return *(const sph_u32 *)src;
#endif
}

static inline void
sph_enc32le(void *dst, sph_u32 val)
{
    ((unsigned char *)dst)[0] = val;
    ((unsigned char *)dst)[1] = (val >> 8);
    ((unsigned char *)dst)[2] = (val >> 16);
    ((unsigned char *)dst)[3] = (val >> 24);
}

static inline void
sph_enc32le_aligned(void *dst, sph_u32 val)
{
#if SPH_LITTLE_ENDIAN
    *(sph_u32 *)dst = val;
#else
    *(sph_u32 *)dst = SPH_BSWAP32(val);
#endif
}

static inline sph_u32
sph_dec32le(const void *src)
{
    return (sph_u32)(((const unsigned char *)src)[0])
        | ((sph_u32)(((const unsigned char *)src)[1]) << 8)
        | ((sph_u32)(((const unsigned char *)src)[2]) << 16)
        | ((sph_u32)(((const unsigned char *)src)[3]) << 24);
}

static inline sph_u32
sph_dec32le_aligned(const void *src)
{
#if SPH_LITTLE_ENDIAN
    return *(const sph_u32 *)src;
#else
    return SPH_BSWAP32(*(const sph_u32 *)src);
#endif
}

static inline void
sph_enc64be(void *dst, sph_u64 val)
{
    ((unsigned char *)dst)[0] = (val >> 56);
    ((unsigned char *)dst)[1] = (val >> 48);
    ((unsigned char *)dst)[2] = (val >> 40);
    ((unsigned char *)dst)[3] = (val >> 32);
    ((unsigned char *)dst)[4] = (val >> 24);
    ((unsigned char *)dst)[5] = (val >> 16);
    ((unsigned char *)dst)[6] = (val >> 8);
    ((unsigned char *)dst)[7] = val;
}

static inline void
sph_enc64be_aligned(void *dst, sph_u64 val)
{
#if SPH_LITTLE_ENDIAN
    *(sph_u64 *)dst = SPH_BSWAP64(val);
#else
    *(sph_u64 *)dst = val;
#endif
}

static inline sph_u64
sph_dec64be(const void *src)
{
    return ((sph_u64)(((const unsigned char *)src)[0]) << 56)
        | ((sph_u64)(((const unsigned char *)src)[1]) << 48)
        | ((sph_u64)(((const unsigned char *)src)[2]) << 40)
        | ((sph_u64)(((const unsigned char *)src)[3]) << 32)
        | ((sph_u64)(((const unsigned char *)src)[4]) << 24)
        | ((sph_u64)(((const unsigned char *)src)[5]) << 16)
        | ((sph_u64)(((const unsigned char *)src)[6]) << 8)
        | (sph_u64)(((const unsigned char *)src)[7]);
}

static inline sph_u64
sph_dec64be_aligned(const void *src)
{
#if SPH_LITTLE_ENDIAN
    return SPH_BSWAP64(*(const sph_u64 *)src);
#else
    return *(const sph_u64 *)src;
#endif
}

static inline void
sph_enc64le(void *dst, sph_u64 val)
{
    ((unsigned char *)dst)[0] = val;
    ((unsigned char *)dst)[1] = (val >> 8);
    ((unsigned char *)dst)[2] = (val >> 16);
    ((unsigned char *)dst)[3] = (val >> 24);
    ((unsigned char *)dst)[4] = (val >> 32);
    ((unsigned char *)dst)[5] = (val >> 40);
    ((unsigned char *)dst)[6] = (val >> 48);
    ((unsigned char *)dst)[7] = (val >> 56);
}

static inline void
sph_enc64le_aligned(void *dst, sph_u64 val)
{
#if SPH_LITTLE_ENDIAN
    *(sph_u64 *)dst = val;
#else
    *(sph_u64 *)dst = SPH_BSWAP64(val);
#endif
}

static inline sph_u64
sph_dec64le(const void *src)
{
    return (sph_u64)(((const unsigned char *)src)[0])
        | ((sph_u64)(((const unsigned char *)src)[1]) << 8)
        | ((sph_u64)(((const unsigned char *)src)[2]) << 16)
        | ((sph_u64)(((const unsigned char *)src)[3]) << 24)
        | ((sph_u64)(((const unsigned char *)src)[4]) << 32)
        | ((sph_u64)(((const unsigned char *)src)[5]) << 40)
        | ((sph_u64)(((const unsigned char *)src)[6]) << 48)
        | ((sph_u64)(((const unsigned char *)src)[7]) << 56);
}

static inline sph_u64
sph_dec64le_aligned(const void *src)
{
#if SPH_LITTLE_ENDIAN
    return *(const sph_u64 *)src;
#else
    return SPH_BSWAP64(*(const sph_u64 *)src);
#endif
}

#endif /* SPH_TYPES_H__ */
