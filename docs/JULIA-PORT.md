# The Julia (CUDA.jl) port

This document explains why the CUDA-Fortran core was replaced and how the
CUDA.jl port fits in.

## Background: why Fortran was a problem

The original Hyunjin core was written in **CUDA Fortran** (`.cuf`,
`legacy/hyunjin_r72_*.cuf` + `legacy/hyunjin_r72_math.f90`). CUDA Fortran is
only accepted by the **NVIDIA HPC SDK compiler** (`nvfortran`).

The NVIDIA HPC SDK is **not distributed for Windows** in a form that lets you
drive the final dnetc link, so the CUDA-Fortran core could not be built into a
native Windows `dnetc` client. That is a hard blocker for real Moo! Wrapper
deployment on Windows.

Two solutions were applied:

1. **A CUDA-C port** (`src/hyunjin_r72_cuda.cu`) — compiles on Windows with the
   conventional **nvcc + MSVC** toolchain. This is the **authoritative**
   implementation shipped inside the Windows and Linux binaries.
2. **A CUDA.jl port** (`julia/`) — a pure-Julia GPU implementation that needs
   no C/CUDA compilation at all, keeping the Fortran→modern-language migration
   the project set out to do.

Both produce bit-identical RC5-72 results.

## Running the Julia port

Prerequisites: Julia 1.12+, a CUDA-capable NVIDIA GPU + driver.

```bash
# instantiate the environment (first run installs CUDA.jl)
julia --project=julia -e 'using Pkg; Pkg.instantiate()'

# run the self-test
julia --project=julia julia/selftest.jl
```

Expected output ends with:

```
ALL TESTS PASSED - Hyunjin Julia core is sound.
```

## Module layout

- `julia/hyunjin_r72.jl` — module `HyunjinR72`:
  - CPU reference: `hy_rotl`, `hy_bswap32`, `hy_increment_l0`, `r72_add`,
    `r72_encode/decode`, `r72_encrypt` (cross-validation only).
  - GPU kernel: `hyunjin_r72_kernel!` (CUDA.jl).
  - Host driver: `hyunjin_r72_run(...)` mirroring the C ABI of the CUDA-C core.
- `julia/selftest.jl` — the self-test harness.
- `julia/Project.toml`, `julia/Manifest.toml` — Julia environment (CUDA.jl).

## Validation

`selftest.jl` runs the same checks as every other backend:

1. CPU reference encrypt matches the official vector.
2. 72-bit key increment round-trips (`+1` then `-1`).
3. CUDA device is present.
4. GPU kernel finds the exact key `85 FEE1D4C0 53030CC9` at offset `65157`.
5. Empty range returns `RESULT_NOTHING` and consumes the requested slice.

On this machine it runs against an **NVIDIA GeForce RTX 5070 Ti** (Blackwell,
sm_120) and passes all five checks.

## Integration note

Julia's runtime cannot be embedded into the `dnetc` C client the way the
CUDA-C core can (dnetc calls the core through a C ABI on every work slice,
which is incompatible with a per-call Julia runtime). The Julia port is
therefore a **standalone research / validation implementation**: same math,
same results, no in-client use. For production Moo! Wrapper deployment use the
CUDA-C core (`src/`).
