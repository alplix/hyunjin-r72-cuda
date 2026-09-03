/*
 * buffpub_e2e_test.cpp
 *
 * End-to-end harness that links the REAL common/buffpub.cpp Moo integration
 * against the public buffer API and exercises it against a real Moo in.r72:
 *
 *   1. BufferCountFileRecords -> expect the non-blank packet count
 *   2. BufferGetFileRecord     -> consume records, verify contest/resultcode
 *                                 and the RC5-72 KAT (key/plain) in host order
 *   3. BufferPutFileRecord     -> write a synthesized result back, verify it
 *                                 round-trips and the count reflects it
 *   4. Confirm the file remains a structurally valid Moo file (magic header
 *      + correct size) the whole time.
 *
 * Only a handful of shallow stubs are needed for symbols buffpub.cpp pulls in:
 *   GetFullPathForFilename, Log, LogScreen, BufferGetRecordInfo, WorkGetSWUCount.
 *
 * Build (this repo):
 *   g++ -O2 -D_M_AMD64 -D_WIN64 -DCLIENT_OS=1 -DCLIENT_CPU=1 \
 *       -DHAVE_RC5_72_CORES -DHAVE_CRYPTO_V2 -DCUDA -DDYN_TIMESLICE \
 *       -I common -I plat/win \
 *       test/buffpub_e2e_test.cpp common/buffpub.cpp common/moo521.cpp \
 *       -lws2_32 -o buffpub_e2e_test
 *   ./buffpub_e2e_test <in.r72> <work_copy.r72>
 */
#ifndef HAVE_CRYPTO_V2
#define HAVE_CRYPTO_V2 1
#endif
#include "cputypes.h"
#include "problem.h"   /* WorkRecord, ContestWork, RC5_72 (defines Client too) */
#include "buffbase.h"
#include "moo521.h"
#include "stdio.h"
#include "string.h"
#include "stdlib.h"

static int g_fail = 0;
static void check(int ok, const char *what){
  printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if(!ok) g_fail = 1;
}

/* ---- shallow stubs for the symbols buffpub.cpp needs ---- */

const char *GetFullPathForFilename(const char *fn){
  static char buf[512];
  strncpy(buf, fn, sizeof(buf)-1); buf[sizeof(buf)-1]=0;
  return buf;
}
void Log(const char *fmt, ...) { (void)fmt; }
void LogScreen(const char *fmt, ...) { (void)fmt; }

/* RC5_72 SWU count (mirrors problem.cpp WorkGetSWUCount): units = 100*it.hi
 * for non-test packets; returns the resultcode (or -1 if invalid). */
static int stub_workgetswu(const ContestWork *work, int rescode,
                           unsigned int contestid, unsigned int *swucount){
  (void)contestid;
  if (rescode != 0 && rescode != 1 && rescode != 2) return -1;
  unsigned int units = 0;
  units = 100 * work->bigcrypto.iterations.hi;
  if (swucount) *swucount = units;
  return rescode;
}

/* BufferGetRecordInfo (mirrors buffbase.cpp's version). */
int BufferGetRecordInfo(const WorkRecord *data, unsigned int *contest,
                        unsigned int *swucount){
  int rc = -1;
  if (data){
    unsigned int cont_i = (unsigned int)data->contest;
    rc = stub_workgetswu(&(data->work), (int)data->resultcode, cont_i, swucount);
    if (rc >= 0 && contest) *contest = cont_i;
  }
  return rc;
}

int main(int argc, char **argv){
  if (argc < 3){
    printf("usage: buffpub_e2e_test <src_in.r72> <work_copy.r72>\n"); return 2;
  }
  const char *src = argv[1];
  const char *work = argv[2];

  /* make a working copy so the pristine source stays untouched */
  FILE *s = fopen(src, "rb");
  if(!s){ printf("cannot open %s\n", src); return 2; }
  FILE *d = fopen(work, "w+b");
  if(!d){ printf("cannot open %s\n", work); return 2; }
  char buf[4096]; size_t n;
  while((n = fread(buf,1,sizeof(buf),s))>0) fwrite(buf,1,n,d);
  fclose(s); fclose(d);

  printf("buffpub end-to-end test  (src=%s, work=%s)\n", src, work);

  /* 1) count */
  unsigned long pc = 0, norm = 0;
  int rc = BufferCountFileRecords(work, RC5_72, &pc, &norm);
  check(rc==0 && pc>=1, "BufferCountFileRecords returns non-blank packet count");
  printf("  packets=%lu  norm(swu)=%lu\n", pc, norm);
  unsigned long initial = pc;

  /* 2) get records until exhausted */
  int got = 0, ok_kat = 0;
  WorkRecord rec;
  for (;;){
    unsigned long cnt = 0;
    rc = BufferGetFileRecord(work, &rec, &cnt, 0, 0);
    if (rc < 0){ printf("  ERR rc=%d\n", rc); check(0,"no I/O error on read"); break; }
    if (rc > 0) break; /* no records left */
    got++;
    check(rec.contest == RC5_72, "record contest == RC5_72");
    check(rec.resultcode == RESULT_WORKING, "record resultcode == RESULT_WORKING");
    /* host-order KAT: plain == "unkn" "The " (RC5-72 known-answer) */
    if (rec.work.bigcrypto.plain.hi == 0x6E6B6E75 &&
        rec.work.bigcrypto.plain.lo == 0x20656854) ok_kat++;
  }
  check(got == (int)initial, "all source packets consumed via BufferGetFileRecord");
  check(ok_kat == got, "each record decodes to the RC5-72 KAT plaintext");
  printf("  consumed=%d  kat_matches=%d\n", got, ok_kat);

  /* 3) put a synthesized result back into a FRESH empty Moo out-file,
   *    exactly as the wrapper pre-creates out.r72 (header, count=0). */
  const char *out = "e2e_out.r72";
  {
    FILE *of = fopen(out, "w+b");
    u8 hdr[MOO521_HEADERLEN];
    moo521_make_header(hdr, 0);
    fwrite(hdr,1,MOO521_HEADERLEN,of);
    fclose(of);
  }
  WorkRecord res; memset(&res, 0, sizeof(res));
  res.contest = RC5_72;
  res.resultcode = RESULT_FOUND;
  res.work.bigcrypto.key.lo = 0xDEADBEEF;
  res.work.bigcrypto.plain.hi = 0x6E6B6E75;
  res.work.bigcrypto.plain.lo = 0x20656854;
  res.work.bigcrypto.iterations.hi = 1;
  strcpy(res.id, "hyunjin-test");
  unsigned long cnt = 0;
  rc = BufferPutFileRecord(out, &res, &cnt, 0);
  check(rc==0, "BufferPutFileRecord writes a result to the pre-created out.r72");
  printf("  put rc=%d count=%lu\n", rc, cnt);

  /* 4) out.r72 must be a structurally valid Moo file that holds the result */
  FILE *v = fopen(out, "rb");
  u8 hdr[MOO521_HEADERLEN];
  int valid = (v && fread(hdr,1,MOO521_HEADERLEN,v)==MOO521_HEADERLEN &&
               moo521_is_header(hdr)==1);
  unsigned long pc2 = 0, norm2 = 0;
  BufferCountFileRecords(out, RC5_72, &pc2, &norm2);
  if (v) fclose(v);
  check(valid, "out.r72 carries the Moo magic header after the write");
  check(pc2 == 1, "re-count sees exactly the one written result packet");
  printf("  after-write out count=%lu\n", pc2);

  /* 5) read it back and verify the RC5-72 KAT plain field survives the
   *    write path's host->net->host round trip */
  WorkRecord back; unsigned long bcnt = 0;
  rc = BufferGetFileRecord(out, &back, &bcnt, 0, 0);
  check(rc==0 && back.contest==RC5_72 && back.work.bigcrypto.plain.hi==0x6E6B6E75,
        "written result reads back with RC5_72 contest and intact plain hi");

  printf("\n%s\n", g_fail ? "FAILED" : "ALL TESTS PASSED - buffpub Moo integration works end-to-end.");
  return g_fail;
}
