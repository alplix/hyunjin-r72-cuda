! ============================================================================
! hyunjin_r72_math.f90
!
! Hyunjin - modern CUDA-Fortran RC5-72 core for distributed.net / Moo! Wrapper
!
! Coded by Alperen Yavuz
!
! Pure-Fortran (CPU) reference implementation of RC5-32/12/16 with a 72-bit
! key.  Used for unit / cross-verification of the GPU kernel.  Self-contained
! and dependency-free (no CUDA), so it can be compiled with any Fortran
! compiler (nvfortran, gfortran, ifort) for validation on a CPU.
!
! For use in distributed.net projects only.
! Any other distribution or use of this source violates copyright.
! ============================================================================

module hyunjin_r72_math
  use, intrinsic :: iso_c_binding
  implicit none

  integer(c_int32_t), parameter :: P_CONST = int(Z'B7E15163', c_int32_t)
  integer(c_int32_t), parameter :: Q_CONST = int(Z'9E3779B9', c_int32_t)

contains

  ! Logical rotate-left on a 32-bit word (n applied mod 32).
  pure function r72_rotl(x, n) result(r)
    integer(c_int32_t), intent(in) :: x
    integer, intent(in) :: n
    integer(c_int32_t) :: r
    integer :: s
    s = iand(n, 31)
    if (s .eq. 0) then
      r = x
    else
      r = ior(ishft(x, s), ishft(x, s - 32))
    end if
  end function r72_rotl

  ! Encrypt a 64-bit block {phi,plo} with the 72-bit key {khi,kmid,klo},
  ! returning {chi,clo}.  RC5-32/12/16 key schedule + 12 rounds.
  pure subroutine r72_encrypt(khi, kmid, klo, plo, phi, clo, chi)
    integer(c_int32_t), intent(in) :: khi, kmid, klo, plo, phi
    integer(c_int32_t), intent(out) :: clo, chi
    integer(c_int32_t) :: S(0:25)
    integer(c_int32_t) :: L(0:2)
    integer(c_int32_t) :: A, B, a_tmp, b_tmp, A_save, B_save
    integer :: i, it, j

    ! S[] greedy table
    do i = 0, 25
      S(i) = P_CONST + i * Q_CONST
    end do

    ! L[0]=lo, L[1]=mid, L[2]=hi  (72-bit little-endian key)
    L(0) = klo
    L(1) = kmid
    L(2) = khi

    ! RC5 key schedule (3*t = 78 steps)
    A_save = 0
    B_save = 0
    i = 0
    j = 0
    do it = 1, 78
      a_tmp = S(i) + A_save + B_save
      S(i)  = r72_rotl(a_tmp, 3)
      A_save = S(i)
      b_tmp = L(j) + A_save + B_save
      L(j)  = r72_rotl(b_tmp, int(A_save + B_save, kind=4))
      B_save = L(j)
      i = mod(i + 1, 26)
      j = mod(j + 1, 3)
    end do

    A = plo + S(0)
    B = phi + S(1)
    do i = 2, 25, 2
      A = r72_rotl(ieor(A, B), int(B, kind=4)) + S(i)
      B = r72_rotl(ieor(B, A), int(A, kind=4)) + S(i + 1)
    end do

    clo = A
    chi = B
  end subroutine r72_encrypt

  ! Byte-swap a 32-bit word (reverse byte order).
  pure function r72_bswap32(x) result(r)
    integer(c_int32_t), intent(in) :: x
    integer(c_int32_t) :: r
    integer :: b0, b1, b2, b3
    b3 = iand(x, 255)                 ! low byte
    b2 = iand(ishft(x, -8), 255)
    b1 = iand(ishft(x, -16), 255)
    b0 = iand(ishft(x, -24), 255)     ! high byte
    r = int(ior(ior(ior(b0, ishft(b1, 8)), ishft(b2, 16)), ishft(b3, 24)))
  end function r72_bswap32

  ! Add "amount" to a 72-bit key (in place).
  !
  ! dnetc represents the 72-bit little-endian key as three fields with a
  ! NON-trivial byte layout, and enumerates keys by cascading a +1 from the
  ! "hi" byte (least significant) through "mid" and then "lo".  Numerically:
  !
  !     K = hi + bswap(mid) * 2^8 + bswap(lo) * 2^40
  !
  ! so adding "amount" is done on this 72-bit value and repacked.  This must
  ! match the reference rc5-72 core increment exactly, otherwise the core
  ! would search a misaligned keyspace and report wrong found keys.
  pure subroutine r72_add(hi, mid, lo, amount)
    integer(c_int32_t), intent(inout) :: hi, mid, lo
    integer(c_int32_t), intent(in) :: amount
    integer(c_int64_t) :: lo64, Bc, carry, mask40
    integer(c_int64_t) :: M64, Hs, Bs
    Hs  = int(hi, c_int64_t)
    M64 = iand(int(r72_bswap32(mid), c_int64_t), int(Z'FFFFFFFF', c_int64_t)) ! unsigned 32
    Bs  = iand(int(r72_bswap32(lo), c_int64_t), int(Z'FFFFFFFF', c_int64_t))  ! unsigned 32
    mask40 = ishft(1_c_int64_t, 40) - 1_c_int64_t

    lo64 = Hs + ishft(M64, 8)                  ! bits 0..39
    lo64 = lo64 + int(amount, c_int64_t)
    carry = ishft(lo64, -40)                   ! to bits 40+
    lo64 = iand(lo64, mask40)
    Bc = Bs + carry                            ! 32-bit (mod 2^32)

    hi  = int(iand(lo64, 255_c_int64_t), c_int32_t)
    mid = r72_bswap32(int(iand(ishft(lo64, -8), int(Z'FFFFFFFF', c_int64_t)), c_int32_t))
    lo  = r72_bswap32(int(iand(Bc, int(Z'FFFFFFFF', c_int64_t)), c_int32_t))
  end subroutine r72_add

end module hyunjin_r72_math
