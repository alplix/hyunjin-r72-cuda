/*
 * =============================================================================
 * moo521_codec_test.cpp
 *
 * Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
 *
 * Coded by Alperen Yavuz
 *
 * Standalone self-test for the Moo! Wrapper RC5-72 buffer codec
 * (common/moo521.cpp).  Verifies, against a captured real 176-byte Moo
 * per-workunit packet:
 *
 *   1. header layout (magic + big-endian version/count),
 *   2. decode -> the RC5-72 plain/cypher/L0 fields are present,
 *   3. checksum invariant: bswap32(decoded dword 42) == moo521_checksum(body),
 *   4. round-trip: moo521_encode(moo521_decode(pkt)) == pkt (bytes 0..167).
 *
 * Build (no dnetc tree needed, but needs the moo521 sources and the
 * cputypes.h include path with OS/CPU defines for GCC/Clang):
 *
 *   g++ -O2 -DCLIENT_OS=1 -DCLIENT_CPU=1 \
 *       -I<dnetc-client-base>/common \
 *       test/moo521_codec_test.cpp common/moo521.cpp -o moo521_codec_test
 *   ./moo521_codec_test
 *
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 * =============================================================================
 */
#ifndef HAVE_CRYPTO_V2
#define HAVE_CRYPTO_V2 1
#endif
#include "moo521.h"
#include <stdio.h>
#include <string.h>
#include <stdint.h>

/* Captured real Moo! Wrapper `in.r72` packet #0 (176 bytes). */
static const uint8_t PACKET0[MOO521_RECLEN] = {
0xB1,0x9B,0x80,0x1F,0x73,0x90,0xCC,0x23,0x41,0xD8,0xDD,0xC0,0xBB,0x6C,0xE2,0x5D,
0x1D,0x80,0x8D,0xB9,0xB3,0xCD,0x7B,0x64,0x82,0xB0,0xEF,0xE8,0x49,0x0E,0x33,0x00,
0x88,0xDD,0x4E,0x0B,0x3A,0xAF,0xA4,0x0B,0x82,0xCE,0x52,0xA8,0xCA,0xED,0x01,0x85,
0x13,0x0B,0xAF,0xE2,0x5B,0x2A,0x5B,0x3F,0xA3,0x49,0x0D,0x1C,0xEB,0x67,0xBB,0xB9,
0x33,0x86,0x6A,0x56,0x7B,0xA5,0x18,0xF3,0xC3,0xC3,0xC7,0x90,0x0B,0xE2,0x76,0x2D,
0x54,0x01,0x25,0x34,0x2D,0xC2,0xE6,0xA7,0xC0,0xA5,0xF4,0xF8,0x9A,0x84,0x4E,0x56,
0x40,0x58,0x7B,0x64,0x0E,0x3D,0xD9,0xDB,0x04,0xB9,0x3C,0x78,0x4C,0xD7,0xEB,0x15,
0x94,0xF6,0x99,0xB2,0xDD,0x15,0x48,0x4F,0x25,0x33,0xF6,0xEC,0x6D,0x52,0xA5,0x89,
0xB5,0x71,0x54,0x26,0xFD,0x90,0x02,0xC3,0x45,0xAE,0xB1,0x60,0x8D,0xCD,0x5F,0xFD,
0xD5,0xEC,0x0E,0x9A,0x1E,0x0A,0xBD,0x34,0x66,0x29,0x6B,0xD4,0xAE,0x48,0x1A,0x71,
0xF6,0x66,0xC9,0x0E,0x3E,0x85,0x77,0xAB,0xC3,0x51,0x92,0xD5,0xEC,0x9B,0xF9,0x32
};

static u32 bswap32_self(u32 x){
  return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8)
       | ((x & 0x00FF0000u) >> 8)  | ((x & 0xFF000000u) >> 24);
}

static u32 rd32le(const u8 *p){ u32 d; memcpy(&d,p,4); return d; }

static int g_fail = 0;
static void check(int ok, const char *what){
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if(!ok) g_fail = 1;
}

int main(void){
  printf("Moo521 codec self-test\n");
  printf("======================\n");

  u8 body[168], re[MOO521_RECLEN];

  /* 1. decode the embedded packet */
  moo521_decode(PACKET0, body);
  check(1, "decode ran");

  /* RC5-72 fields present: plain word-value + RC5 test cypher, L0, iv.
     NB: the plaintext words are stored byte-swapped (dword value 0x756E6B6E
     == bytes "nknu"), exactly as the RC5-72 dnetc unit carries them. */
  check(rd32le(body+20)==0x756E6B6Eu && rd32le(body+24)==0x54686520u,
        "plain == nknu / The (dword-values 756E6B6E 54686520)");
  check(rd32le(body+28)==0x5F74ECA6u && rd32le(body+32)==0xE7AFFCBEu,
        "cypher == RC5-72 test cypher 5F74ECA6 E7AFFCBE");
  check(rd32le(body+12)==0x007AF5F6u && rd32le(body+16)==0x41D5974Cu,
        "iv == 007AF5F6 41D5974C");
  check(rd32le(body+0)==0xAB000000u && rd32le(body+4)==0x00E7DB8Eu && rd32le(body+8)==0,
        "L0.key == AB000000 00E7DB8E 00000000");

  /* compute the on-disk seed and the decoded dword 42 */
  u32 seed = bswap32_self(rd32le(PACKET0 + 172));
  u32 x42;
  {
    /* replicate decode of dword 42 (bytes 168..171) with K = seed - 42*DELTA */
    u32 K = seed - (u32)(42ULL*0x481EAE9DULL);
    u32 x = rd32le(PACKET0 + 168);
    x = bswap32_self(x); x += 0x61C88647u; x = ~x; x ^= K; x = bswap32_self(x);
    x42 = x;
  }

  /* 2. checksum invariant */
  u32 ck = moo521_checksum(body);
  check(bswap32_self(x42) == ck,
        "bswap32(decoded dword42) == moo521_checksum(body)");

  /* 3. round-trip: encode(decode(pkt)) must reproduce ALL 176 bytes.
        (The cipher window covers dwords 0..42, so this also validates the
        checksum dword 42 and the key slot 172..175.) */
  moo521_encode(body, seed, re);
  check(memcmp(re, PACKET0, MOO521_RECLEN) == 0,
        "moo521_encode(moo521_decode(pkt)) == pkt (all 176 bytes)");
  if (memcmp(re, PACKET0, MOO521_RECLEN) != 0){
    for (int i=0;i<MOO521_RECLEN;i++)
      if (re[i]!=PACKET0[i]){ printf("        first diff @ byte %d: %02X != %02X\n", i, re[i], PACKET0[i]); break; }
  }

  /* 4. header helpers */
  u8 hdr[MOO521_HEADERLEN];
  moo521_make_header(hdr, 3);
  check(moo521_is_header(hdr) == 1, "make_header -> is_header");
  u32 c = 0;
  check(moo521_header_count(hdr, &c) == 0 && c == 3,
        "header_count == 3 from generated header");
  /* reject a bad version header */
  u8 bad[MOO521_HEADERLEN]; memcpy(bad, hdr, MOO521_HEADERLEN);
  bad[7] = 0x50; /* version 0x50 != 0x48 */
  check(moo521_header_count(bad, &c) == -1, "header_count rejects wrong version");

  /* 5. pack / unpack: WorkRecord <-> body mapping.  The Moo body is a full
        serialized WorkRecord (sizeof==168): work union + resultcode +
        id[64] + contest/cpu/os/build/core.  Real packets carry the worker id
        string ("rc5@distributed.net") in the id field, so the whole body is
        copied verbatim both ways. */
  {
    WorkRecord wr;
    moo521_unpack( body, &wr );
    /* wr.work.bigcrypto should hold the RC5-72 fields from the body */
    check(wr.work.bigcrypto.plain.hi  == 0x756E6B6Eu,
          "unpack: wr.work.bigcrypto.plain.hi == 756E6B6E");
    check(wr.work.bigcrypto.plain.lo  == 0x54686520u,
          "unpack: wr.work.bigcrypto.plain.lo == 54686520");
    check(wr.work.bigcrypto.cypher.hi == 0x5F74ECA6u,
          "unpack: wr.work.bigcrypto.cypher.hi == 5F74ECA6");
    check(wr.work.bigcrypto.cypher.lo == 0xE7AFFCBEu,
          "unpack: wr.work.bigcrypto.cypher.lo == E7AFFCBE");
    check(wr.work.bigcrypto.key.hi    == 0xAB000000u,
          "unpack: wr.work.bigcrypto.key.hi == AB000000");
    /* the worker-id string lives in the id[] field of the record */
    check(memcmp(wr.id, "rc5@distributed.net", 19) == 0,
          "unpack: wr.id contains 'rc5@distributed.net'");
    /* full-168 round-trip */
    u8 body2[168];
    moo521_pack( &wr, body2 );
    check( memcmp(body, body2, 168) == 0,
           "pack(unpack(body)) == body (all 168 bytes)" );
  }

  printf("\n%s\n", g_fail ? "FAILED" : "ALL TESTS PASSED - moo521 codec is sound.");
  return g_fail;
}
