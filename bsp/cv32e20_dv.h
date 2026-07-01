// Copyright (c) 2026 Eclipse Foundation
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// cv32e20_dv.h - Shared definitions for the CV32E20 directed C test-programs.
//
// Single source of truth for:
//   * the canonical pass/fail signalling protocol, and
//   * the simulation testbench's memory-mapped "virtual peripheral" registers.
//
// The authoritative decode for every address below lives in the testbench at
// tb/core/mm_ram.sv; keep this header in step with that file.
//
// All test-programs are compiled with -I $(BSP) (see mk/Common.mk), so this
// header is reachable from any test with:  #include "cv32e20_dv.h"

#ifndef CV32E20_DV_H
#define CV32E20_DV_H

/* -------------------------------------------------------------------------
 * This header is shared by the C and the assembly directed test-programs.
 * The C body below is hidden from the assembler (gcc defines __ASSEMBLER__
 * when preprocessing a .S file); the assembly-only section at the end of the
 * file provides the matching TEST_PASS / TEST_FAIL macros.  Keeping both in
 * one file makes this the single source of truth for the pass/fail protocol.
 * ------------------------------------------------------------------------- */
#ifndef __ASSEMBLER__

#include <stdint.h>

/* -------------------------------------------------------------------------
 * Virtual-peripheral memory map (see tb/core/mm_ram.sv).
 * ------------------------------------------------------------------------- */
#define MM_PRINT_ADDR        0x10000000u  /* putchar port (write byte in LSB)   */

#define MM_TESTSTATUS_ADDR   0x20000000u  /* pass/fail status port              */
#define MM_EXIT_ADDR         0x20000004u  /* exit-code port                     */
#define MM_SIGBEGIN_ADDR     0x20000008u  /* signature region begin             */
#define MM_SIGEND_ADDR       0x2000000Cu  /* signature region end               */
#define MM_SIGDUMP_ADDR      0x20000010u  /* trigger signature dump             */

#define MM_TIMER_CTRL_ADDR   0x15000000u  /* IRQ mask / timer control           */
#define MM_TIMER_VAL_ADDR    0x15000004u  /* timer delay value                  */
#define MM_DEBUG_CTRL_ADDR   0x15000008u  /* debug request control              */
#define MM_SIG_VERSION_ADDR  0x15000020u  /* interrupt-generator version  (RO)  */
#define MM_SIG_PLATFORM_ADDR 0x15000024u  /* interrupt-generator platform (WO)  */
#define MM_RNDNUM_ADDR       0x15001000u  /* random-number source               */
#define MM_TICKS_ADDR        0x15001004u  /* free-running tick counter          */

#define MM_MTIMECMP_ADDR     0x02004000u  /* CLINT mtimecmp  (low 32)           */
#define MM_MTIMECMPH_ADDR    0x02004004u  /* CLINT mtimecmp  (high 32)          */
#define MM_MTIME_ADDR        0x0200BFF8u  /* CLINT mtime     (low 32)           */
#define MM_MTIMEH_ADDR       0x0200BFFCu  /* CLINT mtime     (high 32)          */

/* 32-bit volatile lvalue accessor for any of the addresses above. */
#define MM_REG(addr)         (*(volatile uint32_t *)(addr))

#define MM_PRINT_REG         MM_REG(MM_PRINT_ADDR)
#define MM_TESTSTATUS_REG    MM_REG(MM_TESTSTATUS_ADDR)
#define MM_EXIT_REG          MM_REG(MM_EXIT_ADDR)
#define MM_SIGBEGIN_REG      MM_REG(MM_SIGBEGIN_ADDR)
#define MM_SIGEND_REG        MM_REG(MM_SIGEND_ADDR)
#define MM_SIGDUMP_REG       MM_REG(MM_SIGDUMP_ADDR)
#define MM_TIMER_CTRL_REG    MM_REG(MM_TIMER_CTRL_ADDR)
#define MM_TIMER_VAL_REG     MM_REG(MM_TIMER_VAL_ADDR)
#define MM_DEBUG_CTRL_REG    MM_REG(MM_DEBUG_CTRL_ADDR)
#define MM_SIG_VERSION_REG   MM_REG(MM_SIG_VERSION_ADDR)
#define MM_SIG_PLATFORM_REG  MM_REG(MM_SIG_PLATFORM_ADDR)
#define MM_RNDNUM_REG        MM_REG(MM_RNDNUM_ADDR)
#define MM_TICKS_REG         MM_REG(MM_TICKS_ADDR)
#define MM_MTIMECMP_REG      MM_REG(MM_MTIMECMP_ADDR)
#define MM_MTIMECMPH_REG     MM_REG(MM_MTIMECMPH_ADDR)
#define MM_MTIME_REG         MM_REG(MM_MTIME_ADDR)
#define MM_MTIMEH_REG        MM_REG(MM_MTIMEH_ADDR)

/* -------------------------------------------------------------------------
 * Backward-compatible names retained for existing directed tests.
 * ------------------------------------------------------------------------- */
#define TIMER_REG_ADDR         ((volatile uint32_t *) MM_TIMER_CTRL_ADDR)
#define TIMER_VAL_ADDR         ((volatile uint32_t *) MM_TIMER_VAL_ADDR)
#define DEBUG_REQ_CONTROL_REG  (*(volatile int *) MM_DEBUG_CTRL_ADDR)

/* -------------------------------------------------------------------------
 * Canonical pass/fail signalling, decoded by tb/core/mm_ram.sv:
 *   write 123456789 to MM_TESTSTATUS -> "ALL TESTS PASSED"
 *   write 1         to MM_TESTSTATUS -> "TEST(S) FAILED!"
 *   any other value is ignored by the testbench.
 * ------------------------------------------------------------------------- */
#define TEST_RESULT_PASS 123456789
#define TEST_RESULT_FAIL 1

#define TEST_PASSED  (MM_TESTSTATUS_REG = TEST_RESULT_PASS)
#define TEST_FAILED  (MM_TESTSTATUS_REG = TEST_RESULT_FAIL)

#else /* __ASSEMBLER__ */

/* -------------------------------------------------------------------------
 * Assembly view of the canonical pass/fail protocol.
 *
 * These GAS macros are the assembly counterpart of the C TEST_PASSED /
 * TEST_FAILED macros above and signal end-of-test exactly the same way:
 * write the canonical value to the test-status port (decoded by
 * tb/core/mm_ram.sv) and then halt.  The literals are duplicated here
 * because the C macros above (with their 'u' suffixes and pointer casts)
 * are not assembler-parseable; keep these three numbers in step with
 * MM_TESTSTATUS_ADDR / TEST_RESULT_PASS / TEST_RESULT_FAIL above.
 *
 * Each macro clobbers t0/t1 only and never returns (it spins on wfi), so it
 * is safe to drop in at any end-of-test point.  Usage from a .S test:
 *
 *     #include "cv32e20_dv.h"
 *     ...
 *     TEST_PASS                 // or TEST_FAIL
 *
 * Tests whose existing control flow branches to labels named test_pass /
 * test_fail can simply define those labels in terms of the macros:
 *
 *     test_pass: TEST_PASS
 *     test_fail: TEST_FAIL
 * ------------------------------------------------------------------------- */
.macro TEST_PASS
    li   t0, 0x20000000          /* MM_TESTSTATUS_ADDR */
    li   t1, 123456789           /* TEST_RESULT_PASS   */
    sw   t1, 0(t0)
1:  wfi
    j    1b
.endm

.macro TEST_FAIL
    li   t0, 0x20000000          /* MM_TESTSTATUS_ADDR */
    li   t1, 1                   /* TEST_RESULT_FAIL   */
    sw   t1, 0(t0)
1:  wfi
    j    1b
.endm

#endif /* __ASSEMBLER__ */

#endif /* CV32E20_DV_H */
