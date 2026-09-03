/*
 * moo521.{h,cpp} - decode/encode for the "Moo! Wrapper" RC5-72 buffer file
 * variant used by the distributed.net `dnetc` fork 2.9112-521-CTR that the
 * Moo! Wrapper (http://moowrap.net) launches per work unit.
 *
 * Copyright distributed.net 1997-2016 - All Rights Reserved
 * For use in distributed.net projects only.
 * Any other distribution or use of this source violates copyright.
 *
 * The Moo per-workunit format differs from the stock distributed.net
 * `in`/`out` buffer files in three ways:
 *
 *   1. A 32-byte header precedes the records (version/count are
 *      big-endian, matching the `-521` fork):
 *        [00..03] magic 83 B6 34 1A
 *        [04..07] version = 00 00 00 48 (== 72, the RC5 key width)
 *        [08..11] packet count N (big-endian u32)
 *        [12..31] 20 zero padding bytes
 *      followed by exactly N x 176-byte packets.
 *
 *   2. Each 176-byte packet carries the *encrypted* (obfuscated) form of a
 *      record:
 *        [0..167] 168 bytes = the 42-dword record body, encrypted by the
 *                 per-packet stream cipher below
 *        [168..171] ciphertext of the body's self-check dword (this dword IS
 *                 inside the 43-dword cipher window); on decode it equals
 *                 bswap32(moo521_checksum(body[0..167]))
 *        [172..175] the raw decipher key bswap32(seed) - NOT ciphered, since
 *                 dword 43 lies outside the 43-dword (bytes 0..171) window
 *
 *   3. Records are not the stock flat WorkRecord (168 bytes in 2.9114-523) -
 *      the Moo packet body is the plaintext/cypher/key/keysdone/etc. block,
 *      lightly obfuscated with a per-record XOR/add/subtract cipher (below)
 *      so the wrapper can detect tampering / corruption on re-upload.
 *
 * The cipher operates on a 43-dword window (bytes 0..171) and is a trivial
 * reversible stream.  `seed` is bswap32(packet[172..175]) and K(i)=seed-i*D:
 *
 *   D = 0x481EAE9D   (key-schedule decrement per dword)
 *   Q = 0x61C88647
 *   dec (disk->host) one dword:  x = bswap(x); x += Q; x = ~x; x ^= K(i); x = bswap(x)
 *   enc (host->disk) inverse:    x = bswap(x); x ^= K(i); x = ~x; x -= Q; x = bswap(x)
 *
 * None of this affects the RC5-72 key search itself - it is only the on-disk
 * container.  The record-body field map (key, iv, plain, cypher, keysdone,
 * iterations, randomsubspace, check) is detailed in docs/MOO521-FORMAT.md;
 * its field ORDER matches the 2.9114-523 `bigcrypto` at offsets 0..56.  The
 * 168-byte body is a serialized WorkRecord kept in network (big-endian/htonl)
 * byte order, exactly like the stock buffer layer's on-disk records - so the
 * buffer layer calls `__switch_byte_order` (the RC5-72 ntohl branch) on both
 * the read and write Moo paths, the same as for stock files.  What differs is
 * only the 32-byte header + 176-byte encrypted packet encapsulation.
 *
 * The 32-byte header/176-byte packet structure is what the Moo `dnetc -521`
 * fork writes/reads for its per-workunit `in.r72`/`out.r72` files.
 */

#ifndef __MOO521_H__
#define __MOO521_H__ "@(#)$Id: moo521.h,v 1.0 2016/03/13 alperen Exp $"

#include "cputypes.h"   /* u32, u8, etc. */
#include "ccoreio.h"    /* RC5_72UnitWork */
#include "problem.h"    /* WorkRecord, ContestWork (bigcrypto), RC5_72 */

#define MOO521_RECLEN      176   /* on-disk Moo packet size            */
#define MOO521_HEADERLEN   32    /* bytes before first packet          */
#define MOO521_MAGIC       0x1A34B683UL  /* bytes 83 B6 34 1A (LE)     */

/* exported API */

/* Returns 1 if `buf` looks like a Moo per-workunit header (magic match),
 * else 0.  Used to auto-detect the on-disk format. */
extern int moo521_is_header(const u8 *buf);

/* Given a 32-byte header, store the packet count into *count.  Returns 0 on
 * success, -1 if the header is not a valid Moo header. */
extern int moo521_header_count(const u8 *header, u32 *count);

/* Write a 32-byte Moo header for `count` packets into `header`. */
extern void moo521_make_header(u8 *header, u32 count);

/* 42-dword record checksum (the "check" dword pair that Moo compares).
 * `rec168w` must point to the 168-byte (42 dword) plaintext body.
 * Returns the value that is stored bswap'ed at bytes [168..171]. */
extern u32 moo521_checksum(const u8 *rec168);

/* Decode one 176-byte Moo packet -> 168-byte plaintext record body. */
extern void moo521_decode(const u8 *packet176, u8 *rec168);

/* Encode one 168-byte plaintext record body -> 176-byte Moo packet.
 * `seed` is the per-record key to use (bswap'ed into bytes [172..175]). */
extern void moo521_encode(const u8 *rec168, u32 seed, u8 *packet176);

/* Convenience pack/unpack between a stock WorkRecord and the 168-byte body.
 * sizeof(WorkRecord)==168, so the Moo body IS a full serialized WorkRecord:
 *   body[0..79]  = wr->work (ContestWork union; body[0..71]==bigcrypto,
 *                 offsets 0..71 per MOO521-FORMAT.md, the rest OGR-overlap
 *                 union padding),
 *   body[80..83] = resultcode,
 *   body[84..147]= id[64] (the d.net worker id - real Moo packets carry e.g.
 *                 "rc5@distributed.net"),
 *   body[148..167]= contest, cpu, os, build, core.
 * Both functions therefore copy all 168 bytes verbatim. */
extern void moo521_pack(const WorkRecord *wr, u8 *body168);
extern void moo521_unpack(const u8 *body168, WorkRecord *wr);

#endif /* __MOO521_H__ */
