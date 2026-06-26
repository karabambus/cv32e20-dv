/*
** Misaligned load/store directed test (CV32E20).
**
** The CV32E20 LSU handles misaligned memory accesses in hardware by splitting
** them into multiple naturally-aligned transactions (CV32E20 Load-Store Unit
** reference: "at least two cycles are needed for misaligned loads and stores").
** There is NO misaligned-access exception (no exception code 4/6).
**
** A source buffer is filled with a known byte pattern (byte k == k), so the
** value of any u16/u32/u64 read at byte offset o is deterministic regardless of
** alignment.  This test verifies misaligned loads return the correct value and
** misaligned stores round-trip correctly.
**
** The misaligned accesses under test are issued via inline assembly
** (lhu/lw/sh/sw), not C typed-pointer dereferences -- see the detailed comment
** block on the access primitives below for why this is necessary.  The golden
** model (expected_u64) uses u8 access only, which is always alignment-1 and
** fully defined.
*/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdint.h>

#include "cv32e20_dv.h"

typedef unsigned char          u8;
typedef unsigned short         u16;
typedef unsigned int           u32;
typedef unsigned long long int u64;

static int err_cnt = 0;

/*
** ===========================================================================
** Misaligned access primitives -- why inline assembly?
** ===========================================================================
**
** The whole point of this test is to drive the CV32E20 LSU with loads and
** stores whose address is NOT naturally aligned, and to confirm the hardware
** splits each into the correct sequence of aligned bus transactions and
** returns/writes the right bytes.  To do that the test must guarantee that a
** misaligned wide access instruction (lhu/lw/sh/sw) actually reaches the LSU.
**
** Expressing the access in C as *(u16/u32/u64 *)(byte_ptr + odd_offset) does
** NOT guarantee that.  Converting a byte pointer that is not naturally aligned
** to a wider pointer type is undefined behavior (C17 6.3.2.3p7): the compiler
** is entitled to assume the alignment of the destination type, and is free to
** either (a) optimize using that false assumption, or (b) lower the access to
** a byte-at-a-time sequence (lbu/sb).  Path (b) is the dangerous one for a
** verification test: the comparison would still pass while the misaligned LSU
** path was never exercised -- a silently vacuous test.
**
** These helpers instead emit the exact instruction under test in inline asm,
** with the (misaligned) address supplied as a register operand.  At the ISA /
** hardware level a misaligned address is well-defined -- it is precisely the
** behavior we are verifying -- so there is no C UB and the codegen cannot
** decay to byte accesses.  Notes on the constraints:
**   - "r"(p)            : the runtime address lives in a register; the
**                         compiler never reasons about its low bits.
**   - "memory" clobber  : prevents reordering/elision of the access and
**                         forces the surrounding buffer state to be coherent.
**   - "=&r" (ld_u64)    : early-clobber so the first loaded word cannot be
**                         allocated to the same register as the address.
**   - u64 on RV32       : no native 64-bit load/store, so it is two word
**                         accesses at offsets 0 and 4 (each itself misaligned
**                         when p is misaligned), matching how the core's LSU
**                         would see a C-level 64-bit misaligned access.
*/
static inline u16 ld_u16(const void *p)
{
    u16 v;
    __asm__ volatile ("lhu %0, 0(%1)" : "=r"(v) : "r"(p) : "memory");
    return v;
}

static inline u32 ld_u32(const void *p)
{
    u32 v;
    __asm__ volatile ("lw %0, 0(%1)" : "=r"(v) : "r"(p) : "memory");
    return v;
}

static inline u64 ld_u64(const void *p)
{
    u32 lo, hi;
    __asm__ volatile ("lw %0, 0(%2)\n\t"
                      "lw %1, 4(%2)"
                      : "=&r"(lo), "=r"(hi) : "r"(p) : "memory");
    return ((u64)hi << 32) | lo;
}

static inline void st_u16(void *p, u16 v)
{
    __asm__ volatile ("sh %1, 0(%0)" :: "r"(p), "r"(v) : "memory");
}

static inline void st_u32(void *p, u32 v)
{
    __asm__ volatile ("sw %1, 0(%0)" :: "r"(p), "r"(v) : "memory");
}

static inline void st_u64(void *p, u64 v)
{
    u32 lo = (u32)v;
    u32 hi = (u32)(v >> 32);
    __asm__ volatile ("sw %1, 0(%0)\n\t"
                      "sw %2, 4(%0)" :: "r"(p), "r"(lo), "r"(hi) : "memory");
}

static u64 expected_u64(u8 *base, int off, int nbytes)
{
    u64 v = 0;
    for (int k = 0; k < nbytes; k++)
        v |= ((u64)base[off + k]) << (8 * k);
    return v;
}

static void chk64(const char *what, int off, u64 got, u64 exp)
{
    if (got != exp) {
        printf("FAIL %s off=%d got=%08x%08x exp=%08x%08x\n", what, off,
               (u32)(got >> 32), (u32)got, (u32)(exp >> 32), (u32)exp);
        err_cnt++;
    }
}

int main(void)
{
    printf("Misaligned load/store directed test\n");

    /* 32-byte source filled with byte k == k. */
    u64 srcbuf[4];
    u64 dstbuf[4];
    u8 *src = (u8 *)srcbuf;
    u8 *dst = (u8 *)dstbuf;
    for (int i = 0; i < 32; i++)
        src[i] = (u8)i;

    //////////////////////////////////////////////////////////////
    // Misaligned LOADS at every byte offset 0..15.
    for (int o = 0; o < 16; o++) {
        chk64("load16", o, ld_u16(src + o), expected_u64(src, o, 2));
        chk64("load32", o, ld_u32(src + o), expected_u64(src, o, 4));
        chk64("load64", o, ld_u64(src + o), expected_u64(src, o, 8));
    }

    //////////////////////////////////////////////////////////////
    // Misaligned STORES at every byte offset 0..15: store then read back
    // (both misaligned) and confirm the value round-trips.
    for (int o = 0; o < 16; o++) {
        u16 s16 = ld_u16(src + o);
        bzero(dst, 32);
        st_u16(dst + o, s16);
        chk64("store16", o, ld_u16(dst + o), s16);

        u32 s32 = ld_u32(src + o);
        bzero(dst, 32);
        st_u32(dst + o, s32);
        chk64("store32", o, ld_u32(dst + o), s32);

        u64 s64 = ld_u64(src + o);
        bzero(dst, 32);
        st_u64(dst + o, s64);
        chk64("store64", o, ld_u64(dst + o), s64);
    }

    //////////////////////////////////////////////////////////////
    printf("Done\n");
    if (err_cnt) {
        printf("FAILURE. %d errors\n", err_cnt);
        TEST_FAILED;
        return 1;
    } else {
        printf("SUCCESS\n");
        TEST_PASSED;
        return 0;
    }
}
