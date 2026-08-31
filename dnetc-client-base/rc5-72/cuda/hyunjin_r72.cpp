/*
 * ============================================================================
 * hyunjin_r72.c
 *
 * Hyunjin - modern CUDA-Fortran RC5-72 core for distributed.net / Moo! Wrapper
 *
 * Coded by Alperen Yavuz
 *
 * C shim that bridges the distributed.net client's core interface
 * (ccoreio.h: rc5_72_unit_func_*) with the CUDA-Fortran compute core
 * (hyunjin_r72_host.cuf).
 *
 * The dnetc client calls a core as:
 *     s32 rc5_72_unit_func_*(RC5_72UnitWork *, u32 *iterations, void *)
 * This shim implements that prototype, unpacks RC5_72UnitWork into the
 * parameter list the Fortran driver expects, invokes it, and writes the
 * results (consumed iteration count, partial-match stats, found key, updated
 * L0) back into the work-unit structure.
 *
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 * ============================================================================
 */

#include "ccoreio.h"
#include "cputypes.h"
#include "logstuff.h"   /* Log() / LogScreen() */

#ifdef __cplusplus
extern "C" {
#endif

/* Fortran driver entry point (bind(C), defined in hyunjin_r72_host.cuf).
 *
 *   int hyunjin_r72_run(
 *       u32 plain_hi, u32 plain_lo, u32 cypher_hi, u32 cypher_lo,
 *       u32 l0_hi,  u32 l0_mid,  u32 l0_lo,
 *       si64 iterations_in, si64 *iterations_out,
 *       int device, int block_size,
 *       u32 *n_check, u32 *check_hi, u32 *check_mid, u32 *check_lo,
 *       u32 *found_hi, u32 *found_mid, u32 *found_lo,
 *       u32 *res_l0_hi, u32 *res_l0_mid, u32 *res_l0_lo);
 *
 * Returns dnetc Resultcode (RESULT_NOTHING/RESULT_FOUND) or -1 on error.
 * NOTE: "si64 iterations" is passed by value for input and the consumed count
 * is returned through a pointer so the Fortran side never mutates the caller's
 * register variable.
 */
#define HYUNJIN_BLOCK_SIZE_DEFAULT 256

s32 hyunjin_r72_run(
    u32 plain_hi, u32 plain_lo, u32 cypher_hi, u32 cypher_lo,
    u32 l0_hi, u32 l0_mid, u32 l0_lo,
    si64 iterations_in, si64 *iterations_out,
    int device, int block_size,
    u32 *n_check, u32 *check_hi, u32 *check_mid, u32 *check_lo,
    u32 *found_hi, u32 *found_mid, u32 *found_lo,
    u32 *res_l0_hi, u32 *res_l0_mid, u32 *res_l0_lo);

/* ------------------------------------------------------------------------ */

s32 CDECL rc5_72_unit_func_hyunjin(RC5_72UnitWork *rc5_72unitwork, u32 *iterations, void *memblk)
{
  si64 iter_in = (si64)(*iterations);
  si64 iter_out = 0;
  u32 n_check = 0;
  u32 check_hi = 0, check_mid = 0, check_lo = 0;
  u32 found_hi = 0, found_mid = 0, found_lo = 0;
  u32 res_l0_hi, res_l0_mid, res_l0_lo;
  s32 ret;

  static int banner_done = 0;
  if (!banner_done) {
    banner_done = 1;
    LogScreen("Hyunjin RC5-72 CUDA core v1.0 - coded by Alperen Yavuz\r\n");
  }

  res_l0_hi = rc5_72unitwork->L0.hi;
  res_l0_mid = rc5_72unitwork->L0.mid;
  res_l0_lo = rc5_72unitwork->L0.lo;

  ret = hyunjin_r72_run(
      rc5_72unitwork->plain.hi,  rc5_72unitwork->plain.lo,
      rc5_72unitwork->cypher.hi, rc5_72unitwork->cypher.lo,
      rc5_72unitwork->L0.hi, rc5_72unitwork->L0.mid, rc5_72unitwork->L0.lo,
      iter_in, &iter_out,
      0,  /* device: dnetc's CPU-only client omits the GPU devicenum field */
      HYUNJIN_BLOCK_SIZE_DEFAULT,
      &n_check, &check_hi, &check_mid, &check_lo,
      &found_hi, &found_mid, &found_lo,
      &res_l0_hi, &res_l0_mid, &res_l0_lo);

  /* Keep partial-match statistics (used for verification stats).  dnetc
   * carries check.count across ProblemRun calls (see problem.cpp:567,753,816)
   * and reference cores accumulate with ++, so ADD (do not assign); also only
   * overwrite the check key when a new match was found this call. */
  if (n_check) {
    rc5_72unitwork->check.count += n_check;
    rc5_72unitwork->check.hi = check_hi;
    rc5_72unitwork->check.mid = check_mid;
    rc5_72unitwork->check.lo = check_lo;
  }

  if (ret == RESULT_FOUND) {
    /* Exact match: record the found key so dnetc can report it. */
    rc5_72unitwork->L0.hi = found_hi;
    rc5_72unitwork->L0.mid = found_mid;
    rc5_72unitwork->L0.lo = found_lo;
    *iterations = (u32)iter_out;
  } else if (ret == RESULT_NOTHING || ret == RESULT_WORKING) {
    /* Advance L0 by everything we consumed. */
    rc5_72unitwork->L0.hi = res_l0_hi;
    rc5_72unitwork->L0.mid = res_l0_mid;
    rc5_72unitwork->L0.lo = res_l0_lo;
    *iterations = (u32)iter_out;
  }
  /* On error (ret<0) leave L0 / iterations untouched; dnetc will abort. */

  return ret;
}

#ifdef __cplusplus
}
#endif
