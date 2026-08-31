/*
 * =============================================================================
 * hyunjin_selftest_c.c
 *
 * Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
 *
 * Coded by Alperen Yavuz
 *
 * Standalone self-test / validation harness for the CUDA-C core
 * (src/hyunjin_r72_cuda.cu).  Can be built and run without any dnetc/BOINC
 * integration:
 *
 *     nvcc -O3 hyunjin_selftest_c.c src/hyunjin_r72_cuda.cu -o hyunjin_selftest
 *
 * It validates the known dnetc RC5-72 test vector against the GPU core
 * (via the C ABI `hyunjin_r72_run`), mirroring the CUDA-Fortran and Julia
 * self-tests.
 *
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 * =============================================================================
 */
#include <stdint.h>
#include <stdio.h>
#include <cuda_runtime.h>

#define RESULT_WORKING 0
#define RESULT_NOTHING 1
#define RESULT_FOUND   2

/* C ABI from src/hyunjin_r72_cuda.cu */
extern int hyunjin_r72_run(
    uint32_t plain_hi, uint32_t plain_lo, uint32_t cypher_hi, uint32_t cypher_lo,
    uint32_t l0_hi, uint32_t l0_mid, uint32_t l0_lo,
    int64_t iterations_in, int64_t *iterations_out,
    int device, int block_size,
    uint32_t *n_check, uint32_t *check_hi, uint32_t *check_mid, uint32_t *check_lo,
    uint32_t *found_hi, uint32_t *found_mid, uint32_t *found_lo,
    uint32_t *res_l0_hi, uint32_t *res_l0_mid, uint32_t *res_l0_lo);

/* CPU reference RC5-32/12/16 encrypt (bit-identical to the GPU core schedule) */
static uint32_t hy_rotl(uint32_t x, int32_t n) { n &= 31; return n ? (x << n) | (x >> (32 - n)) : x; }
static uint32_t hy_bswap32(uint32_t x) {
    return ((x & 0x000000FFu) << 24) | ((x & 0x0000FF00u) << 8) |
           ((x & 0x00FF0000u) >> 8) | ((x & 0xFF000000u) >> 24);
}
static void ref_encrypt(uint32_t kmhi, uint32_t kmmid, uint32_t kmlo,
                        uint32_t plain_lo, uint32_t plain_hi,
                        uint32_t *out_lo, uint32_t *out_hi) {
    uint32_t S[26], L[3], A, B;
    int it, si, lj;
    for (it = 0; it < 26; it++) S[it] = 0xB7E15163u + (uint32_t)it * 0x9E3779B9u;
    L[0] = kmlo; L[1] = kmmid; L[2] = kmhi;
    A = 0; B = 0; si = 0; lj = 0;
    for (it = 0; it < 78; it++) {
        S[si] = hy_rotl(S[si] + A + B, 3); A = S[si];
        L[lj] = hy_rotl(L[lj] + A + B, (int32_t)(A + B)); B = L[lj];
        si = (si + 1) % 26; lj = (lj + 1) % 3;
    }
    A = plain_lo + S[0]; B = plain_hi + S[1];
    for (it = 0; it < 12; it++) {
        A = hy_rotl(A ^ B, (int32_t)B) + S[2 + 2*it];
        B = hy_rotl(B ^ A, (int32_t)A) + S[3 + 2*it];
    }
    *out_lo = A; *out_hi = B;
}

int main(void) {
    int failures = 0;
    /* dnetc selftest vector #0 (official RSA test case #1) */
    const uint32_t MAG_LO   = 0x53030CC9u;
    const uint32_t MAG_MI   = 0xFEE1D4C0u;
    const uint32_t MAG_HI   = 0x00000085u;
    const uint32_t PLAIN_LO = 0x3F3CA653u;   /* IV-mixed */
    const uint32_t PLAIN_HI = 0x2FF17AF3u;   /* IV-mixed */
    const uint32_t START_LO = 0x53030CC9u;
    const uint32_t START_MI = 0x00E1D4C0u;
    const uint32_t START_HI = 0x00000000u;
    const int64_t  SOL_OFFSET = 65157;
    const uint32_t CY_LO = 0x562D285Au;
    const uint32_t CY_HI = 0x2FB7852Au;

    uint32_t clo, chi;
    int status;
    int64_t iter_in, iter_out;
    uint32_t n_check, chk_hi, chk_mid, chk_lo, fnd_hi, fnd_mid, fnd_lo;
    uint32_t rl0_hi, rl0_mid, rl0_lo;
    int device, devCount;

    printf("=========================================================\n");
    printf(" Hyunjin RC5-72 CUDA-C core - coded by Alperen Yavuz\n");
    printf(" Self-test / validation harness\n");
    printf("=========================================================\n");

    /* 1) CPU reference against the known vector */
    ref_encrypt(MAG_HI, MAG_MI, MAG_LO, PLAIN_LO, PLAIN_HI, &clo, &chi);
    printf("   CPU encrypt: clo=%08X chi=%08X\n", clo, chi);
    if (clo == CY_LO && chi == CY_HI) {
        printf("   [PASS] CPU RC5-72 reference matches known vector\n");
    } else { printf("   [FAIL] CPU reference mismatch\n"); failures++; }

    /* 2) CUDA device enumeration */
    devCount = 0;
    if (cudaGetDeviceCount(&devCount) != 0) devCount = 0;
    printf("   CUDA devices detected: %d\n", devCount);
    if (devCount <= 0) { printf(" [ABORT] No CUDA device available.\n"); return 1; }
    device = 0;

    /* 3) GPU core on the authentic dnetc test slice */
    iter_in = SOL_OFFSET + 512;
    iter_out = 0; n_check = 0;
    chk_hi = 0; chk_mid = 0; chk_lo = 0;
    fnd_hi = 0; fnd_mid = 0; fnd_lo = 0;
    rl0_hi = 0; rl0_mid = 0; rl0_lo = 0;
    status = hyunjin_r72_run(
        PLAIN_HI, PLAIN_LO, CY_HI, CY_LO,
        START_HI, START_MI, START_LO,
        iter_in, &iter_out, device, 256,
        &n_check, &chk_hi, &chk_mid, &chk_lo,
        &fnd_hi, &fnd_mid, &fnd_lo,
        &rl0_hi, &rl0_mid, &rl0_lo);
    printf("   GPU core status=%d iterations_out=%lld\n", status, (long long)iter_out);
    printf("   GPU found key = %08X %08X %08X\n", fnd_hi, fnd_mid, fnd_lo);
    printf("   expected key  = %08X %08X %08X\n", MAG_HI, MAG_MI, MAG_LO);
    printf("   partial matches (n_check) = %u\n", n_check);
    if (status == RESULT_FOUND && iter_out == SOL_OFFSET &&
        fnd_hi == MAG_HI && fnd_mid == MAG_MI && fnd_lo == MAG_LO) {
        printf("   [PASS] GPU core found the exact key at offset %lld\n", (long long)SOL_OFFSET);
    } else { printf("   [FAIL] GPU core did not find the expected key\n"); failures++; }

    /* 4) negative case: no solution in the range.  The core always consumes a
     * multiple of its pipeline (4) keys, so a 1-key request consumes 4 keys
     * and returns RESULT_NOTHING with no partial matches. */
    n_check = 0; iter_out = 0;
    fnd_hi = 0; fnd_mid = 0; fnd_lo = 0;
    status = hyunjin_r72_run(
        PLAIN_HI, PLAIN_LO, CY_HI, CY_LO,
        START_HI, START_MI, START_LO,
        (int64_t)1, &iter_out, device, 256,
        &n_check, &chk_hi, &chk_mid, &chk_lo,
        &fnd_hi, &fnd_mid, &fnd_lo,
        &rl0_hi, &rl0_mid, &rl0_lo);
    if (status == RESULT_NOTHING && n_check == 0 && iter_out == 4) {
        printf("   [PASS] empty range returns RESULT_NOTHING, consumed=%lld\n",
               (long long)iter_out);
    } else {
        printf("   [FAIL] empty-range behavior: status=%d consumed=%lld n_check=%u\n",
               status, (long long)iter_out, n_check);
        failures++;
    }

    printf("---------------------------------------------------------\n");
    if (failures == 0) {
        printf(" ALL TESTS PASSED - Hyunjin CUDA-C core is sound.\n");
        return 0;
    }
    printf(" %d TEST(S) FAILED.\n", failures);
    return 1;
}
