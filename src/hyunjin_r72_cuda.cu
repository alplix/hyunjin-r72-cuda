/*
 * ============================================================================
 * hyunjin_r72_cuda.cu
 *
 * Hyunjin - modern CUDA-C RC5-72 core for distributed.net / BOINC (Windows)
 *
 * Coded by Alperen Yavuz
 *
 * CUDA-C port of the original CUDA-Fortran core (hyunjin_r72_device.cuf /
 * hyunjin_r72_host.cuf).  Previously the core was written in CUDA Fortran and
 * could only be built with the NVIDIA HPC SDK (nvfortran), which is not
 * distributed for Windows.  This CUDA-C version targets the NVIDIA CUDA
 * toolkit (nvcc + MSVC), so the same core can be built on Windows x64.
 *
 * The host entry point `hyunjin_r72_run` keeps the exact C ABI expected by
 * the C shim in hyunjin_r72.c, which bridges it to the distributed.net
 * core interface (rc5_72_unit_func_hyunjin).
 *
 * RC5-72 = RC5-32/12/16 with a 72-bit key.  Each work unit carries an
 * IV-mixed plaintext block, a cyphertext block and a 72-bit starting key
 * L0 {hi,mid,lo}.  The core iterates L0 upward and finds the key whose
 * encryption of "plain" equals "cypher".
 *
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 * ============================================================================
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <cuda_runtime.h>

/* dnetc Resultcode values (mirror ccoreio.h) */
#define RESULT_WORKING 0
#define RESULT_NOTHING 1
#define RESULT_FOUND   2

/* Number of consecutive keys each thread tests per kernel launch. */
#define HYUNJIN_PIPELINE 4

/* Launch geometry (256 threads/block). */
#define HYUNJIN_BLOCK_SIZE 256

/* Maximum blocks per launch. */
#define MAX_GRID 65535

/* RC5 constants (unsigned 32-bit bit-identical to Fortran signed int32). */
#define RC5_P 0xB7E15163u
#define RC5_Q 0x9E3779B9u

/* --------------------------------------------------------------------------
 * Device helpers
 * ------------------------------------------------------------------------ */

/* ROTL32(x,n): rotate a 32-bit word left by n bits (n applied mod 32). */
__host__ __device__ __forceinline__ uint32_t hy_rotl(uint32_t x, int n)
{
  n &= 31;
  if (n == 0) return x;
  return (uint32_t)((x << n) | (x >> (32 - n)));
}

/* Byte-swap a 32-bit word. */
__host__ __device__ __forceinline__ uint32_t hy_bswap32(uint32_t x)
{
  return ((x & 0x000000FFu) << 24) |
         ((x & 0x0000FF00u) << 8) |
         ((x & 0x00FF0000u) >> 8) |
         ((x & 0xFF000000u) >> 24);
}

/* --------------------------------------------------------------------------
 * Add "amount" to the 72-bit key {hi,mid,lo} using the EXACT dnetc RC5-72
 * enumeration order.  dnetc lays the 72-bit little-endian key out as
 *     K = hi + bswap(mid) * 2^8 + bswap(lo) * 2^40
 * and cascades a carry from "hi" (least significant) through "mid" then "lo".
 * This mirrors rc5-72/ansi/r72-ref.cpp so the search keyspace matches dnetc.
 * ------------------------------------------------------------------------ */
__host__ __device__ __forceinline__ void hy_increment_l0(uint32_t *hi, uint32_t *mid,
                                                uint32_t *lo, uint32_t amount)
{
  uint64_t Hs  = (uint64_t)(*hi);
  uint64_t M64 = (uint64_t)hy_bswap32(*mid);   /* unsigned 32 */
  uint64_t Bs  = (uint64_t)hy_bswap32(*lo);    /* unsigned 32 */
  uint64_t mask40 = ((uint64_t)1 << 40) - 1;

  uint64_t lo64 = Hs + (M64 << 8);             /* bits 0..39 */
  lo64 += amount;
  uint64_t carry = lo64 >> 40;                 /* to bits 40+ */
  lo64 &= mask40;
  uint64_t Bc = Bs + carry;                    /* 32-bit (mod 2^32) */

  *hi  = (uint32_t)(lo64 & 0xFFu);
  *mid = hy_bswap32((uint32_t)((lo64 >> 8) & 0xFFFFFFFFu));
  *lo  = hy_bswap32((uint32_t)(Bc & 0xFFFFFFFFu));
}

/* --------------------------------------------------------------------------
 * hyunjin_r72_kernel
 *
 * Each thread tests HYUNJIN_PIPELINE consecutive keys, starting from
 * L0 + pipe*(linear_index).  Per-thread byte results hold 2-bit fields:
 *   bit(2k)   -> partial match (A == cypher_lo)
 *   bit(2k+1) -> exact match    (B == cypher_hi as well)
 * match_found is set if any partial match occurred (so the host only copies
 * results back when necessary).
 * ------------------------------------------------------------------------ */
__global__ void hyunjin_r72_kernel(
    uint32_t plain_hi, uint32_t plain_lo,
    uint32_t cypher_hi, uint32_t cypher_lo,
    uint32_t L0_hi, uint32_t L0_mid, uint32_t L0_lo,
    uint32_t process_amount,
    uint32_t *results)
{
  int bx = blockIdx.x;
  int tx = threadIdx.x;
  int bd = blockDim.x;
  int idx = bx * bd + tx;               /* 0-based */

  if (idx >= (int)process_amount) return;

  uint32_t base = (uint32_t)idx * HYUNJIN_PIPELINE;

  for (int k = 0; k < HYUNJIN_PIPELINE; k++) {
    uint32_t S[26];
    uint32_t L[3];
    uint32_t A, B;
    uint32_t a_tmp, b_tmp;
    int si, lj;

    /* ---------------- S[] greedy table ---------------- */
    #pragma unroll
    for (int it = 0; it < 26; it++)
      S[it] = RC5_P + (uint32_t)it * RC5_Q;

    /* ------- L[0]=lo, L[1]=mid, L[2]=hi (72-bit key) ------- */
    L[0] = L0_lo;
    L[1] = L0_mid;
    L[2] = L0_hi;
    hy_increment_l0(&L[2], &L[1], &L[0], base + (uint32_t)k);

    /* ---------------- RC5 key expansion (3*t = 78 steps) ----------------
     * Standard RC5 key schedule operating on S[0..25] and L[0..2].   */
    A = 0;
    B = 0;
    si = 0;
    lj = 0;
    for (int it = 0; it < 78; it++) {
      a_tmp = S[si] + A + B;
      S[si] = hy_rotl(a_tmp, 3);
      A = S[si];

      b_tmp = L[lj] + A + B;
      L[lj] = hy_rotl(b_tmp, (int)(A + B));
      B = L[lj];

      si = (si + 1) % 26;
      lj = (lj + 1) % 3;
    }

    /* ---------------- RC5-32/12/16 encryption (12 rounds) ----------------
     * A uses S[2..24] (even), B uses S[3..25] (odd).               */
    A = plain_lo + S[0];
    B = plain_hi + S[1];

    #pragma unroll
    for (int i = 0; i < 12; i++) {
      A = hy_rotl(A ^ B, (int)B) + S[2 + 2*i];
      B = hy_rotl(B ^ A, (int)A) + S[3 + 2*i];
    }

    /* ---------------- match check ---------------- */
    if (A == cypher_lo) {
      /* use atomic only when there is a hit; bit-OR is safe */
      atomicOr(&results[idx], 1u << (2*k));
      if (B == cypher_hi)
        atomicOr(&results[idx], 2u << (2*k));
    }
  }
}

/* --------------------------------------------------------------------------
 * Host-side driver (C-callable, matches the shim prototype in hyunjin_r72.c)
 * ------------------------------------------------------------------------ */
extern "C" int hyunjin_r72_run(
    uint32_t plain_hi, uint32_t plain_lo, uint32_t cypher_hi, uint32_t cypher_lo,
    uint32_t l0_hi, uint32_t l0_mid, uint32_t l0_lo,
    int64_t iterations_in, int64_t *iterations_out,
    int device, int block_size,
    uint32_t *n_check, uint32_t *check_hi, uint32_t *check_mid, uint32_t *check_lo,
    uint32_t *found_hi, uint32_t *found_mid, uint32_t *found_lo,
    uint32_t *res_l0_hi, uint32_t *res_l0_mid, uint32_t *res_l0_lo)
{
  int curdev, devCount, nthreads;
  cudaError_t ierr;
  int64_t done = 0, remain, keys_this_pass;
  uint32_t nthreads_this_pass, grid;
  uint32_t *d_results = NULL;
  uint32_t *h_res = NULL;
  size_t bufsz;
  int ret = RESULT_NOTHING;
  int j, k;
  int64_t offset;
  uint32_t base_hi, base_mid, base_lo;
  uint32_t c3, c2, c1;
  size_t max_threads;

  *iterations_out = 0;
  *found_hi = 0; *found_mid = 0; *found_lo = 0;
  *res_l0_hi = l0_hi; *res_l0_mid = l0_mid; *res_l0_lo = l0_lo;
  *n_check = 0;
  *check_hi = 0; *check_mid = 0; *check_lo = 0;

  /* ------------------------------------------------------------------
   * Select device.  Sanitize the requested index (dnetc may pass a value
   * that is out of range or negative in offline mode).
   * ------------------------------------------------------------------ */
  ierr = cudaGetDeviceCount(&devCount);
  if (ierr != cudaSuccess || devCount <= 0) return -1;
  if (device < 0 || device >= devCount) device = 0;
  ierr = cudaGetDevice(&curdev);
  if (ierr != cudaSuccess) return -1;
  if (curdev != device) {
    ierr = cudaSetDevice(device);
    if (ierr != cudaSuccess) return -1;
  }

  nthreads = block_size;
  if (nthreads < 32) nthreads = HYUNJIN_BLOCK_SIZE;
  if (nthreads > 1024) nthreads = 1024;

  /* ------------------------------------------------------------------
   * Allocate device + host result buffers (one 32-bit word per thread).
   * ------------------------------------------------------------------ */
  max_threads = (size_t)nthreads * MAX_GRID;
  bufsz = max_threads * sizeof(uint32_t);
  ierr = cudaMalloc((void **)&d_results, bufsz);
  if (ierr != cudaSuccess) return -1;
  h_res = (uint32_t *)malloc(bufsz);
  if (h_res == NULL) {
    cudaFree(d_results);
    return -1;
  }

  base_hi = l0_hi; base_mid = l0_mid; base_lo = l0_lo;
  done = 0;
  ret = RESULT_NOTHING;

  /* ------------------------------------------------------------------
   * Process the requested key range in passes.  Each pass launches a grid
   * sized to the remaining work; every thread tests HYUNJIN_PIPELINE keys.
   * ------------------------------------------------------------------ */
  while (done < iterations_in) {
    remain = iterations_in - done;
    keys_this_pass = remain;
    if (keys_this_pass > (int64_t)nthreads * MAX_GRID * HYUNJIN_PIPELINE)
      keys_this_pass = (int64_t)nthreads * MAX_GRID * HYUNJIN_PIPELINE;

    /* number of threads needed for keys_this_pass keys (each does PIPE keys) */
    nthreads_this_pass = (uint32_t)((keys_this_pass + HYUNJIN_PIPELINE - 1)
                                    / HYUNJIN_PIPELINE);
    grid = (uint32_t)(((int64_t)nthreads_this_pass + nthreads - 1) / nthreads);

    /* clear device buffer */
    ierr = cudaMemsetAsync(d_results, 0, (size_t)nthreads_this_pass * sizeof(uint32_t));
    if (ierr != cudaSuccess) { ret = -1; break; }

    /* launch */
    hyunjin_r72_kernel<<<grid, nthreads>>>(
        plain_hi, plain_lo, cypher_hi, cypher_lo,
        base_hi, base_mid, base_lo, nthreads_this_pass, d_results);
    ierr = cudaGetLastError();
    if (ierr != cudaSuccess) { ret = -1; break; }

    ierr = cudaDeviceSynchronize();
    if (ierr != cudaSuccess) { ret = -1; break; }

    /* pull full result vector back to host and scan it */
    ierr = cudaMemcpy(h_res, d_results,
                      (size_t)nthreads_this_pass * sizeof(uint32_t),
                      cudaMemcpyDeviceToHost);
    if (ierr != cudaSuccess) { ret = -1; break; }

    /* scan results for partial / exact matches */
    for (j = 0; j < (int)nthreads_this_pass; j++) {
      if (h_res[j] == 0) continue;
      for (k = 0; k < HYUNJIN_PIPELINE; k++) {
        if (h_res[j] & (1u << (2*k))) {
          (*n_check)++;
          offset = HYUNJIN_PIPELINE*(int64_t)j + k;
          c3 = base_hi; c2 = base_mid; c1 = base_lo;
          hy_increment_l0(&c3, &c2, &c1, (uint32_t)offset);
          *check_hi = c3; *check_mid = c2; *check_lo = c1;
          if (h_res[j] & (2u << (2*k))) {
            *found_hi = c3; *found_mid = c2; *found_lo = c1;
            *iterations_out = done + offset;
            ret = RESULT_FOUND;
            break;
          }
        }
      }
      if (ret == RESULT_FOUND) break;
    }
    if (ret == RESULT_FOUND) break;

    /* advance base key by the number of keys actually processed */
    hy_increment_l0(&base_hi, &base_mid, &base_lo,
                    nthreads_this_pass * HYUNJIN_PIPELINE);
    done += (int64_t)nthreads_this_pass * HYUNJIN_PIPELINE;
  }

  /* report final key cursor and consumed count */
  *res_l0_hi = base_hi; *res_l0_mid = base_mid; *res_l0_lo = base_lo;
  if (ret == RESULT_NOTHING) *iterations_out = done;

  free(h_res);
  cudaFree(d_results);

  return ret;
}