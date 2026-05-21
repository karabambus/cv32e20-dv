/*
 * csr_discrepancy_test.c
 *
 * Verifies four RTL-vs-manual discrepancies found in CVE2 (CV32E20):
 *
 *  #1  cpuctrl (0x7C0): manual documents WARL CSR; RTL has no case
 *      handler -> illegal instruction on any access.
 *
 *  #2  Zicntr user-mode counter aliases (0xC00/0xC02/0xC80/0xC82):
 *      online manual implies accessible; RTL has no case handler
 *      -> illegal instruction.
 *
 *  #7  mhpmevent3-31 (0x323-0x33F): manual says WARL (writable);
 *      RTL write-case has no handler -> writes silently discarded,
 *      values are hardwired.
 *
 *  #8  mcause exception-code width: manual documents bits[4:0];
 *      RTL stores/exposes bits[5:0] (6-bit code).
 *
 * Build with the BSP; exc_handler.S overrides the weak u_sw_irq_handler.
 */

#include <stdio.h>
#include <stdlib.h>

/* Shared with exc_handler.S */
volatile int          glb_expect_illegal_insn = 0;
volatile unsigned int glb_last_mcause         = 0;

static int err_count = 0;

/* ------------------------------------------------------------------ */
/* Helper: attempt a CSR read that is expected to trap as illegal.     */
/* Sets the expect flag, issues the CSR instruction, then checks that  */
/* the handler fired (flag decremented to 0).                          */
/* The .option norvc / rvc pair is defensive; CSR instructions are     */
/* never 16-bit, but it avoids assembler surprises in the vicinity.    */
/* ------------------------------------------------------------------ */
#define CHECK_ILLEGAL_CSRR(addr, disc_num)                                  \
    do {                                                                    \
        unsigned int _dummy = 0xDEADBEEF;                                   \
        glb_expect_illegal_insn = 1;                                        \
        __asm__ volatile(                                                   \
            ".option norvc\n\t"                                             \
            "csrr %0, " #addr "\n\t"                                        \
            ".option rvc\n\t"                                               \
            : "=r"(_dummy));                                                \
        if (glb_expect_illegal_insn == 0) {                                 \
            printf("PASS  [#%d] CSRR 0x%03x -> illegal-insn trap\n",        \
                   disc_num, (unsigned)(addr));                             \
        } else {                                                            \
            printf("FAIL  [#%d] CSRR 0x%03x did NOT trap - register "       \
                   "accessible (value=0x%08x)\n",                           \
                   disc_num, (unsigned)(addr), _dummy);                     \
            err_count++;                                                    \
        }                                                                   \
    } while (0)

/* Same but for a write (csrwi with zero) */
#define CHECK_ILLEGAL_CSRWI(addr, disc_num)                                 \
    do {                                                                    \
        glb_expect_illegal_insn = 1;                                        \
        __asm__ volatile(                                                   \
            ".option norvc\n\t"                                             \
            "csrwi " #addr ", 0\n\t"                                        \
            ".option rvc\n\t");                                             \
        if (glb_expect_illegal_insn == 0) {                                 \
            printf("PASS  [#%d] CSRWI 0x%03x -> illegal-insn trap\n",       \
                   disc_num, (unsigned)(addr));                             \
        } else {                                                            \
            printf("FAIL  [#%d] CSRWI 0x%03x did NOT trap\n",              \
                   disc_num, (unsigned)(addr));                             \
            err_count++;                                                    \
        }                                                                   \
    } while (0)

/* ------------------------------------------------------------------ */

int main(void)
{
    printf("\n===== CVE2 CSR Discrepancy Tests =====\n\n");

    /* -------------------------------------------------------------- */
    printf("--- #1: cpuctrl (0x7C0) ---\n");
    printf("    Manual: WARL CSR; fields double_fault_seen(7) sync_exc_seen(6)\n");
    printf("    RTL:    address absent from case statement -> illegal_csr=1\n");
    printf("    Source: cve2_cs_registers.sv:469 (default branch)\n\n");

    CHECK_ILLEGAL_CSRR (0x7C0, 1);
    CHECK_ILLEGAL_CSRWI(0x7C0, 1);

    /* -------------------------------------------------------------- */
    printf("\n--- #2: Zicntr user-mode counter aliases ---\n");
    printf("    Manual: cycle/instret/cycleh/instreth readable from M-mode\n");
    printf("    RTL:    no 0xCxx address in case statement -> illegal_csr=1\n");
    printf("    Source: cve2_cs_registers.sv:469 (default branch)\n\n");

    CHECK_ILLEGAL_CSRR(0xC00, 2);   /* cycle     */
    CHECK_ILLEGAL_CSRR(0xC01, 2);   /* time      (also undocumented as absent) */
    CHECK_ILLEGAL_CSRR(0xC02, 2);   /* instret   */
    CHECK_ILLEGAL_CSRR(0xC03, 2);   /* hpmcounter3 */
    CHECK_ILLEGAL_CSRR(0xC80, 2);   /* cycleh    */
    CHECK_ILLEGAL_CSRR(0xC82, 2);   /* instreth  */

    /* -------------------------------------------------------------- */
    printf("\n--- #7: mhpmevent3-31 write behavior ---\n");
    printf("    Manual: WARL -software configures event selector bits\n");
    printf("    RTL:    write-case has no MHPMEVENT handler -> writes discarded\n");
    printf("    Source: cve2_cs_registers.sv:1193 (hardwired always_comb)\n\n");

    {
        unsigned int wval    = 0xFFFFFFFF;
        unsigned int readback;

        /* mhpmevent3 (0x323): bit 3 hardwired (MHPMCounterNum default=10, 3<13) */
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw 0x323, %1\n\t"
            "csrr %0,    0x323\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(wval));
        if (readback == 0x00000008) {
            printf("PASS  [#7] mhpmevent3: write 0xFFFFFFFF, read 0x%08x (hardwired bit3)\n",
                   readback);
        } else {
            printf("FAIL  [#7] mhpmevent3: write accepted? read 0x%08x (expected 0x00000008)\n",
                   readback);
            err_count++;
        }

        /* mhpmevent5 (0x325): bit 5 hardwired */
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw 0x325, %1\n\t"
            "csrr %0,    0x325\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(wval));
        if (readback == 0x00000020) {
            printf("PASS  [#7] mhpmevent5: write 0xFFFFFFFF, read 0x%08x (hardwired bit5)\n",
                   readback);
        } else {
            printf("FAIL  [#7] mhpmevent5: read 0x%08x (expected 0x00000020)\n", readback);
            err_count++;
        }

        /* mhpmevent13 (0x32D): inactive (index >= MHPMCounterNum+3=13) -> 0 */
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw 0x32D, %1\n\t"
            "csrr %0,    0x32D\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(wval));
        if (readback == 0x00000000) {
            printf("PASS  [#7] mhpmevent13 (inactive): write 0xFFFFFFFF, read 0x%08x (hardwired 0)\n",
                   readback);
        } else {
            printf("FAIL  [#7] mhpmevent13: read 0x%08x (expected 0x00000000)\n", readback);
            err_count++;
        }
    }

    /* -------------------------------------------------------------- */
    printf("\n--- #8: mcause exception-code bit width ---\n");
    printf("    Manual: exception code = bits[4:0] (5 bits)\n");
    printf("    RTL:    mcause_d = {wdata[31], wdata[5:0]} -> 6-bit code\n");
    printf("    Source: cve2_cs_registers.sv:174 (logic [6:0] mcause_q)\n");
    printf("            cve2_cs_registers.sv:323 (read: mcause_q[5:0])\n");
    printf("            cve2_cs_registers.sv:497 (write: wdata[5:0])\n\n");

    {
        unsigned int readback;

        /* Write 0x20 (bit 5 set, bits[4:0] clear, interrupt clear) */
        unsigned int test_val = 0x00000020;
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw mcause, %1\n\t"
            "csrr %0,     mcause\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(test_val));

        if (readback == 0x00000020) {
            printf("INFO  [#8] Write 0x00000020, read 0x%08x -"
                   "bit[5] preserved (RTL stores 6-bit code; undocumented)\n", readback);
        } else if (readback == 0x00000000) {
            printf("NOTE  [#8] Write 0x00000020, read 0x%08x -"
                   "bit[5] truncated (only 5-bit code stored)\n", readback);
            /* Mark as error: RTL analysis says bit5 IS stored */
            err_count++;
        } else {
            printf("UNEXPECTED [#8] mcause read 0x%08x after writing 0x00000020\n", readback);
            err_count++;
        }

        /* Write 0x3F (all of bits[5:0] set) */
        test_val = 0x0000003F;
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw mcause, %1\n\t"
            "csrr %0,     mcause\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(test_val));
        printf("INFO  [#8] Write 0x0000003F, read 0x%08x "
               "(expected 0x0000003F if 6-bit, 0x0000001F if 5-bit)\n", readback);

        /* Write interrupt + bit5 code */
        test_val = 0x80000020;
        __asm__ volatile(
            ".option norvc\n\t"
            "csrw mcause, %1\n\t"
            "csrr %0,     mcause\n\t"
            ".option rvc\n\t"
            : "=r"(readback) : "r"(test_val));
        printf("INFO  [#8] Write 0x80000020, read 0x%08x "
               "(interrupt bit + bit5 code)\n", readback);
    }

    /* -------------------------------------------------------------- */
    printf("\n===== Summary =====\n");
    if (err_count == 0) {
        printf("All checks passed -RTL matches the discrepancy analysis.\n\n");
        return EXIT_SUCCESS;
    } else {
        printf("%d check(s) failed.\n\n", err_count);
        return EXIT_FAILURE;
    }
}
