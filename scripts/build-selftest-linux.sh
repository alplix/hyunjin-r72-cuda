#!/usr/bin/env bash
# =============================================================================
# build-selftest-linux.sh
#
# Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# Builds and runs the standalone self-test/validation harness for the CUDA-C
# core.  Does NOT need the dnetc client tree.  It checks the CPU RC5-72
# reference against the known test vector, then runs the GPU kernel and
# confirms it finds the exact solution key at the expected offset.
#
# Usage:
#   ./build-selftest-linux.sh
#
# Requires: nvcc (CUDA toolkit), an NVIDIA GPU + driver.
# The Julia self-test (julia/selftest.jl) provides the equivalent validation
# with no C compilation, if you prefer.
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

set -euo pipefail

NVCC="${NVCC:-nvcc}"
SRC="$(cd "$(dirname "$0")/.." && pwd)"
OUT="${1:-build/selftest}"
# Blackwell (RTX 50-series) consumer GPUs are sm_120; include sm_80/90/100 too.
GPU_ARCHS="${GPU_ARCHS:--gencode arch=compute_80,code=sm_80 -gencode arch=compute_90,code=sm_90 -gencode arch=compute_100,code=sm_100 -gencode arch=compute_120,code=sm_120}"

command -v "$NVCC" >/dev/null || { echo "ERROR: nvcc not found"; exit 1; }
mkdir -p "$OUT"

echo "==> compiling self-test harness (CUDA-C core)"
"$NVCC" -O3 $GPU_ARCHS \
    -o "$OUT/hyunjin_selftest" \
    "$SRC/test/hyunjin_selftest_c.c" \
    "$SRC/src/hyunjin_r72_cuda.cu"

echo "==> running self-test"
"$OUT/hyunjin_selftest"
