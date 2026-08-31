# =============================================================================
# selftest.jl
#
# Hyunjin - modern CUDA (CUDA.jl) RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# Standalone self-test / validation harness for the Julia CUDA core.  Run with:
#
#     julia --project=julia julia/selftest.jl
#
# It validates the CPU reference against the dnetc RC5-72 test vector, then
# runs the GPU kernel via the host driver and confirms it finds the known
# solution key at the expected iteration offset and behaves correctly on an
# empty range.  This mirrors the C/CUDA-C and CUDA-Fortran self-tests.
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================

include("hyunjin_r72.jl")
using CUDA
using .HyunjinR72

# ---------------------------------------------------------------------------
# dnetc selftest vector #0 (official RSA test case #1), common/selftest.cpp.
#
# solution key (mangled)   = { hi 85, mid FEE1D4C0, lo 53030CC9 }
# IV-mixed plain           = { hi 2FF17AF3, lo 3F3CA653 }
# cypher                   = { hi 2FB7852A, lo 562D285A }
# start key (mangled)      = { hi 00, mid 00E1D4C0, lo 53030CC9 }
# The core must step from the start key and find the mangled solution at
# iteration offset 65157.
# ---------------------------------------------------------------------------
const MAG_LO     = UInt32(0x53030CC9)
const MAG_MI     = UInt32(0xFEE1D4C0)
const MAG_HI     = UInt32(0x00000085)
const PLAIN_LO   = UInt32(0x3F3CA653)   # IV-mixed
const PLAIN_HI   = UInt32(0x2FF17AF3)   # IV-mixed
const START_LO   = UInt32(0x53030CC9)
const START_MI   = UInt32(0x00E1D4C0)
const START_HI   = UInt32(0x00000000)
const SOL_OFFSET = Int64(65157)
const CY_LO      = UInt32(0x562D285A)
const CY_HI      = UInt32(0x2FB7852A)

failures = 0

println("=========================================================")
println(" Hyunjin RC5-72 CUDA (Julia) core - coded by Alperen Yavuz")
println(" Self-test / validation harness")
println("=========================================================")

# --- 1) CPU reference against the known vector ------------------------------
clo, chi = HyunjinR72.r72_encrypt(MAG_HI, MAG_MI, MAG_LO, PLAIN_LO, PLAIN_HI)
println(string("   CPU encrypt: clo=", string(clo, base=16, pad=8),
              " chi=", string(chi, base=16, pad=8),
              " (expect cypher lo=562D285A hi=2FB7852A)"))
if clo == CY_LO && chi == CY_HI
    println("   [PASS] CPU RC5-72 reference matches known vector")
else
    println("   [FAIL] CPU RC5-72 reference mismatch")
    global failures += 1
end

# --- 2) key-increment round-trip (+1 then -1) -------------------------------
khi, kmid, klo = MAG_HI, MAG_MI, MAG_LO
khi, kmid, klo = HyunjinR72.r72_add(khi, kmid, klo, 1)
khi, kmid, klo = HyunjinR72.r72_add(khi, kmid, klo, -1)
if (khi, kmid, klo) == (MAG_HI, MAG_MI, MAG_LO)
    println("   [PASS] 72-bit key increment round-trip")
else
    println("   [FAIL] 72-bit key increment round-trip")
    global failures += 1
end

# --- 3) enumerate available CUDA devices -----------------------------------
if !CUDA.functional()
    println(" [ABORT] No CUDA device available.")
    exit(1)
end
devcount = length(CUDA.devices())
println("   CUDA devices detected: ", devcount)
if devcount <= 0
    println(" [ABORT] No CUDA device available.")
    exit(1)
end
device = 0

# --- 4) run the GPU core on the authentic dnetc test slice ------------------
iter_in = SOL_OFFSET + 512          # search a bit past the solution
status, iter_out, n_check, chk_hi, chk_mid, chk_lo,
    fnd_hi, fnd_mid, fnd_lo, rl0_hi, rl0_mid, rl0_lo =
    HyunjinR72.hyunjin_r72_run(
        PLAIN_HI, PLAIN_LO, CY_HI, CY_LO,
        START_HI, START_MI, START_LO,
        iter_in, device, 256)

println("   GPU core status=", status, " iterations_out=", iter_out)
println(string("   GPU found key = ", string(fnd_hi, base=16, pad=8),
              " ", string(fnd_mid, base=16, pad=8),
              " ", string(fnd_lo, base=16, pad=8)))
println(string("   expected key  = ", string(MAG_HI, base=16, pad=8),
              " ", string(MAG_MI, base=16, pad=8),
              " ", string(MAG_LO, base=16, pad=8)))
println("   partial matches (n_check) = ", n_check)

if status == HyunjinR72.RESULT_FOUND && iter_out == SOL_OFFSET &&
   fnd_hi == MAG_HI && fnd_mid == MAG_MI && fnd_lo == MAG_LO
    println("   [PASS] GPU core found the exact key at offset ", SOL_OFFSET)
else
    println("   [FAIL] GPU core did not find the expected key")
    global failures += 1
end

# --- 5) negative case: range with no solution -------------------------------
khi, kmid, klo = HyunjinR72.r72_add(MAG_HI, MAG_MI, MAG_LO, 8)  # start past
status, iter_out, n_check, chk_hi, chk_mid, chk_lo,
    fnd_hi, fnd_mid, fnd_lo, rl0_hi, rl0_mid, rl0_lo =
    HyunjinR72.hyunjin_r72_run(
        PLAIN_HI, PLAIN_LO, CY_HI, CY_LO,
        khi, kmid, klo, 8, device, 256)

if status == HyunjinR72.RESULT_NOTHING && iter_out == 8
    println("   [PASS] empty range returns RESULT_NOTHING, consumed=8")
else
    println(string("   [FAIL] empty-range behavior: status=", status,
                   " consumed=", iter_out))
    global failures += 1
end

# ---------------------------------------------------------------------------
println("---------------------------------------------------------")
if failures == 0
    println(" ALL TESTS PASSED - Hyunjin Julia core is sound.")
else
    println(" ", failures, " TEST(S) FAILED.")
    exit(1)
end
