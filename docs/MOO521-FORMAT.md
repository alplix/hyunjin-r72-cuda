# Moo! Wrapper RC5-72 buffer-file format (`moo521`)

This document reverse-engineers and documents the per-workunit binary buffer
format that the **Moo! Wrapper** (`http://moowrap.net`) feeds to and reads
back from the `dnetc` client it launches. The format is produced/consumed by
the Moo custom client build `2.9112-521-CTR` (the `-521` fork). Understanding
it is required to make a custom RC5-72 client (such as the Hyunjin CUDA-C
core build) interoperate with real Moo! Wrapper work units.

The companion codec lives in `common/moo521.h` / `common/moo521.cpp` and was
verified byte-exact against real `in.r72` / `out.r72` files.

> For use in distributed.net projects only.

---

## 1. Top-level layout

```
[ 32-byte header ][ packet 0 (176 bytes) ][ packet 1 (176 bytes) ] ...
```

No packet can exceed 176 bytes; a file's packet count `N` is given by

```
N = (filesize - 32) / 176
```

The 32-byte header (all multi-byte fields **big-endian**):

| Offset | Size | Field  | Value / meaning                                  |
|--------|------|--------|--------------------------------------------------|
| 0      | 4    | magic  | `83 B6 34 1A` (fixed signature)                  |
| 4      | 4    | version| `00 00 00 48` == 72 (RC5 key width)              |
| 8      | 4    | count  | packet count `N` (big-endian u32)                |
| 12     | 20   | pad    | all zero                                         |

Example for a 3-packet file (560 bytes):

```
83 B6 34 1A 00 00 00 48 00 00 00 03  00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00 00
```

---

## 2. The 176-byte packet

Each packet is the **encrypted (obfuscated) on-disk form** of one RC5-72 work
unit/result. It splits as:

| Offset | Size | Field          |
|--------|------|----------------|
| 0      | 168  | ciphertext     | 42 dwords of the record body
| 168    | 4    | checksum       | on disk: `enc_d42( bswap32(8-pass checksum of body[0..167]) )`
| 172    | 4    | key / seed     | on disk: `bswap32( per-packet decipher key )`, **not** ciphered

The cipher window covers dwords **0..42** (bytes 0..171). So:
- bytes 168..171 (dword 42) hold the **encrypted** checksum; on decode,
  `bswap32( decrypt_dword42 )` equals the 8-pass checksum of the body.
- bytes 172..175 (dword 43) are the **raw** `bswap32(seed)` — they lie
  *outside* the 43-dword window and are not ciphered.

---

## 3. Record body field map (42 dwords, little-endian, offsets 0..167)

After decoding, the 168-byte body maps onto the RC5-72 unit fields. This was
confirmed by diffing an in-progress packet against a completed one:

| dword(s) | offset | field | in-progress example   | completed example (out.r72 rec0) |
|----------|--------|-------|-----------------------|----------------------------------|
| d0..d2   | 0      | `L0.key {hi,mid,lo}` | `AB000000 00E7DB8E 00000000` | `54000000 0E6EEE07 00000000` |
| d3..d4   | 12     | `iv {hi,lo}` | `007AF5F6 41D5974C` | same |
| d5..d6   | 20     | `plain {hi,lo}` | `756E6B6E 54686520` ("nknu ehT") | same |
| d7..d8   | 28     | `cypher {hi,lo}` | `5F74ECA6 E7AFFCBE` | same |
| d9..d10  | 36     | `keysdone {hi,lo}` | `00000000 00000000` | `01000000 20000000` |
| d11..d12 | 44     | `iterations {hi,lo}` | `40000000 00000000` | `01000000 00000000` |
| d13      | 52     | `randomsubspace` | `40050000` | `FFFF0000` |
| d14..d17 | 56     | `check {count,hi,mid,lo}` | `00000000 ...` | `01000000 54000000 0E6EEE07 33A62236` |
| d18..d19 | 72     | OGR-overlap union padding | zeros | zeros |
| d20..d41 | 80     | rest of the serialized WorkRecord | zeros | zeros |

> The 168-byte body is a **full serialized WorkRecord** (`sizeof(WorkRecord)==168`):
> `body[0..79]` is the `ContestWork` union, `body[80..83]` is `resultcode`,
> `body[84..147]` is `id[64]` (real Moo packets carry the worker id, e.g. the
> bytes at 84..102 decode to the ASCII string `"rc5@distributed.net"`), and
> `body[148..167]` is `contest, cpu, os, build, core`.  So `moo521_pack` /
> `moo521_unpack` copy all 168 bytes verbatim between the body and a
> `WorkRecord`.

The plaintext words store the RC5-72 test vector with each 32-bit half
**byte-swapped on disk**: the dword values `756E6B6E 54686520` equal the raw
byte string `"nknu ehT"` (little-endian), which corresponds to the ASCII
plaintext **"The unkn..."** once each word is byte-swapped; the cypher
`5F74ECA6 E7AFFCBE` is the matching RC5-72 test cyphertext. This confirms
these are the RC5-72 plain/cypher pair, carried exactly as the `-521` client
stores them.

> These are raw little-endian dword values as stored in the decoded body.
> **Byte order:** the whole 168-byte body is a serialized `WorkRecord` in
> **network (big-endian/htonl) order**, exactly like the stock buffer layer's
> on-disk records.  Applying `__switch_byte_order` (the RC5-72 `ntohl` branch,
> which swaps every dword of the `work` union) yields host-order values the
> core consumes.  This is proven by the RC5-72 known-answer check: after
> `ntohl` the plain words read `6E6B6E75 20656854` = ASCII `"unkn" "The "` =
> **"The unknown..."** — matching dnetc's stored plain&nbsp;> and the 
> `contest` field (`00 00 00 05`) is network-order of `RC5_72` (== 5).
> `common/buffpub.cpp` therefore calls `__switch_byte_order` on both the read
> and write Moo paths to keep `work` in the same byte order as stock.

---

## 4. Cipher

Each packet is obfuscated with a reversible per-packet stream cipher over a
**43-dword window** (bytes 0..171). Constants (from the `-521` disassembly):

```
Q     = 0x61C88647
DELTA = 0x481EAE9D     (keyschedule decrement per dword)
```

**Decode (disk → host)** — one dword at a time, `K` starts at `seed` and
decrements by `DELTA` each step:

```
decode_dword(x, K):
    x = bswap32(x)
    x = x + Q
    x = ~x
    x = x ^ K
    return bswap32(x)
```

**Encode (host → disk)** is the exact inverse:

```
encode_dword(x, K):
    x = bswap32(x)
    x = x ^ K
    x = ~x
    x = x - Q
    return bswap32(x)
```

Where `seed = bswap32( u32 at packet[172..175] )`, and for dword index `i`:

```
K(i) = seed - i * DELTA        (mod 2^32)
```

---

## 5. Checksum (8-pass)

The value stored at packet[168..171] is the byte-swap of an 8-pass rolling
checksum over the **42 dword** plaintext record body (bytes 0..167):

```
state = 0xB7E15163
repeat 8 times:
    for dword i in 0..41:
        x = bswap32( body[i] )
        x = x ^ state
        x = x - 0x61C88647
        state = x
result = state
```

On disk: `packet[168..171] = bswap32(result)` (before the cipher window turns
it into ciphertext; on decode `bswap32(decoded dword 42) == result`).

---

## 6. Verification status

The codec is confirmed against real files:

- **Full round-trip** (`encode∘decode == identity` on **all 176 bytes**,
  including the ciphered checksum dword 42 and the raw key slot 172..175):
  pass on every packet examined — 3/3 pristine `in.r72`, 22/22 client-written
  `out.r72`, 22/22 `cpu521/out.r72`.
- **Buffpub read/write symmetry** — the exact normalization the buffer layer
  uses (`moo521_unpack` → `contest=RC5_72` → `__switch_byte_order(net→host)`
  on read, and `__switch_byte_order(host→net)` → `moo521_pack` → `moo521_encode`
  on write) reproduces the original on-disk `work` section **byte-exactly**:
  pass 3/3 pristine `in.r72`. Trailing metadata (contest/cpu/os/build/core) is
  intentionally re-emitted as client host values by the read path's overrides,
  so it is excluded from byte-exactness (the client's own buffer lifecycle is
  self-consistent).
- **End-to-end `buffpub` integration** — linking the real `common/buffpub.cpp`
  Moo branches against the public API (`BufferCountFileRecords`,
  `BufferGetFileRecord`, `BufferPutFileRecord`) on a real Moo `in.r72`
  (`test/buffpub_e2e_test.cpp`): counts 3 packets, consumes all 3 via `GetFileRecord`
  with `contest==RC5_72`, `resultcode==RESULT_WORKING` and the KAT plaintext,
  then writes a result into a wrapper-style pre-created empty `out.r72` and
  reads it back intact. ALL PASS.
- **Checksum** (`bswap32(decrypted dword42) == moo521_checksum(body)`):
  pass 3/3 pristine `in.r72`, 22/22 `out.r72`.
- **Header** parse: pass (magic + big-endian version/count).

Standalone sanity harness (byte-exact against `wtest/in.r72`):

```
dec882.c  -> roundtrip_raw_eq=1  checkmatch=1  (packets 0..2)
```

---

## 7. Why a plain `buffpub.cpp` size tweak is *not* enough

The repo's `dnetc-client-base` is `2.9114-523` stock, whose buffer layer
(`common/buffpub.cpp`) assumes a flat array of `sizeof(WorkRecord)` records
(here 168 bytes) with **no** 32-byte header and **no** on-disk obfuscation;
on read it byte-swaps each dword with `ntohl`. The Moo `-521` format instead
uses a 32-byte header plus 176-byte **encrypted** packets. The underlying
168-byte body matches the 523 `bigcrypto` fields and, like stock, is kept in
**network order** (§3) — so the byte-swap step (`__switch_byte_order`) is the
same, only the size and encapsulation differ. Simply changing the record size
in `buffpub.cpp` would not work because it cannot decrypt the packet or
account for the header; it also stores the *encrypted* packet, not the record.
A correct integration needs either:

1. a `moo521`-aware reader/writer at the buffer layer that detects the magic
   header, decodes/encodes each 176-byte packet and maps `§3` fields onto the
   client's RC5-72 unit, or
2. an external daemon/translator that converts between the Moo `-521` files
   and the stock `WorkRecord` files the client natively handles.

The `common/moo521.*` codec in this document implements the format primitives
used by either approach.
