/*
**
** Copyright 2020,2022 OpenHW Group
**
** Licensed under the Solderpad Hardware Licence, Version 2.0 (the "License");
** you may not use this file except in compliance with the License.
** You may obtain a copy of the License at
**
**     https://solderpad.org/licenses/
**
** Unless required by applicable law or agreed to in writing, software
** distributed under the License is distributed on an "AS IS" BASIS,
** WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
** See the License for the specific language governing permissions and
** limitations under the License.
**
** SPDX-License-Identifier: Apache-2.0 WITH SHL-2.0
**
*******************************************************************************
**
** Performance counter directed test (CV32E20).
**
** IMPORTANT: CV32E20 (CVE2) hardwires each event counter to a fixed event
** (see CV32E20 Performance Counters reference and cve2_cs_registers.sv):
**
**    mhpmcounter3 (0xB03) -> event 3  NumCyclesLSU
**    mhpmcounter4 (0xB04) -> event 4  NumCyclesIF
**    mhpmcounter5 (0xB05) -> event 5  NumLoads
**    mhpmcounter6 (0xB06) -> event 6  NumStores
**    mhpmcounter7 (0xB07) -> event 7  NumJumps
**    mhpmcounter8 (0xB08) -> event 8  NumBranches
**    mhpmcounter9 (0xB09) -> event 9  NumBranchesTaken
**    mhpmcounter10(0xB0A) -> event 10 NumInstrRetC (compressed)
**
** The mhpmevent CSRs are NOT programmable event selectors on CV32E20, so this
** test reads the appropriate hardwired counter directly rather than trying to
** route an event onto mhpmcounter3 (which is what the cv32e40p-style original
** did).  Counting is gated with mcountinhibit (0x320): all counters are
** inhibited outside the measured window and enabled only across the asm block
** of interest, so each event counter reflects exactly the events in that block.
**
*******************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>

#define TEST_PASSED  *(volatile int *)0x20000000 = 123456789
#define TEST_FAILED  *(volatile int *)0x20000000 = 1

static int chck(const char *name, unsigned int is, unsigned int should)
{
  int err = (is == should) ? 0 : 1;
  printf("%-24s = %u (expected %u) %s\n", name, is, should, err ? "FAIL" : "pass");
  return err;
}

int main(int argc, char *argv[])
{
  int err_cnt = 0;
  unsigned int count;

  printf("\nCV32E20 hardwired performance-counter test\n\n");

  //////////////////////////////////////////////////////////////
  // Loads -> mhpmcounter5 (event 5, NumLoads). Three word loads.
  __asm__ volatile(
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"   // inhibit all counters
    "csrwi 0xB05, 0        \n"   // mhpmcounter5 = 0
    "csrw  0x320, x0       \n"   // enable all counters
    "lw    x0, 0(sp)       \n"
    "lw    x0, 0(sp)       \n"
    "lw    x0, 0(sp)       \n"
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"   // inhibit all counters
    "csrr  %0, 0xB05       \n"
    : "=r"(count) : : "t0");
  err_cnt += chck("Loads (mhpmcounter5)", count, 3);

  //////////////////////////////////////////////////////////////
  // Stores -> mhpmcounter6 (event 6, NumStores). Three word stores.
  __asm__ volatile(
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrwi 0xB06, 0        \n"
    "csrw  0x320, x0       \n"
    "sw    x0, 0(sp)       \n"
    "sw    x0, 0(sp)       \n"
    "sw    x0, 0(sp)       \n"
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrr  %0, 0xB06       \n"
    : "=r"(count) : : "t0");
  err_cnt += chck("Stores (mhpmcounter6)", count, 3);

  //////////////////////////////////////////////////////////////
  // Jumps -> mhpmcounter7 (event 7, NumJumps). Two unconditional jumps.
  __asm__ volatile(
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrwi 0xB07, 0        \n"
    "csrw  0x320, x0       \n"
    "j     1f              \n"
    "1:                    \n"
    "j     2f              \n"
    "2:                    \n"
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrr  %0, 0xB07       \n"
    : "=r"(count) : : "t0");
  err_cnt += chck("Jumps (mhpmcounter7)", count, 2);

  //////////////////////////////////////////////////////////////
  // Branches -> mhpmcounter8 (event 8, NumBranches, conditional, taken or not)
  // Taken branches -> mhpmcounter9 (event 9, NumBranchesTaken)
  // 3 conditional branches: beq(taken), bne(not taken), beq(taken).
  unsigned int taken;
  __asm__ volatile(
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrwi 0xB08, 0        \n"
    "csrwi 0xB09, 0        \n"
    "csrw  0x320, x0       \n"
    "beq   x0, x0, 1f      \n"   // taken
    "1:                    \n"
    "bne   x0, x0, 2f      \n"   // not taken
    "2:                    \n"
    "beq   x0, x0, 3f      \n"   // taken
    "3:                    \n"
    "li    t0, -1          \n"
    "csrw  0x320, t0       \n"
    "csrr  %0, 0xB08       \n"
    "csrr  %1, 0xB09       \n"
    : "=r"(count), "=r"(taken) : : "t0");
  err_cnt += chck("Branches (mhpmcounter8)", count, 3);
  err_cnt += chck("Taken branches (cnt9)", taken, 2);

  //////////////////////////////////////////////////////////////
  // Compressed instructions -> mhpmcounter10 (event 10, NumInstrRetC).
  // Three explicit compressed instructions.
  // NB: prepare the inhibit mask in t0 BEFORE enabling. "li t0,-1" assembles
  // to a compressed c.li (-1 fits in 6 bits); if it appeared inside the counting
  // window it would be counted as a 4th compressed instruction.
  __asm__ volatile(
    "li    t0, -1          \n"   // inhibit mask (outside the window)
    "csrw  0x320, t0       \n"   // inhibit all counters
    "csrwi 0xB0A, 0        \n"   // mhpmcounter10 = 0
    "csrw  0x320, x0       \n"   // enable all counters
    "c.addi x15, 1         \n"
    "c.addi x15, 1         \n"
    "c.addi x15, 1         \n"
    "csrw  0x320, t0       \n"   // inhibit all (csrw is 32-bit, not counted)
    "csrr  %0, 0xB0A       \n"
    : "=r"(count) : : "t0", "x15");
  err_cnt += chck("Compressed (mhpmcounter10)", count, 3);

  //////////////////////////////////////////////////////////////
  printf("\nDone\n");
  if (err_cnt) {
    printf("FAILURE. %d errors\n\n", err_cnt);
    TEST_FAILED;
  } else {
    printf("SUCCESS\n\n");
    TEST_PASSED;
  }
  return err_cnt;
}
