# Hyunjin — a brand new, from-scratch CUDA RC5-72 core

**Posted by Alperen Yavuz** · Coded by Alperen Yavuz

---

Hello distributed.net / Moo! Wrapper community,

I want to present **Hyunjin**, a completely original GPU core for the RC5-72
project. This is **not** a repackaging, fork, or port of any already-existing
RC5-72 GPU core. It was written from scratch as an independent, standalone
system: its own key-search kernel, its own host driver, its own dnetc shim and
its own BOINC integration. All that is shared with the wider ecosystem is the
standard RC5-72 work-unit format it interoperates with — everything else in
this project has its own separate design and codebase.

---

## What it is

A modern **CUDA-C** implementation of the RC5-72 (RC5-32/12/16, 72-bit key)
key-search algorithm for NVIDIA GPUs, delivered as a ready-to-run client for
the **Moo! Wrapper** BOINC project.

Supported hardware (compiled-in `GPU_ARCHS`):

| Architecture | NVIDIA families        | SM   |
|--------------|------------------------|------|
| Ampere       | RTX 30xx, A-series      | sm_80 |
| Hopper       | H100, H200, RTX 40xx   | sm_90 |
| Blackwell    | RTX 50xx               | sm_100 / sm_120 |

## Three equivalent implementations

All three produce **bit-identical results** and all pass the official RC5-72
self-test vectors:

1. **CUDA-C core** (`src/hyunjin_r72_cuda.cu` + `src/hyunjin_r72.cpp`) — the
   authoritative version, linked into a custom `dnetc` client for Moo! Wrapper.
2. **CUDA.jl port** (`julia/`) — a pure-Julia research implementation.
3. **CUDA Fortran (legacy)** (`legacy/`) — the original development version.

## Correctness

- The authentic dnetc RC5-72 test vector #0 is found
  (`85 FEE1D4C0 53030CC9` at iteration offset `65157`).
- `dnetc -test rc5-72 5` reports `32/32 Tests Passed` on the shipped Windows
  client (that is the Hyunjin core #5 in the core table).
- Standalone validator: `scripts/build-selftest-linux.sh` →
  `ALL TESTS PASSED - Hyunjin CUDA-C core is sound.`

## Performance notes

The kernel processes four keys per thread pipeline stage
(`HYUNJIN_PIPELINE = 4`), expands the key schedule per thread, and compresses
per-thread match bitsets so the host only transfers results when a match
occurred. RC5-72 is a key-agile cipher, so throughput is dominated by the key
schedule: the pipeline overlaps that expansion of the next batch with
encryption of the current one, which is where the Ampere/Hopper/Blackwell
architecture actually pays off.

## Ready-to-run packages

Everything you need is attached to the release:

- **Windows x86_64** — `dnetc_hyunjin_1.0_windows_x86_64.exe` + a ZIP with the
  full BOINC set.
- **Linux x86_64** — tarball with client + BOINC files.
- **Linux aarch64 (arm64)** — tarball for ARM64/NVMe boards.

Each package contains the complete, end-user-ready BOINC anonymous-application
set:

```
app_info.xml   BOINC anonymous-app descriptor
app_config.xml resource guard (max_concurrent=1)
job.xml        launcher control file
dnetc.ini      client config, selects the Hyunjin core ([rc5-72] core = 5)
cc_config.xml  enables BOINC anonymous platform (use_anonymous_platform=1)
INSTALL.txt    step-by-step end-user install guide (incl. troubleshooting)
README.txt     technical notes
```

## Installing on BOINC (Moo! Wrapper) — quick start

1. Locate your BOINC data directory:
   - Windows: `C:\ProgramData\BOINC\`
   - Linux: `~/BOINC/` (or `/var/lib/boinc-client/`)
2. Copy *all* package files into the project slot
   `...\projects\moowrap.net\` (Linux: `projects/moowrap.net/`).
3. Put `cc_config.xml` (from the package) into the BOINC data directory root —
   it contains `<use_anonymous_platform>1</use_anonymous_platform>`. If a
   `cc_config.xml` already exists there, just add that one line.
4. Restart BOINC (or hit Scan) and wait for a Moo! Wrapper work unit.
5. Confirm the client is running: the slot's `stderr.txt` prints
   `Hyunjin RC5-72 CUDA core v1.0 - coded by Alperen Yavuz`.

## Where to find everything

- Repository: https://github.com/alplix/hyunjin-r72-cuda
- Release assets: https://github.com/alplix/hyunjin-r72-cuda/releases

Feedback, benchmark logs and architecture-specific tuning ideas are welcome.

— Alperen Yavuz