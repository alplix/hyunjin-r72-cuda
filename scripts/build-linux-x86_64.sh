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
#   - NVIDIA CUDA toolkit (nvcc) 12.8 or newer for Blackwell (sm_120)
#   - gcc/g++, make
#   - an NVIDIA GPU + driver at runtime
#
# The CUDA-C core (src/hyunjin_r72_cuda.cu) and its C++ shim
# (src/hyunjin_r72.cpp) are the authoritative implementation; they are also
# what the Windows build compiles.  The CUDA-Fortran core in legacy/ is NOT
# used: it needs the NVIDIA HPC SDK (nvfortran).
#
# The client registers the Hyunjin core as the last entry of the CUDA core
# table, so select it with:
#     [rc5-72]
#     core=12
#
# Usage:
#   ./build-linux-x86_64.sh <path-to-dnetc-client-base> [outdir]
#
# Environment:
#   CUDA_INSTALL_PATH   toolkit root (default: autodetected under /usr/local)
#   HOSTCC              C compiler for the host side, when nvcc rejects the
#                       default one (CUDA 12.8 refuses gcc > 14)
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
DN_BASE="${1:-$SRC/dnetc-client-base}"
OUTDIR="${2:-$SRC/build/linux-x86_64}"

# The dnetc source is tracked without the executable bit (committed from
# Windows), so re-apply it to the build scripts before use.
find "$DN_BASE" -maxdepth 3 -type f \
  \( -name 'configure' -o -name 'tomake' -o -name 'imake' -o -name '*.sh' -o -name '*.pl' \) \
  -exec chmod +x {} + 2>/dev/null || true

MAKE="${MAKE:-make}"
mkdir -p "$OUTDIR"

export CUDA_INSTALL_PATH="${CUDA_INSTALL_PATH:-}"
if [ -z "$CUDA_INSTALL_PATH" ]; then
  for cand in /usr/local/cuda-* /usr/local/cuda /opt/cuda*; do
    if [ -x "$cand/bin/nvcc" ]; then CUDA_INSTALL_PATH="$cand"; break; fi
  done
fi
export CUDA_INSTALL_PATH
NVCC="$CUDA_INSTALL_PATH/bin/nvcc"
[ -x "$NVCC" ] || { echo "ERROR: nvcc not found; set CUDA_INSTALL_PATH"; exit 1; }
export PATH="$CUDA_INSTALL_PATH/bin:$PATH"

# The CUDA version decides both the configure target suffix and the -D value
# the client uses to sanity-check the runtime it links against.
CUDA_FULL="$("$NVCC" --version | sed -n 's/.*release \([0-9]\+\)\.\([0-9]\+\).*/\1.\2/p' | head -1)"
[ -n "$CUDA_FULL" ] || { echo "ERROR: could not parse nvcc version"; exit 1; }
CUDA_MAJOR="${CUDA_FULL%%.*}"
CUDA_MINOR="${CUDA_FULL##*.}"
CUDA_TARGET="linux-cuda${CUDA_MAJOR}${CUDA_MINOR}"
echo "==> toolkit     : $CUDA_INSTALL_PATH (CUDA $CUDA_FULL)"
echo "==> configure   : ./configure $CUDA_TARGET"

# nvcc checks the host compiler's version and refuses gcc > 14 on CUDA 12.8.
# Prefer an older supported gcc over nvcc's -allow-unsupported-compiler escape
# hatch, since the latter risks miscompiling the host side.
SHIM=""
if [ -z "${HOSTCC:-}" ]; then
  DEF_GCC_MAJOR="$(gcc -dumpversion 2>/dev/null | cut -d. -f1 || echo 0)"
  if [ "$DEF_GCC_MAJOR" -gt 14 ] 2>/dev/null; then
    for alt in gcc-14 gcc-13; do
      if command -v "$alt" >/dev/null; then
        SHIM="$OUTDIR/hostcc-shim"
        mkdir -p "$SHIM"
        ln -sf "$(command -v "$alt")" "$SHIM/gcc"
        [ -x "$(command -v "g++-${alt#gcc-}")" ] && \
          ln -sf "$(command -v "g++-${alt#gcc-}")" "$SHIM/g++"
        export PATH="$SHIM:$PATH"
        echo "==> host cc     : $alt ($(gcc -dumpversion)) via shim, default gcc is $DEF_GCC_MAJOR"
        break
      fi
    done
  fi
fi

echo "==> dnetc base  : $DN_BASE"
echo "==> output      : $OUTDIR"

# configure generates the Makefile but not the directory its rules write into.
mkdir -p "$DN_BASE/output"
( cd "$DN_BASE" && ./configure "$CUDA_TARGET" 2>&1 | tee "$OUTDIR/configure.log" )

( cd "$DN_BASE" && "$MAKE" 2>&1 | tee "$OUTDIR/make.log" )

BIN="$DN_BASE/dnetc"
[ -x "$BIN" ] || { echo "ERROR: no dnetc binary produced"; exit 1; }

cp -f "$BIN" "$OUTDIR/dnetc"
cp -f "$SRC/packaging/dnetc.ini" "$OUTDIR/dnetc.ini"

echo
echo "==> BUILD FINISHED: $OUTDIR/dnetc"
"$OUTDIR/dnetc" --version | sed -n 's/^dnetc /    /p'
echo "    Select the core with [rc5-72] core=12 in dnetc.ini,"
echo "    then run: ./dnetc -ini dnetc.ini -runoffline -multiok=1"
echo "    Use './dnetc -gpuinfo' to confirm the GPU is detected."
