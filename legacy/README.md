# Legacy CUDA-Fortran implementation

This directory holds the **original CUDA-Fortran** implementation of the
Hyunjin core, plus the old C shim and the WSL build notes that validated the
Fortran core on an RTX 5070 Ti.

| File | Role |
|------|------|
| `hyunjin_r72_device.cuf` | CUDA-Fortran GPU kernel + crypto |
| `hyunjin_r72_host.cuf` | CUDA-Fortran host driver (`hyunjin_r72_run`, bind(C)) |
| `hyunjin_r72_math.f90` | pure-Fortran CPU reference (for validation) |
| `hyunjin_r72.c` | old C shim (superseded by the corrected C++ shim) |
| `WSL-SETUP-NOTES.md` | how the Fortran core was built + validated in WSL2 |

## Why this is legacy

CUDA Fortran (`.cuf`) requires the **NVIDIA HPC SDK** (`nvfortran`), which is
**not distributed for Windows**. Since Windows x64 is the primary Moo! Wrapper
target, the CUDA-Fortran core could not be linked into a native `dnetc.exe`.

It was therefore ported 1:1 to **CUDA-C** (`src/hyunjin_r72_cuda.cu` +
`src/hyunjin_r72.cpp`) and — separately — to **CUDA.jl** (`julia/`). Both ports
produce bit-identical RC5-72 results and both pass the official self-test
vectors. This directory is kept for reference and for anyone who wants to
build with the HPC SDK on Linux/WSL.

## Building (Linux/WSL only, requires nvfortran)

```bash
nvfortran -O3 -cpp -gpu=cc80,cc90,cc100,cc120 \
    -o hyunjin_selftest \
    legacy/hyunjin_r72_device.cuf \
    legacy/hyunjin_r72_math.f90 \
    legacy/hyunjin_r72_host.cuf \
    test/hyunjin_selftest.cuf
./hyunjin_selftest
```
