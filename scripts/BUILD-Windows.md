# Building the Hyunjin dnetc client on Windows (x64)

Hyunjin - modern CUDA-C RC5-72 core for distributed.net / Moo! Wrapper
Coded by Alperen Yavuz

This document describes how to build a custom distributed.net client on
Windows x64 with the Hyunjin **CUDA-C** core compiled in, using the NVIDIA
CUDA toolkit (`nvcc`) with the dnetc MSVC build (`makefile.vc`).

> **Note** These steps mirror exactly what produced
> `dist/windows-x86_64/dnetc_hyunjin_1.0_windows_x86_64.exe`. If you just want
> a ready-to-run client, use that binary instead of rebuilding.

## Prerequisites
- Visual Studio (MSVC) + Windows SDK (`nmake`, `link`, `cl`, `VsDevCmd`)
- NVIDIA CUDA toolkit (`nvcc`) + `cudart.lib`
- A CUDA-capable NVIDIA GPU (driver at runtime)

## Steps

### 1. Apply the core-registration patch
From a prompt at the repo root (git-bash recommended):

    git apply dnetc-integration/dnetc-r72-hyunjin-coresel.patch

or apply the same edits by hand to `common/core_r72.cpp` (see
`dnetc-integration/README.md`).

### 2. Put the core sources into the dnetc tree
Copy the authoritative CUDA-C core and shim into the CUDA source path the
`makefile.vc` expects:

    copy src\hyunjin_r72_cuda.cu   <dnetc>\rc5-72\cuda\
    copy src\hyunjin_r72.cpp       <dnetc>\rc5-72\cuda\

(`makefile.vc` already has rules for `hyunjin_r72_cuda.obj` from
`hyunjin_r72_cuda.cu` via `nvcc`, and `hyunjin_r72.obj` from
`hyunjin_r72.cpp` via `cl`, with both objects listed in the RC5-72 link group
and `cudart.lib` appended to `OPTS_LIBS`.)

### 3. Build
Set up an x64 MSVC environment and the CUDA path, then run:

    call "C:\...\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64
    set CUDA_PATH=C:\Program Files\NVIDIA GPU Computing Toolkit\CUDA\v13.3
    set PROCESSOR_ARCHITECTURE=AMD64
    nmake -nologo /f makefile.vc

A successful build produces `dnetc.exe` in the tree root (and object files in
`output\`). The Hyunjin core is core index #5 (`[rc5-72] core = 5`).

### 4. Configure and run the client
Put the binary plus the files from `packaging/` (or `dist/windows-x86_64/`)
side by side and run exactly as moowrap's wrapper does:

    dnetc_hyunjin_1.0_windows_x86_64.exe -ini dnetc.ini -runoffline -multiok=1

The client banner prints:
`Hyunjin RC5-72 CUDA core v1.0 - coded by Alperen Yavuz`.

## Validation on Windows
Validate the core on the same GPU:

    dnetc.exe -test rc5-72 5      -> 32/32 Tests Passed

Or build and run the standalone CUDA-C harness (no dnetc tree needed):

    nvcc -O3 test\hyunjin_selftest_c.c src\hyunjin_r72_cuda.cu -o hyunjin_selftest.exe
    hyunjin_selftest.exe          -> ALL TESTS PASSED

## Deploying as a BOINC anonymous app
See `packaging/` for `app_info.xml`, `app_config.xml`, `job.xml` and
`dnetc.ini`. Rename the binary/ini to the names referenced by `app_info.xml`
and `job.xml`, drop them into `<boinc>/projects/moowrap.net/`, and restart
BOINC. Add the anonymous-platform block to `cc_config.xml` if required.
