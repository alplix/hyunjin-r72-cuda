#!/usr/bin/env bash
# =============================================================================
# build-linux-x86_64.sh
#
# Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# Builds a custom distributed.net client (Linux x86_64) with the Hyunjin
# CUDA-C RC5-72 core compiled in.  Requires:
#   - NVIDIA CUDA toolkit (nvcc) for x86_64 Linux
#   - gcc/g++, make, patch
#   - an NVIDIA GPU + driver at runtime (Ampere or newer recommended)
#
# This uses the authoritative CUDA-C core (src/hyunjin_r72_cuda.cu) and the
# C++ shim (src/hyunjin_r72.cpp) that are also used by the Windows build.  The
# CUDA-Fortran core (the original implementation) lives in legacy/ and is NOT
# used here; it required the NVIDIA HPC SDK (nvfortran) which is not available
# on Windows, which is why the CUDA-C port exists.
#
# Usage:
#   ./build-linux-x86_64.sh <path-to-dnetc-client-base> [outdir]
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
DN_BASE="${1:-$SRC/dnetc-client-base}"
OUTDIR="${2:-build/linux-x86_64}"

NVCC="${NVCC:-nvcc}"
CXX="${CXX:-g++}"
MAKE="${MAKE:-make}"

echo "==> dnetc base : $DN_BASE"
echo "==> cores      : $SRC/src"
echo "==> output     : $OUTDIR"
echo "==> compilers  : nvcc=$NVCC cxx=$CXX"

command -v "$NVCC" >/dev/null || { echo "ERROR: nvcc not found (set NVCC)"; exit 1; }
command -v "$CXX" >/dev/null  || { echo "ERROR: c++ compiler not found (set CXX)"; exit 1; }
command -v patch >/dev/null  || { echo "ERROR: patch not found"; exit 1; }

mkdir -p "$OUTDIR"
OBJ="$OUTDIR/obj"
mkdir -p "$OBJ"

# GPU architectures to embed (Ampere cc80, Hopper cc90, Blackwell cc100/cc120).
GPU_ARCHS="${GPU_ARCHS:--gencode arch=compute_80,code=sm_80 -gencode arch=compute_90,code=sm_90 -gencode arch=compute_100,code=sm_100 -gencode arch=compute_120,code=sm_120}"

# ---------------------------------------------------------------------------
# 1. Apply the dnetc core-registration patch (idempotent).
# ---------------------------------------------------------------------------
PATCH="$SRC/dnetc-integration/dnetc-r72-hyunjin-coresel.patch"
if grep -q 'rc5_72_unit_func_hyunjin' "$DN_BASE/common/core_r72.cpp"; then
  echo "==> core_r72.cpp already patched, skipping"
else
  echo "==> applying core registration patch"
  ( cd "$DN_BASE" && patch -p1 < "$PATCH" )
fi

# ---------------------------------------------------------------------------
# 2. Compile the CUDA-C core + C++ shim to objects.
# ---------------------------------------------------------------------------
echo "==> compiling CUDA-C core + shim"
"$NVCC" -c -O3 $GPU_ARCHS \
    -o "$OBJ/hyunjin_r72_cuda.o" "$SRC/src/hyunjin_r72_cuda.cu"
"$CXX" -c -O2 -I"$DN_BASE/common" \
    -o "$OBJ/hyunjin_r72.o" "$SRC/src/hyunjin_r72.cpp"

# ---------------------------------------------------------------------------
# 3. Configure the distributed.net client for x86_64.
# ---------------------------------------------------------------------------
echo "==> configuring dnetc client (x86_64)"
( cd "$DN_BASE" && ./configure x86_64 )

# ---------------------------------------------------------------------------
# 4. Build, injecting the Hyunjin objects and the CUDA runtime.
#    Link with nvcc so the CUDA runtime is pulled in correctly.
# ---------------------------------------------------------------------------
CUDA_ROOT="$(cd "$(dirname "$(command -v "$NVCC")")/.." 2>/dev/null && pwd)"
CUDA_LIB="${CUDA_LIB:-$CUDA_ROOT/lib64}"

BASE_ADDOBJS="$(cd "$DN_BASE" && grep '^ADDOBJS' Makefile | cut -d= -f2- | sed 's/^[[:space:]]*//')"
BASE_LIBS="$(cd "$DN_BASE" && grep '^LIBS' Makefile | cut -d= -f2- | sed 's/^[[:space:]]*//')"

echo "==> injecting Hyunjin objects + CUDA runtime into dnetc link"
echo "    ADDOBJS += $OBJ/hyunjin_r72.o $OBJ/hyunjin_r72_cuda.o"
echo "    LIBS    += -L$CUDA_LIB -lcudart"
echo "    LD       = nvcc (CUDA runtime)"

( cd "$DN_BASE" && "$MAKE" \
    LD="$NVCC" \
    CXX="$CXX" \
    ADDOBJS="$BASE_ADDOBJS $OBJ/hyunjin_r72.o $OBJ/hyunjin_r72_cuda.o" \
    LIBS="$BASE_LIBS -L$CUDA_LIB -lcudart -lrt -lpthread -lm" \
    all 2>&1 | tee "$OUTDIR/make.log" )

echo
echo "==> BUILD FINISHED.  Client binary is in $DN_BASE/output/"
echo "    The Hyunjin core is selectable as RC5-72 core (see dnetc.ini [rc5-72] core=<idx>)."
echo "    Run ./dnetc -ini <inifile> -runoffline -multiok=1 to process work."
