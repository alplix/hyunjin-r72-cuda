# Architecture

This document describes the RC5-72 core, the three implementations and how
they relate, and the dnetc integration path.

## RC5-72 basics

RC5-72 is RC5-32/12/16 with a 72-bit key. A work unit carries:

- an **IV-mixed 64-bit plaintext** (`plain {hi,lo}`),
- a **64-bit cyphertext** (`cypher {hi,lo}`),
- a **72-bit starting key** `L0 {hi,mid,lo}`.

The core steps `L0` upward through the keyspace using dnetc's exact
enumeration order and, for each key, builds the RC5 key schedule and encrypts
the plaintext. When the low cipher word matches it records a **partial
match**; when both words match it records an **exact match** plus the winning
key. Each thread tests `HYUNJIN_PIPELINE = 4` consecutive keys and packs the
2-bit match flags for the 4 keys into one 32-bit result word, so the host only
downloads the result vector when at least one match occurred.

## The three implementations

```
                        legacy/  (CUDA Fortran, needs nvfortran)
                           │  ported, no Windows HPC SDK
                           ▼
src/hyunjin_r72_cuda.cu  (CUDA-C, authoritative) ──► dnetc.exe (Windows+Linux)
src/hyunjin_r72.cpp      (C++ shim)                ──► rc5_72_unit_func_hyunjin
         │
         ▼  ported 1:1
julia/hyunjin_r72.jl     (CUDA.jl, standalone research port)
```

All three share:

- the same 72-bit little-endian enumeration `K = hi + bswap(mid)*2^8 +
  bswap(lo)*2^40` (mirroring `rc5-72/ansi/r72-ref.cpp` so the searched keyspace
  matches dnetc),
- the same key schedule and 12-round RC5-32/12/16 encryption,
- the same 2-bit match-result packing and the same host-side scan.

## dnetc integration

The C++ shim `src/hyunjin_r72.cpp` implements the dnetc core interface

```c
s32 CDECL rc5_72_unit_func_hyunjin(RC5_72UnitWork *, u32 *iterations, void *);
```

It unpacks the work unit into the `hyunjin_r72_run(...)` argument list, calls
the CUDA-C host driver, and writes the results (consumed iterations, partial
/ exact match stats, advanced `L0`) back into the work-unit structure. It
**accumulates** `check.count` (`+=`), matching how dnetc's reference cores walk
the counter across `ProblemRun` calls.

The `dnetc-integration/dnetc-r72-hyunjin-coresel.patch` registers the core into
`common/core_r72.cpp`:

1. declares `rc5_72_unit_func_hyunjin`,
2. appends a `"Hyunjin CUDA 4-pipe"` name to the core-name table,
3. adds a `case` in `selcoreSelectCore_rc572` routing a core index to the Hyunjin
   entry point (appended at the end of each architecture's list so existing
   indexes are preserved).

The core is selected by the `[rc5-72] core = N` ini key.

## Key files

| File | Role |
|------|------|
| `src/hyunjin_r72_cuda.cu` | GPU kernel + C host driver (`hyunjin_r72_run`) |
| `src/hyunjin_r72.cpp` | dnetc C++ shim |
| `legacy/hyunjin_r72_*.cuf`, `legacy/hyunjin_r72_math.f90` | original CUDA Fortran |
| `julia/hyunjin_r72.jl` | CUDA.jl port |
| `test/hyunjin_selftest_c.c` | standalone CUDA-C validator |
| `julia/selftest.jl` | standalone Julia validator |
| `dnetc-integration/dnetc-r72-hyunjin-coresel.patch` | core registration |
