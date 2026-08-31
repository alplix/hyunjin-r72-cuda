Hyunjin RC5-72 CUDA core - dnetc-client-base integration
=========================================================

This directory contains a PATCH (not applied) that registers the Hyunjin
CUDA RC5-72 core into the distributed.net client so it can be selected
as the active RC5-72 core.

The patch is a standard unified diff against the top-level dnetc-client-base
tree.  Apply it with:

    patch -p1 < dnetc-r72-hyunjin-coresel.patch

or on Windows (git-bash / msys2):

    git apply dnetc-r72-hyunjin-coresel.patch

WHAT IT DOES
------------
1. Declares the shim       extern "C" s32 CDECL rc5_72_unit_func_hyunjin(...)
2. Appends a "Hyunjin CUDA 4-pipe" entry to the core name table for
   CPU_AMD64, CPU_ARM64 and CPU_X86.
3. Appends a matching "case" in selcoreSelectCore_rc572 for those three
   architectures that route core selector N to hyunjin_r72.c's entry point.

Because the core is a CPU-hosted core that drives the GPU (a normal .exe
client), it is registered on the *host* CPU architectures rather than the
CPU_CUDA/CPU_OPENCL client variants.

The Hyunjin core is appended at the END of each architecture's list so that
all pre-existing core indices are preserved and the default core selection is
unchanged.

SELECTING THE CORE AT RUNTIME
-----------------------------
distributed.net picks a core by index for the RC5_72 contest.  To make the
client use Hyunjin, set in the `[rc5-72]` section of dnetc.ini:

    core = <the appended index>

(verified on the Windows build: `core = 5`; the ini key is "core", see
common/confrwv.cpp -- the older "key" name is not read by this build.)

The patch writes the appended index determination into the build script
(scripts/build-linux-x86_64.sh sets -cputype accordingly), or you can force it
with the distributed.net environment override if supported by your build.
For the included Makefile builds, the Hyunjin core is made the *default* so
the freshly built client uses it without extra config.
