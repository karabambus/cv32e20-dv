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
** Performance counter CSR test (CV32E20).
**
** Validates the documented CV32E20 (CVE2) performance-counter CSRs.  Unlike
** cv32e40p, CV32E20:
**   - implements mhpmcounter3..12 (MHPMCounterNum = 10); 13..31 read 0,
**   - HARDWIRES each counter to its event (mhpmevent is not a programmable
**     selector): mhpmevent3..12 read (1<<n) and ignore writes; 13..31 read 0,
**   - resets mcountinhibit implemented bits to 0 (counters enabled), bit 1 is
**     reserved/0, and the unimplemented high bits read 1,
**   - mcycle/minstret are 64-bit RW counters, gated by mcountinhibit.
**
** See the CV32E20 Performance Counters reference and cve2_cs_registers.sv.
**
*******************************************************************************
*/

#include <stdio.h>
#include <stdlib.h>

#include "cv32e20_dv.h"

#define MHPM_NUM 10   /* CV32E20: mhpmcounter3..12 implemented */

static int errs = 0;

static void chk(const char *name, unsigned int exp, unsigned int got)
{
  if (exp != got) {
    printf("FAIL %-28s exp 0x%08x got 0x%08x\n", name, exp, got);
    errs++;
  } else {
    printf("pass %-28s = 0x%08x\n", name, got);
  }
}

/* read/write helpers for the 12 mhpmevent CSRs we care about (3..12 implemented,
 * plus representative unimplemented 13 and 31). */
#define RD_EVENT(addr, var)  __asm__ volatile("csrr %0, " #addr : "=r"(var))
#define WR_EVENT(addr, val)  __asm__ volatile("csrw " #addr ", %0" :: "r"(val))

int main(int argc, char *argv[])
{
  unsigned int v, a, b;
  unsigned int all1 = (unsigned int)-1;

  printf("\nCV32E20 performance-counter CSR test\n\n");

  //////////////////////////////////////////////////////////////
  // mhpmevent3..12 are hardwired to (1<<n) and ignore writes.
  RD_EVENT(0x323, v); chk("mhpmevent3",  1u<<3,  v);
  RD_EVENT(0x324, v); chk("mhpmevent4",  1u<<4,  v);
  RD_EVENT(0x325, v); chk("mhpmevent5",  1u<<5,  v);
  RD_EVENT(0x326, v); chk("mhpmevent6",  1u<<6,  v);
  RD_EVENT(0x327, v); chk("mhpmevent7",  1u<<7,  v);
  RD_EVENT(0x328, v); chk("mhpmevent8",  1u<<8,  v);
  RD_EVENT(0x329, v); chk("mhpmevent9",  1u<<9,  v);
  RD_EVENT(0x32A, v); chk("mhpmevent10", 1u<<10, v);
  RD_EVENT(0x32B, v); chk("mhpmevent11", 1u<<11, v);
  RD_EVENT(0x32C, v); chk("mhpmevent12", 1u<<12, v);

  // Writes to a hardwired event selector are ignored (still reads 1<<n).
  WR_EVENT(0x323, all1); RD_EVENT(0x323, v); chk("mhpmevent3 wr-ignored", 1u<<3, v);
  WR_EVENT(0x32C, all1); RD_EVENT(0x32C, v); chk("mhpmevent12 wr-ignored", 1u<<12, v);

  // Unimplemented event selectors read 0 (representative 13 and 31).
  RD_EVENT(0x32D, v); chk("mhpmevent13 (unimpl)", 0, v);
  RD_EVENT(0x33F, v); chk("mhpmevent31 (unimpl)", 0, v);
  WR_EVENT(0x33F, all1); RD_EVENT(0x33F, v); chk("mhpmevent31 wr-ignored", 0, v);

  //////////////////////////////////////////////////////////////
  // Unimplemented counters mhpmcounter13..31 read 0 (representative).
  __asm__ volatile("csrr %0, 0xB0D" : "=r"(v)); chk("mhpmcounter13 (unimpl)", 0, v);
  __asm__ volatile("csrr %0, 0xB1F" : "=r"(v)); chk("mhpmcounter31 (unimpl)", 0, v);
  __asm__ volatile("csrw 0xB1F, %0" :: "r"(all1));
  __asm__ volatile("csrr %0, 0xB1F" : "=r"(v)); chk("mhpmcounter31 wr-ignored", 0, v);

  //////////////////////////////////////////////////////////////
  // mcountinhibit: bit 1 is reserved and reads 0; implemented bits are R/W.
  __asm__ volatile("csrw 0x320, %0" :: "r"(all1));   // try to set every bit
  __asm__ volatile("csrr %0, 0x320" : "=r"(v));
  chk("mcountinhibit bit1=0", 0u, v & 0x2);
  // implemented inhibit bits (0,2,3..12) must be settable
  chk("mcountinhibit impl bits", 0x1FFDu, v & 0x1FFD);
  __asm__ volatile("csrw 0x320, x0");                // clear (enable all)
  __asm__ volatile("csrr %0, 0x320" : "=r"(v));
  chk("mcountinhibit impl cleared", 0u, v & 0x1FFD);

  //////////////////////////////////////////////////////////////
  // mcycle/minstret are R/W and gated by mcountinhibit.
  // (1) While inhibited they hold their written value.
  __asm__ volatile("csrw 0x320, %0" :: "r"(all1));   // inhibit all
  __asm__ volatile("csrwi 0xB00, 0");                // mcycle   = 0
  __asm__ volatile("csrwi 0xB02, 0");                // minstret = 0
  __asm__ volatile("csrr %0, 0xB00" : "=r"(a));
  __asm__ volatile("nop\n nop\n nop\n nop");
  __asm__ volatile("csrr %0, 0xB00" : "=r"(b));
  chk("mcycle frozen when inhibited", a, b);
  __asm__ volatile("csrr %0, 0xB02" : "=r"(v));
  chk("minstret frozen (==0)", 0u, v);

  // (2) When enabled they advance.
  __asm__ volatile("csrwi 0xB00, 0");                // mcycle   = 0
  __asm__ volatile("csrwi 0xB02, 0");                // minstret = 0
  __asm__ volatile("csrw  0x320, x0");               // enable all counters
  __asm__ volatile("nop\n nop\n nop\n nop\n nop");
  __asm__ volatile("csrw  0x320, %0" :: "r"(all1));  // inhibit all
  __asm__ volatile("csrr %0, 0xB00" : "=r"(a));      // mcycle
  __asm__ volatile("csrr %0, 0xB02" : "=r"(b));      // minstret
  printf("mcycle after window   = %u\n", a);
  printf("minstret after window = %u\n", b);
  if (a == 0) { printf("FAIL mcycle did not advance\n"); errs++; }
  if (b == 0) { printf("FAIL minstret did not advance\n"); errs++; }

  //////////////////////////////////////////////////////////////
  printf("\nDone\n");
  if (errs) {
    printf("FAILURE. %d errors\n\n", errs);
    TEST_FAILED;
  } else {
    printf("SUCCESS\n\n");
    TEST_PASSED;
  }
  return errs;
}
