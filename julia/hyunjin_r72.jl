# =============================================================================
# hyunjin_r72.jl
#
# Hyunjin - modern CUDA (CUDA.jl) RC5-72 core for distributed.net / Moo! Wrapper
#
# Coded by Alperen Yavuz
#
# CUDA.jl port of the validated CUDA-C core (src/hyunjin_r72_cuda.cu).
# The CUDA-C version is itself a port of the original CUDA-Fortran core
# (legacy/hyunjin_r72_{host,device}.cuf).  This Julia implementation keeps
# the exact same RC5-72 enumeration order, endian layout and callable host
# driver, so it produces bit-identical results to the C/CUDA cores.
#
# RCS5-72 = RC5-32/12/16 with a 72-bit key.  Each work unit carries an
# IV-mixed plaintext block, a cyphertext block and a 72-bit starting key
# L0 {hi,mid,lo}.  The core iterates L0 upward and finds the key whose
# encryption of "plain" equals "cypher".
#
# This module is self-contained: it exposes a CPU reference (for cross
# validation) and a CUDA GPU kernel plus a host driver `hyunjin_r72_run`
# mirroring the C ABI used by the dnetc shim.  Julia's runtime cannot be
# embedded into the distributed.net client the way the C/CUDA-C core can,
# so this GPU core is intended as a standalone research/validation
# implementation, not for in-client use.
#
# For use in distributed.net projects only.
# Any other distribution or use of this source violates copyright.
# =============================================================================
module HyunjinR72

using CUDA

export hyunjin_r72_run, r72_encrypt, r72_add, hy_rotl, hy_bswap32,
       hy_increment_l0, RESULT_WORKING, RESULT_NOTHING, RESULT_FOUND

# --- dnetc Resultcode values (mirror common/ccoreio.h) ---------------------
const RESULT_WORKING = Int32(0)
const RESULT_NOTHING = Int32(1)
const RESULT_FOUND   = Int32(2)

# --- core tuning ------------------------------------------------------------
const PIPELINE    = 4                       # keys per thread per kernel pass
const BLOCK_SIZE  = 256                     # threads / block
const MAX_GRID    = 65535                   # max blocks / launch

# --- RC5 constants (unsigned 32-bit, bit-identical to Fortran int32) -------
const RC5_P = UInt32(0xB7E15163)
const RC5_Q = UInt32(0x9E3779B9)

# --- base arithmetic helpers (CPU + GPU) -----------------------------------

# ROTL32(x,n): rotate a 32-bit word left by n bits (n applied mod 32).
@inline hy_rotl(x::UInt32, n::Int32) = begin
    m = n & Int32(31)
    m == 0 ? x : (x << m) | (x >> (32 - m))
end
@inline hy_rotl(x::UInt32, n::UInt32) = hy_rotl(x, Int32(Int(n & UInt32(31))))

# Byte-swap a 32-bit word.
@inline hy_bswap32(x::UInt32) =
    ((x & 0x000000FF) << 24) |
    ((x & 0x0000FF00) << 8) |
    ((x & 0x00FF0000) >> 8) |
    ((x & 0xFF000000) >> 24)

# Add `amount` to the 72-bit key {hi,mid,lo} using the EXACT dnetc RC5-72
# enumeration order.  dnetc lays the 72-bit little-endian key out as
#     K = hi + bswap(mid)*2^8 + bswap(lo)*2^40
# and cascades a carry from "hi" (least significant) through "mid" then "lo".
# This mirrors rc5-72/ansi/r72-ref.cpp so the search keyspace matches dnetc.
# Returns (hi, mid, lo).
@inline function hy_increment_l0(hi::UInt32, mid::UInt32, lo::UInt32, amount::UInt32)
    Hs  = UInt64(hi)
    M64 = UInt64(hy_bswap32(mid))
    Bs  = UInt64(hy_bswap32(lo))
    mask40 = (UInt64(1) << 40) - 1

    lo64 = Hs + (M64 << 8)                 # bits 0..39
    lo64 += UInt64(amount)
    carry = lo64 >> 40                     # to bits 40+
    lo64 &= mask40
    Bc = Bs + carry                        # 32-bit mod 2^32

    nhi  = UInt32(lo64 & 0xFF)
    nmid = hy_bswap32(UInt32((lo64 >> 8) & 0xFFFFFFFF))
    nlo  = hy_bswap32(UInt32(Bc & 0xFFFFFFFF))
    return (nhi, nmid, nlo)
end

# Decode the 72-bit key {hi,mid,lo} to an integer in dnetc's enumeration
# order:  K = hi + bswap(mid)*2^8 + bswap(lo)*2^40.
@inline function r72_decode(hi::UInt32, mid::UInt32, lo::UInt32)
    K = UInt128(hi)
    K |= (UInt128(hy_bswap32(mid)) << 8)
    K |= (UInt128(hy_bswap32(lo)) << 40)
    return K
end

# Encode an integer K (0..2^72-1) back to {hi,mid,lo} in the same order.
@inline function r72_encode(K::UInt128)
    hi  = UInt32(K & 0xFF)
    mid = hy_bswap32(UInt32((K >> 8) & 0xFFFFFFFF))
    lo  = hy_bswap32(UInt32((K >> 40) & 0xFFFFFFFF))
    return (hi, mid, lo)
end

# Add a signed delta to the 72-bit key (host-side, for the reference checks).
# Works for arbitrarily large positive or negative deltas (mod 2^72).
@inline function r72_add(hi::UInt32, mid::UInt32, lo::UInt32, delta::Integer)
    K = r72_decode(hi, mid, lo)
    d = Int128(delta)
    modulus = UInt128(1) << 72
    if d >= 0
        D = UInt128(d)
    else
        D = modulus - UInt128(-d)   # congruent to d (mod 2^72)
    end
    K = (K + D) & (modulus - 1)
    return r72_encode(K)
end

# CPU reference for RC5-32/12/16 encryption of the (IV-mixed) plaintext block
# with a 72-bit key {kmhi,kmmid,kmlo}.  Returns (lo, hi) ciphertext words.
# Mirrors the RC5 kernel exactly; used for cross-validating the GPU core.
function r72_encrypt(kmhi::UInt32, kmmid::UInt32, kmlo::UInt32,
                     plain_lo::UInt32, plain_hi::UInt32)
    S = Vector{UInt32}(undef, 26)
    L = Vector{UInt32}(undef, 3)
    for it in 0:25
        S[it+1] = RC5_P + UInt32(it) * RC5_Q
    end
    L[1] = kmlo; L[2] = kmmid; L[3] = kmhi

    A = UInt32(0); B = UInt32(0)
    si = 0; lj = 0
    for it in 0:77
        a_tmp = S[si+1] + A + B
        S[si+1] = hy_rotl(a_tmp, Int32(3))
        A = S[si+1]
        b_tmp = L[lj+1] + A + B
        L[lj+1] = hy_rotl(b_tmp, (A + B) & UInt32(0x1F))
        B = L[lj+1]
        si = (si + 1) % 26
        lj = (lj + 1) % 3
    end

    A = plain_lo + S[1]
    B = plain_hi + S[2]
    for i in 0:11
        A = hy_rotl(A ⊻ B, B & UInt32(0x1F)) + S[3 + 2*i]
        B = hy_rotl(B ⊻ A, A & UInt32(0x1F)) + S[4 + 2*i]
    end
    return (A, B)
end

# -----------------------------------------------------------------------------
# GPU kernel
#
# Each thread tests PIPELINE consecutive keys, starting from L0 +
# pipe*(linear_index).  Per-thread result words hold 2-bit fields:
#   bit(2k)   -> partial match (A == cypher_lo)
#   bit(2k+1) -> exact match   (B == cypher_hi as well)
# (single-thread-owner writes, so a plain | is safe).
# -----------------------------------------------------------------------------
function hyunjin_r72_kernel!(results, plain_hi::UInt32, plain_lo::UInt32,
                             cypher_hi::UInt32, cypher_lo::UInt32,
                             L0_hi::UInt32, L0_mid::UInt32, L0_lo::UInt32,
                             process_amount::UInt32)
    bx  = blockIdx().x - 1
    tx  = threadIdx().x - 1
    bd  = blockDim().x
    idx = bx * bd + tx                  # 0-based

    if idx >= Int(process_amount)
        return nothing
    end

    base = UInt32(idx) * UInt32(PIPELINE)

    S = Vector{UInt32}(undef, 26)
    L = Vector{UInt32}(undef, 3)

    for k in 0:(PIPELINE-1)
        # S[] greedy table
        for it in 0:25
            S[it+1] = RC5_P + UInt32(it) * RC5_Q
        end

        # L[1]=lo, L[2]=mid, L[3]=hi (72-bit key), stepped by base+k
        L[1] = L0_lo
        L[2] = L0_mid
        L[3] = L0_hi
        nhi, nmid, nlo = hy_increment_l0(L[3], L[2], L[1], base + UInt32(k))
        L[3] = nhi; L[2] = nmid; L[1] = nlo

        # RC5 key expansion (3*t = 78 steps) on S[1..26] and L[1..3]
        A = UInt32(0); B = UInt32(0)
        si = 0; lj = 0
        for it in 0:77
            a_tmp = S[si+1] + A + B
            S[si+1] = hy_rotl(a_tmp, Int32(3))
            A = S[si+1]

            b_tmp = L[lj+1] + A + B
            L[lj+1] = hy_rotl(b_tmp, (A + B) & UInt32(0x1F))
            B = L[lj+1]

            si = (si + 1) % 26
            lj = (lj + 1) % 3
        end

        # RC5-32/12/16 encryption (12 rounds)
        A = plain_lo + S[1]
        B = plain_hi + S[2]
        for i in 0:11
            A = hy_rotl(A ⊻ B, B & UInt32(0x1F)) + S[3 + 2*i]
            B = hy_rotl(B ⊻ A, A & UInt32(0x1F)) + S[4 + 2*i]
        end

        # match check
        if A == cypher_lo
            results[idx + 1] |= (UInt32(1) << (2 * k))
            if B == cypher_hi
                results[idx + 1] |= (UInt32(2) << (2 * k))
            end
        end
    end
    return nothing
end

# -----------------------------------------------------------------------------
# Host-side driver.  Mirrors the C ABI of `hyunjin_r72_run` in
# src/hyunjin_r72_cuda.cu so the dnetc shim call sequence can be validated.
#
#   status, iterations_out, n_check, check{hi,mid,lo}, found{hi,mid,lo},
#       res_l0{hi,mid,lo} = hyunjin_r72_run(plain_hi, plain_lo,
#             cypher_hi, cypher_lo, l0_hi, l0_mid, l0_lo, iterations_in,
#             device, block_size)
#
# Returns RESULT_FOUND / RESULT_NOTHING, or -1 on error.
# -----------------------------------------------------------------------------
function hyunjin_r72_run(plain_hi::UInt32, plain_lo::UInt32,
                         cypher_hi::UInt32, cypher_lo::UInt32,
                         l0_hi::UInt32, l0_mid::UInt32, l0_lo::UInt32,
                         iterations_in::Integer, device::Integer,
                         block_size::Integer)
    if !CUDA.functional()
        return (-1, Int64(0), UInt32(0), UInt32(0), UInt32(0), UInt32(0),
                UInt32(0), UInt32(0), UInt32(0), UInt32(0), UInt32(0), UInt32(0))
    end

    nthreads = Int(block_size)
    if nthreads < 32
        nthreads = BLOCK_SIZE
    elseif nthreads > 1024
        nthreads = 1024
    end

    max_threads = nthreads * MAX_GRID
    d_results = CUDA.zeros(UInt32, max_threads)

    base_hi = l0_hi; base_mid = l0_mid; base_lo = l0_lo
    done = Int64(0)
    ret = RESULT_NOTHING
    n_check = UInt32(0)
    check_hi = UInt32(0); check_mid = UInt32(0); check_lo = UInt32(0)
    found_hi = UInt32(0); found_mid = UInt32(0); found_lo = UInt32(0)
    iterations_out = Int64(0)
    iters_64 = Int64(iterations_in)

    process_amount_units = max_threads

    while done < iters_64
        remain = iters_64 - done
        keys_this_pass = min(remain, Int64(max_threads) * PIPELINE)
        nthreads_this_pass = (keys_this_pass + PIPELINE - 1) ÷ PIPELINE
        grid = (nthreads_this_pass + nthreads - 1) ÷ nthreads

        # clear device buffer (only the active prefix needs clearing)
        CUDA.fill!(d_results, UInt32(0))

        CUDA.@cuda blocks=grid threads=nthreads hyunjin_r72_kernel!(
            d_results, plain_hi, plain_lo, cypher_hi, cypher_lo,
            base_hi, base_mid, base_lo, UInt32(nthreads_this_pass))
        CUDA.synchronize()

        h_res = Array(d_results[1:nthreads_this_pass])

        # scan results for partial / exact matches
        for j in 1:nthreads_this_pass
            h_j = h_res[j]
            if h_j == 0
                continue
            end
            for k in 0:(PIPELINE-1)
                if (h_j & (UInt32(1) << (2 * k))) != 0
                    n_check += 1
                    offset = Int64(PIPELINE) * (j - 1) + k
                    thi, tmid, tlo = hy_increment_l0(base_hi, base_mid, base_lo,
                                                     UInt32(offset))
                    check_hi = thi; check_mid = tmid; check_lo = tlo
                    if (h_j & (UInt32(2) << (2 * k))) != 0
                        found_hi = thi; found_mid = tmid; found_lo = tlo
                        iterations_out = done + offset
                        ret = RESULT_FOUND
                        break
                    end
                end
            end
            if ret == RESULT_FOUND
                break
            end
        end
        if ret == RESULT_FOUND
            break
        end

        # advance base key by the number of keys actually processed
        amt = UInt32(nthreads_this_pass * PIPELINE)
        thi, tmid, tlo = hy_increment_l0(base_hi, base_mid, base_lo, amt)
        base_hi = thi; base_mid = tmid; base_lo = tlo
        done += Int64(nthreads_this_pass) * PIPELINE
    end

    if ret == RESULT_NOTHING
        iterations_out = done
    end

    return (ret, iterations_out, n_check, check_hi, check_mid, check_lo,
            found_hi, found_mid, found_lo, base_hi, base_mid, base_lo)
end

end # module HyunjinR72

