#!/usr/bin/env bash
# =============================================================================
# build-moo521-selftest-linux.sh
#
# Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# Builds and runs the standalone self-test for the Moo! Wrapper RC5-72 buffer
# codec (common/moo521.cpp).  Pure host C++, no CUDA/GPU and no dnetc client
# tree required.  It validates, against a captured real Moo! Wrapper
# `in.r72` packet, that the header layout, the per-packet cipher
# (encode/decode round-trip) and the 8-pass checksum are all correct.
#
# Usage:
#   ./build-moo521-selftest-linux.sh [dnetc-client-base] [outdir]
#
# Requires: a C++ compiler (g++/clang++).
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

set -euo pipefail

SRC="$(cd "$(dirname "$0")/.." && pwd)"
DN_BASE="${1:-$SRC/dnetc-client-base}"
OUT="${2:-$SRC/build/moo521-selftest}"
CXX="${CXX:-g++}"

command -v "$CXX" >/dev/null || { echo "ERROR: $CXX not found"; exit 1; }
mkdir -p "$OUT"

echo "==> compiling moo521 codec self-test"
"$CXX" -O2 -DCLIENT_OS=1 -DCLIENT_CPU=1 \
    -I"$DN_BASE/common" \
    -o "$OUT/moo521_codec_test" \
    "$SRC/test/moo521_codec_test.cpp" \
    "$DN_BASE/common/moo521.cpp"

echo "==> running moo521 codec self-test"
"$OUT/moo521_codec_test"
