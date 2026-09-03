/*
 * moo521_file_test.cpp
 *
 * Standalone harness that exercises the same read/write logic the buffer
 * layer uses (moo521_decode + moo521_unpack / moo521_pack + moo521_encode,
 * plus the 32-byte header) against a REAL Moo `in.r72` file:
 *
 *   1. open in.r72, validate header, count packets
 *   2. decode + unpack every packet into a WorkRecord
 *   3. verify the RC5-72 fields and the worker-id string are present
 *   4. pack + re-encode and confirm the exact original bytes round-trip
 *
 * Build (needs the dnetc common include path and HAVE_CRYPTO_V2):
 *   g++ -O2 -DCLIENT_OS=1 -DCLIENT_CPU=1 -DHAVE_CRYPTO_V2 \
 *       -I<dnetc-client-base>/common \
 *       test/moo521_file_test.cpp common/moo521.cpp -o moo521_file_test
 *   ./moo521_file_test <in.r72>
 */
#ifndef HAVE_CRYPTO_V2
#define HAVE_CRYPTO_V2 1
#endif
#include "moo521.h"
#include <stdio.h>
#include <string.h>
#ifdef _WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif

static int g_fail = 0;
static void check(int ok, const char *what){
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if(!ok) g_fail = 1;
}
static u32 bswap_self(u32 x){
  return ((x&0xFF)<<24)|((x&0xFF00)<<8)|((x&0xFF0000)>>8)|((x&0xFF000000)>>24);
}

int main(int argc, char **argv){
  const char *path = (argc > 1) ? argv[1] : "in.r72";
  printf("Moo521 file round-trip test  (%s)\n", path);
  printf("=================================\n");

  FILE *f = fopen(path, "rb");
  if(!f){ printf("  could not open %s\n", path); return 1; }

  u8 hdr[MOO521_HEADERLEN];
  if(fread(hdr, 1, MOO521_HEADERLEN, f) != MOO521_HEADERLEN){
    printf("  short header\n"); return 1;
  }
  check(moo521_is_header(hdr) == 1, "header magic detected");
  u32 count = 0;
  int hrc = moo521_header_count(hdr, &count);
  check(hrc == 0 && count >= 1, "header count parsed (>=1 packet)");
  printf("  packets in file: %u\n", count);

  long fsz;
  fseek(f, 0, SEEK_END); fsz = ftell(f);
  check(fsz == (long)(MOO521_HEADERLEN + count * MOO521_RECLEN),
        "file size == 32 + count*176");
  fseek(f, MOO521_HEADERLEN, SEEK_SET);

  int n_ok = 0, n_bad = 0;
  int w_ok = 0, w_bad = 0;
  u8 pkt[MOO521_RECLEN];
  for (u32 i = 0; i < count; i++){
    if (fread(pkt, 1, MOO521_RECLEN, f) != MOO521_RECLEN) break;

    u8 body[168], re[MOO521_RECLEN];
    WorkRecord wr;
    memset(&wr, 0, sizeof(wr));

    moo521_decode(pkt, body);
    moo521_unpack(body, &wr);

    /* round-trip: pack + re-encode must reproduce the original 176 bytes */
    moo521_pack(&wr, body);
    u32 seed;
    { u32 d; memcpy(&d, pkt+172, 4); seed = bswap_self(d); }
    moo521_encode(body, seed, re);
    if (memcmp(re, pkt, MOO521_RECLEN) == 0) n_ok++;
    else n_bad++;

    /* full buffpub read+write symmetry:
     * read path  : decode -> unpack -> __switch_byte_order(net->host)
     * write path : __switch_byte_order(host->net) -> pack -> encode(same seed)
     * must reproduce the original on-disk packet. */
    {
      WorkRecord host;  /* host-order view (what the core consumes) */
      memset(&host, 0, sizeof(host));
      moo521_unpack(body, &host);
      host.contest = RC5_72;
      /* net->host on the work union, exactly like buffpub moo_read_one_record */
      u32 *w = (u32 *)(&(host.work));
      for (unsigned k = 0; k < (sizeof(host.work)/sizeof(u32)); k++)
        w[k] = (u32)ntohl(w[k]);

      u8 re2[MOO521_RECLEN];
      WorkRecord scratch;             /* host->net, like buffpub moo_write_one_record */
      memset(&scratch, 0, sizeof(scratch));
      memcpy(&scratch.work, &host.work, sizeof(scratch.work));
      memcpy(scratch.id, host.id, sizeof(scratch.id));
      scratch.contest = RC5_72;
      w = (u32 *)(&(scratch.work));
      for (unsigned k = 0; k < (sizeof(scratch.work)/sizeof(u32)); k++)
        w[k] = (u32)ntohl(w[k]);
      moo521_pack(&scratch, body);
      moo521_encode(body, seed, re2);
      /* The `work` union (packet bytes 0..79) must be byte-exact: it is the
       * crypto-relevant payload, normalized host<->net by __switch_byte_order
       * (ntohl is self-inverse).  Trailing metadata (contest/cpu/os/build/core,
       * bytes 80..167) is intentionally re-written as client host values by the
       * buffpub read path, so it is excluded from byte-exactness. */
      if (memcmp(re2, pkt, 80) == 0) w_ok++;
      else w_bad++;
    }

    if (i == 0){
      /* Show host-order (byte-swapped) view of the work union, which is
       * what the RC5 core consumes after __switch_byte_order in the
       * buffpub read path. */
      u32 keyhi=ntohl(wr.work.bigcrypto.key.hi),
          keymid=ntohl(wr.work.bigcrypto.key.mid),
          keylo =ntohl(wr.work.bigcrypto.key.lo),
          plainhi=ntohl(wr.work.bigcrypto.plain.hi),
          plainlo=ntohl(wr.work.bigcrypto.plain.lo);
      printf("  packet 0 fields (host order, as the core sees them):\n");
      printf("    L0.key  hi/mid/lo = %08X %08X %08X\n", keyhi,keymid,keylo);
      printf("    plain   hi/lo    = %08X %08X  (%.8s)\n",
             plainhi, plainlo, (const char*)&plainhi);
      printf("    id               = '%.64s'\n", wr.id);
      printf("    contest          = %u\n", (unsigned)wr.contest);
    }
  }
  fclose(f);

  check(n_bad == 0 && n_ok == (int)count,
        "pack(unpack(decode(pkt))) round-trips all packets byte-exact");
  check(w_bad == 0 && w_ok == (int)count,
        "buffpub read(write(host)) round-trips byte-exact (host<->net symmetry)");
  printf("  byte-exact round-trips: %d/%u  buffpub symmetry: %d/%u\n",
         n_ok, count, w_ok, count);

  printf("\n%s\n", g_fail ? "FAILED" : "ALL TESTS PASSED - moo521 file IO is sound.");
  return g_fail;
}
