# Hyunjin — CUDA RC5-72 core for distributed.net / Moo! Wrapper

**Coded by Alperen Yavuz**

A modern, GPU-accelerated **RC5-72** key-search core for the
[distributed.net](http://distributed.net/) project (the successor to RC5-64),
targeting NVIDIA GPUs — **Ampere (sm_80)**, **Hopper (sm_90)** and
**Blackwell / RTX 50-series (sm_100 / sm_120)**.

> **This is an entirely original, from-scratch implementation — not a fork,
> port or derivative of any existing RC5-72 GPU core.** The crypto kernel,
> host plumbing, dnetc shim and the package/Moo! Wrapper integration were all
> written independently with a completely separate codebase and design of its
> own ("a completely different system"), sharing nothing with the stock
> distributed.net CUDA cores other than the public RC5-72 work-unit format it
> interoperates with.

The core exists in **three equivalent implementations** that all produce
bit-identical results and all pass the official RC5-72 self-test vectors:

| Implementation | Source | When to use |
|----------------|--------|-------------|
| **CUDA-C core (authoritative)** | `src/hyunjin_r72_cuda.cu` + `src/hyunjin_r72.cpp` | Linked into a custom `dnetc` client for the Moo! Wrapper. Buildable on Windows (nvcc + MSVC) and Linux (nvcc + g++). |
| **CUDA.jl port** | `julia/` (CUDA.jl) | Pure-Julia research / validation implementation with no C compilation. |
| **CUDA Fortran (legacy)** | `legacy/` | Original implementation. Requires the NVIDIA HPC SDK (`nvfortran`), which is **not available on Windows** — this is why the CUDA-C port exists. |

> For use in distributed.net projects only. Any other distribution or use of
> this source violates copyright.

---

## Overview

The real Moo! Wrapper deployment launches the official `dnetc` client against
per-workunit binary buffer files. The BOINC wrapper drops each work unit into
the job's slot directory and the client reads it through the standard dnetc
`in` / `out` buffer files. To extract maximum performance we build a **custom
`dnetc` client** with the CUDA-C core compiled in, called through the standard
dnetc `ccoreio.h` core interface.

```
  BOINC wrapper
    └─ job.xml ─ launcher: dnetc_hyunjin.exe -ini dnetc.ini -runoffline -multiok=1
         └─ dnetc client (RC5-72)
              └─ rc5_72_unit_func_hyunjin   (src/hyunjin_r72.cpp shim)
                   └─ hyunjin_r72_run       (src/hyunjin_r72_cuda.cu host driver)
                        └─ hyunjin_r72_kernel (CUDA device kernel)
```

---

## Validation status

Every implementation passes the authentic dnetc RC5-72 test vector #0
(solution key `85 FEE1D4C0 53030CC9` found at iteration offset `65157`):

- **CUDA-C core (source-level):** `dnetc.exe -test rc5-72 5` → `32/32 Tests Passed`
  on `dist/windows-x86_64/dnetc_hyunjin_1.0_windows_x86_64.exe`, plus the
  standalone harness:
  ```
  scripts/build-selftest-linux.sh      # builds+run test/hyunjin_selftest_c.c
  ALL TESTS PASSED - Hyunjin CUDA-C core is sound.
  ```
- **CUDA.jl port:** `julia --project=julia julia/selftest.jl`
  ```
  ALL TESTS PASSED - Hyunjin Julia core is sound.
  ```
- **CUDA Fortran (legacy):** verified before the CUDA-C port was created.

## Moo! Wrapper buffer-file format (`moo521`)

The Moo! Wrapper feeds each work unit to the client as a binary buffer file
(`in.r72`) in a format that differs from the stock distributed.net buffer
files: a **32-byte header** + **N x 176-byte packets**, each packet being the
*encrypted* (obfuscated) form of one RC5-72 unit with its own per-packet
cipher key and an 8-pass checksum.

`docs/MOO521-FORMAT.md` documents the full format (header, 176-byte packet,
record-body field map, cipher, checksum), and `common/moo521.{h,cpp}` provides
a verified codec that parses/encodes it. Validate it with
`scripts/build-moo521-selftest-linux.sh` (host-only, no CUDA/GPU needed).

---

## Repository layout

```
src/                   Authoritative CUDA-C core
  hyunjin_r72_cuda.cu  CUDA device kernel + host driver (hyunjin_r72_run)
  hyunjin_r72.cpp      C++ shim implementing rc5_72_unit_func_hyunjin
julia/                 CUDA.jl port (Project.toml, hyunjin_r72.jl, selftest.jl)
legacy/                Original CUDA Fortran (.cuf/.f90) + old C shim
test/
  hyunjin_selftest_c.c standalone self-test for the CUDA-C core
  hyunjin_selftest.cuf standalone self-test for the CUDA Fortran core
  moo521_codec_test.cpp standalone self-test for the Moo! Wrapper buffer codec
dnetc-integration/
  dnetc-r72-hyunjin-coresel.patch   registers the core into core_r72.cpp
  README.md
scripts/
  build-linux-x86_64.sh    Linux amd64 client build (nvcc + g++)
  build-linux-aarch64.sh   Linux arm64 cross build
  build-selftest-linux.sh  build + run the CUDA-C validator
  build-moo521-selftest-linux.sh  build + run the Moo! buffer codec validator
  BUILD-Windows.md         Windows x64 build guide
packaging/             BOINC anonymous-app files (app_info, job.xml, dnetc.ini)
dist/                  Ready-to-run binaries (windows-x86_64 included)
docs/                  Extended guides
  ARCHITECTURE.md
  MOO521-FORMAT.md     reverse-engineered Moo! Wrapper buffer-file format + codec
```

---

## Prerequisites

- **NVIDIA CUDA toolkit** (`nvcc`) — required for the CUDA-C core.
- **gcc / g++** (Linux) or **MSVC** (Windows) for the C++ shim and client.
- **CUDA-capable NVIDIA GPU + driver** at runtime.
- **make** and **patch** for the Linux build.
- **Julia 1.12+** only if you want to run the `julia/` port.

---

## Building the custom dnetc client

### Windows x64 (already built)

A ready-to-run Windows binary is shipped in
`dist/windows-x86_64/dnetc_hyunjin_1.0_windows_x86_64.exe` alongside its
`dnetc.ini`, `job.xml`, `app_info.xml` and `app_config.xml`. The binary's
RC5-72 core (#5) passes `32/32` self-tests. To rebuild from source, follow
`scripts/BUILD-Windows.md` (apply the patch, compile
`src/hyunjin_r72_cuda.cu` with `nvcc` and `src/hyunjin_r72.cpp` with `cl`, then
link into the dnetc `makefile.vc` build with `cudart.lib`).

### Linux amd64
```bash
scripts/build-linux-x86_64.sh /path/to/dnetc-client-base
```
Applies the registration patch, compiles the CUDA-C core with `nvcc` and the
shim with `g++`, configures dnetc for `x86_64`, and links with the CUDA
runtime.

### Linux arm64
```bash
CROSS_CC=aarch64-linux-gnu-gcc CROSS_CXX=aarch64-linux-gnu-g++ \
scripts/build-linux-aarch64.sh /path/to/dnetc-client-base
```
Cross build using an aarch64-capable `nvcc` and toolchain.

---

## Configuring and running

Run the client exactly as the Moo! Wrapper does:

```bash
dnetc_hyunjin_1.0_<arch>.exe -ini dnetc.ini -runoffline -multiok=1
```

On startup the client prints:

```
Hyunjin RC5-72 CUDA core v1.0 - coded by Alperen Yavuz
```

The core is selected through the `[rc5-72] core = N` key in `dnetc.ini`
(indexes: x86/AMD64 append `5`; see `dnetc-integration/README.md`).

---

## Deploying as a BOINC anonymous app

Drop the files in `packaging/` into `<boinc>/projects/moowrap.net/` and restart
BOINC:

- `app_info.xml`   — anonymous-app descriptor (targets the Moo! Wrapper)
- `app_config.xml` — optional resource guard
- `job.xml`        — Moo! Wrapper control file (runs the dnetc client)
- `dnetc.ini`      — client config (offline buffer mode, `core = 5`)

> See the deployment note in the docstring of this README's root section: the
> buffer file the client opens must match the logical `<open_name>` Moo's
> work-unit template stages into the slot directory.

---

## How the core works (brief)

RC5-72 is RC5-32/12/16 with a 72-bit key. Each work unit supplies an IV-mixed
64-bit plaintext, a 64-bit cyphertext and a 72-bit starting key `L0
{hi,mid,lo}`. The kernel expands the key via the standard RC5 schedule and
encrypts the plaintext; when the low word matches the cypher it reports a
partial match, and when both words match it reports an exact match plus the
winning key. Each thread tests `HYUNJIN_PIPELINE = 4` consecutive keys, and
results are compressed into per-thread bit-fields so the host only downloads
them when a match occurred.

---

## Disclaimer

The CUDA-C, CUDA Fortran and Julia sources are provided as reference
implementations for the distributed.net RC5-72 project. Always run the
self-test (`scripts/build-selftest-linux.sh` or `julia/selftest.jl`) first to
confirm the crypto math and GPU plumbing on your specific GPU before pointing
the client at real work units.
