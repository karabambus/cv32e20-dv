/*
** Illegal instruction directed test (CV32E20).
**
** Executes a known-illegal instruction and verifies the documented CV32E20
** trap behaviour:
**   - mcause == 2 (Illegal Instruction), and
**   - mtval  == the faulting instruction encoding (CV32E20 writes the actual
**     faulting instruction to mtval; see CV32E20 cs_registers reference).
**
** mcause/mtval are sticky, and the BSP exception handler (u_sw_irq_handler)
** logs the illegal instruction and advances mepc past it (mepc += 4 for this
** full-width encoding), so execution resumes at the csrr reads below and the
** trap state is still observable.
*/

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define TEST_PASSED  *(volatile int *)0x20000000 = 123456789
#define TEST_FAILED  *(volatile int *)0x20000000 = 1

/* 0xFFFFFFFF: opcode 0x7F is reserved -> illegal; low bits 0b11 -> full 32-bit,
 * so the handler skips exactly 4 bytes and resumes on the csrr instructions. */
#define ILLEGAL_ENCODING 0xFFFFFFFFu

int main(void)
{
    uint32_t mcause = 0, mtval = 0;

    printf("Illegal instruction directed test\n");
    fflush(0);

    __asm__ volatile(
        ".word 0xFFFFFFFF  \n"   /* illegal instruction -> trap + BSP skip */
        "csrr  %0, mcause  \n"
        "csrr  %1, mtval   \n"
        : "=r"(mcause), "=r"(mtval));

    printf("mcause = %u (expected 2)\n", mcause);
    printf("mtval  = 0x%08x (expected 0x%08x)\n", mtval, ILLEGAL_ENCODING);

    if (mcause == 2 && mtval == ILLEGAL_ENCODING) {
        printf("SUCCESS\n");
        TEST_PASSED;
        return 0;
    } else {
        printf("FAILURE\n");
        TEST_FAILED;
        return 1;
    }
}
