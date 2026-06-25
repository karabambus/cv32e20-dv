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
*/

#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <stdint.h>

#define TEST_PASSED  *(volatile int *)0x20000000 = 123456789
#define TEST_FAILED  *(volatile int *)0x20000000 = 1

typedef unsigned char          u8;
typedef unsigned short         u16;
typedef unsigned int           u32;
typedef unsigned long long int u64;

static int err_cnt = 0;

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
        u16 g16 = *(u16 *)(src + o);
        chk64("load16", o, g16, expected_u64(src, o, 2));

        u32 g32 = *(u32 *)(src + o);
        chk64("load32", o, g32, expected_u64(src, o, 4));

        u64 g64 = *(u64 *)(src + o);
        chk64("load64", o, g64, expected_u64(src, o, 8));
    }

    //////////////////////////////////////////////////////////////
    // Misaligned STORES at every byte offset 0..15: store then read back
    // (both misaligned) and confirm the value round-trips.
    for (int o = 0; o < 16; o++) {
        u16 s16 = *(u16 *)(src + o);
        bzero(dst, 32);
        *(u16 *)(dst + o) = s16;
        chk64("store16", o, *(u16 *)(dst + o), s16);

        u32 s32 = *(u32 *)(src + o);
        bzero(dst, 32);
        *(u32 *)(dst + o) = s32;
        chk64("store32", o, *(u32 *)(dst + o), s32);

        u64 s64 = *(u64 *)(src + o);
        bzero(dst, 32);
        *(u64 *)(dst + o) = s64;
        chk64("store64", o, *(u64 *)(dst + o), s64);
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
