#!/usr/bin/env bash
# =============================================================================
# build-linux-aarch64.sh
#
# Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# Cross-builds a custom distributed.net client (Linux aarch64 / arm64) with
# the Hyunjin CUDA-C RC5-72 core compiled in.  Requires:
#   - NVIDIA CUDA toolkit with aarch64 cross-compile support (nvcc) OR a native
#     aarch64 machine with the CUDA toolkit
#   - an aarch64 cross toolchain (aarch64-linux-gnu-gcc/g++) + sysroot
#   - make, patch
#   - an NVIDIA GPU + driver at runtime (Jetson Orin / Grace Hopper etc.)
#
# Uses the authoritative CUDA-C core (src/hyunjin_r72_cuda.cu) and the C++
# shim (src/hyunjin_r72.cpp), matching the Windows and Linux x86_64 builds.
#
# Usage:
#   ./build-linux-aarch64.sh <path-to-dnetc-client-base> [outdir]
#   CROSS_CC=aarch64-linux-gnu-gcc CROSS_CXX=aarch64-linux-gnu-g++ \
#       NVCC=nvcc ./build-linux-aarch64.sh <path>
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
DN_BASE="${1:-$SRC/dnetc-client-base}"
OUTDIR="${2:-$SRC/build/linux-aarch64}"

# The dnetc source is tracked without the executable bit (committed from
# Windows), so re-apply it to the build scripts before use.
find "$DN_BASE" -maxdepth 3 -type f \
  \( -name 'configure' -o -name 'tomake' -o -name 'imake' -o -name '*.sh' -o -name '*.pl' \) \
  -exec chmod +x {} + 2>/dev/null || true

NVCC="${NVCC:-nvcc}"
CC="${CROSS_CC:-aarch64-linux-gnu-gcc}"
CXX="${CROSS_CXX:-aarch64-linux-gnu-g++}"
MAKE="${MAKE:-make}"
# CUDA architecture flags for the target GPU (AArch64 hosts are typically
# Jetson Orin (sm_87), Grace Hopper (sm_90) or Grace Blackwell (sm_120)).
GPU_ARCHS="${GPU_ARCHS:--gencode arch=compute_87,code=sm_87 -gencode arch=compute_90,code=sm_90 -gencode arch=compute_120,code=sm_120}"

echo "==> dnetc base : $DN_BASE"
echo "==> output     : $OUTDIR"
echo "==> compilers  : nvcc=$NVCC cc=$CC cxx=$CXX"

command -v "$NVCC" >/dev/null || { echo "ERROR: nvcc not found (set NVCC)"; exit 1; }
command -v "$CC" >/dev/null    || { echo "ERROR: cross cc not found (set CROSS_CC)"; exit 1; }
command -v "$CXX" >/dev/null   || { echo "ERROR: cross cxx not found (set CROSS_CXX)"; exit 1; }
command -v patch >/dev/null    || { echo "ERROR: patch not found"; exit 1; }

mkdir -p "$OUTDIR"; OBJ="$OUTDIR/obj"; mkdir -p "$OBJ"

# ---------------------------------------------------------------------------
# 1. Apply the core-registration patch (idempotent).
# ---------------------------------------------------------------------------
PATCH="$SRC/dnetc-integration/dnetc-r72-hyunjin-coresel.patch"
if grep -q 'rc5_72_unit_func_hyunjin' "$DN_BASE/common/core_r72.cpp"; then
  echo "==> core_r72.cpp already patched, skipping"
else
  echo "==> applying core registration patch"
  ( cd "$DN_BASE" && patch -p1 < "$PATCH" )
fi

# ---------------------------------------------------------------------------
# 2. Compile the CUDA-C core + C++ shim for aarch64.
# ---------------------------------------------------------------------------
echo "==> compiling CUDA-C core + shim for aarch64"
"$NVCC" -c -O3 $GPU_ARCHS \
    -o "$OBJ/hyunjin_r72_cuda.o" "$SRC/src/hyunjin_r72_cuda.cu"
"$CXX" -c -O2 -I"$DN_BASE/common" \
    -o "$OBJ/hyunjin_r72.o" "$SRC/src/hyunjin_r72.cpp"

# ---------------------------------------------------------------------------
# 3. Configure the dnetc client for arm64.
# ---------------------------------------------------------------------------
echo "==> configuring dnetc client (aarch64)"
( cd "$DN_BASE" && CC="$CC" CXX="$CXX" ./configure linux-arm64 )

# ---------------------------------------------------------------------------
# 4. Build with Hyunjin objects + CUDA runtime injected.
# ---------------------------------------------------------------------------
CUDA_ROOT="$(cd "$(dirname "$(command -v "$NVCC")")/.." 2>/dev/null && pwd)"
CUDA_LIB="${CUDA_LIB:-$CUDA_ROOT/targets/aarch64-linux/lib}"

BASE_ADDOBJS="$(cd "$DN_BASE" && grep '^ADDOBJS' Makefile | cut -d= -f2- | sed 's/^[[:space:]]*//')"
BASE_LIBS="$(cd "$DN_BASE" && grep '^LIBS' Makefile | cut -d= -f2- | sed 's/^[[:space:]]*//')"
BASE_LDFLAGS="$(cd "$DN_BASE" && grep '^LDFLAGS' Makefile | cut -d= -f2- | sed 's/^[[:space:]]*//')"

# The client links -lcudart dynamically, so it has to be able to find the CUDA
# runtime next to itself.  configure bakes this rpath into the linux-cuda*
# targets, but this script configures a plain linux-arm64 target and injects
# the CUDA link flags by hand, so it has to add the rpath itself.  Without it
# the binary searches only the system library paths and aborts with "error
# while loading shared libraries: libcudart.so.13" even though the runtime
# ships in lib/ beside it.
#
# The $$ is deliberate: this string is handed to make on the command line, make
# folds each $$ to a single $, and the shell make invokes then folds \$ to $,
# so the linker finally sees a literal $ORIGIN.  This is the same spelling the
# configure-generated Makefile uses for its own rpath.
RUNPATH_LDFLAGS='-Wl,-rpath=\$$ORIGIN/lib -Wl,-rpath,'"$CUDA_LIB"

echo "==> injecting Hyunjin objects + CUDA runtime into dnetc link"
echo "    rpath: $RUNPATH_LDFLAGS"
mkdir -p "$DN_BASE/output"
( cd "$DN_BASE" && "$MAKE" \
    LD="$NVCC" \
    CC="$CC" CXX="$CXX" \
    ADDOBJS="$BASE_ADDOBJS $OBJ/hyunjin_r72.o $OBJ/hyunjin_r72_cuda.o" \
    LIBS="$BASE_LIBS -L$CUDA_LIB -lcudart -lrt -lpthread -lm" \
    LDFLAGS="$BASE_LDFLAGS $RUNPATH_LDFLAGS" \
    dnetc 2>&1 | tee "$OUTDIR/make.log" )

BIN="$DN_BASE/dnetc"
[ -x "$BIN" ] || { echo "ERROR: no dnetc binary produced"; exit 1; }
cp -f "$BIN" "$OUTDIR/dnetc"

echo
echo "==> BUILD FINISHED: $OUTDIR/dnetc"
readelf -d "$OUTDIR/dnetc" | grep -E 'NEEDED.*cudart|RUNPATH|RPATH' | sed 's/^/    /'
