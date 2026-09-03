/*
 * moo521.cpp - implementation of the Moo! Wrapper RC5-72 buffer codec.
 * See moo521.h for a full description and field map.
 *
 * All constants reverse-engineered from the Moo `dnetc` 2.9112-521-CTR fork
 * (see notes in moo521.h) and verified byte-exact on real per-workunit
 * `in.r72` files (round-trip encode->decode reproduces the source packet,
 * and the produced `check` dword matches the client's own 8-pass checksum).
 *
 * Copyright distributed.net 1997-2016 - All Rights Reserved
 * For use in distributed.net projects only.
 */

#include "moo521.h"
#include <string.h>

/* --------------------------------------------------------------------- */
/* internal helpers                                                       */

static u32 rd32le(const u8 *p){ u32 d; memcpy(&d,p,4); return d; }
static void wr32le(u8 *p, u32 v){ memcpy(p,&v,4); }

static u32 bswap32(u32 x){
  return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8)
       | ((x & 0x00FF0000u) >> 8)  | ((x & 0xFF000000u) >> 24);
}

/* checksum constants (8 passes over the 42 dwords) */
#define MOO521_CKSEED  0xB7E15163u
#define MOO521_CKQ     0x61C88647u

/* cipher constants (per-dword key schedule step) */
#define MOO521_Q       0x61C88647u
#define MOO521_DELTA   0x481EAE9Du   /* K -= DELTA each dword */

/* --------------------------------------------------------------------- */
/* public API                                                             */

int moo521_is_header(const u8 *buf){
  if (!buf) return 0;
  return (buf[0]==0x83 && buf[1]==0xB6 && buf[2]==0x34 && buf[3]==0x1A);
}

int moo521_header_count(const u8 *header, u32 *count){
  if (!moo521_is_header(header)) return -1;
  /* version/count are stored big-endian in the header */
  u32 v = ((u32)header[4]<<24)|((u32)header[5]<<16)|((u32)header[6]<<8)|(u32)header[7];
  u32 c = ((u32)header[8]<<24)|((u32)header[9]<<16)|((u32)header[10]<<8)|(u32)header[11];
  if (v != 0x48)    return -1;   /* version must equal 72 */
  if (c > 0xFFFFFF) return -1;   /* sanity */
  if (count) *count = c;
  return 0;
}

void moo521_make_header(u8 *header, u32 count){
  if (!header) return;
  memset(header, 0, MOO521_HEADERLEN);
  header[0]=0x83; header[1]=0xB6; header[2]=0x34; header[3]=0x1A;
  header[4]=0; header[5]=0; header[6]=0; header[7]=0x48;       /* version 72 BE */
  header[8] =(u8)((count>>24)&0xFF);
  header[9] =(u8)((count>>16)&0xFF);
  header[10]=(u8)((count>>8 )&0xFF);
  header[11]=(u8)( count     &0xFF);
}

u32 moo521_checksum(const u8 *rec168){
  u32 edi = MOO521_CKSEED;
  u32 w[42];
  for (int i=0;i<42;i++) w[i] = rd32le(rec168 + i*4);
  for (int pass=0; pass<8; pass++){
    for (int i=0;i<42;i++){
      u32 x = bswap32(w[i]) ^ edi;
      x -= MOO521_CKQ;
      edi = x;
    }
  }
  return edi;
}

void moo521_decode(const u8 *packet176, u8 *rec168){
  u32 seed = bswap32(rd32le(packet176 + 172));
  memcpy(rec168, packet176, 168);
  u32 K = seed;
  /* The record body is exactly 42 dwords (bytes 0..167).  dword 42 (the
   * checksum, bytes 168..171) is metadata and is recomputed on encode, so it
   * is intentionally NOT decrypted here - that keeps `rec168` a clean
   * in-bounds 168-byte buffer. */
  for (int i=0;i<42;i++){
    u32 x = rd32le(rec168 + i*4);
    x = bswap32(x);
    x += MOO521_Q;
    x = ~x;
    x ^= K;
    x = bswap32(x);
    wr32le(rec168 + i*4, x);
    K -= MOO521_DELTA;
  }
}

void moo521_encode(const u8 *rec168, u32 seed, u8 *packet176){
  memcpy(packet176, rec168, 168);
  u32 ck = moo521_checksum(rec168);
  wr32le(packet176 + 168, bswap32(ck));
  wr32le(packet176 + 172, bswap32(seed));
  u32 K = seed;
  /* The cipher window covers dwords 0..42 (bytes 0..171).  Read from
   * `packet176` (not `rec168`): dword 42's pre-cipher value - the swapped
   * checksum - was written to packet176[168..171] above and must be read
   * from there (reading rec168 would go out of bounds). */
  for (int i=0;i<43;i++){
    u32 x = rd32le(packet176 + i*4);
    x = bswap32(x);
    x ^= K;
    x = ~x;
    x -= MOO521_Q;
    x = bswap32(x);
    wr32le(packet176 + i*4, x);
    K -= MOO521_DELTA;
  }
}

/* Map a stock WorkRecord into the 168-byte Moo body.  The Moo body is a full
 * serialized WorkRecord (sizeof(WorkRecord)==168: the ContestWork work union,
 * resultcode, id[64], and the contest/cpu/os/build/core tail).  Even though
 * the RC5-72 bigcrypto fields only occupy the first 72 bytes, real Moo
 * packets carry the worker `id` string and the trailing metadata here too, so
 * the whole body is copied verbatim. */
void moo521_pack(const WorkRecord *wr, u8 *body168){
  memcpy(body168, wr, 168);
}

/* Map a 168-byte Moo body back into a WorkRecord (full serialized record). */
void moo521_unpack(const u8 *body168, WorkRecord *wr){
  memcpy(wr, body168, 168);
}
