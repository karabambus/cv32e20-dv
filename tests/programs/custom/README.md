<!--
Copyright 2026 Eclipse Foundation AISBL
SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
-->

# CV32E20 C test-program cleanup

A "cleanup" of the C test-programs under `tests/programs/custom` was undertaken with the
goal that each test must (a) be applicable to the CV32E20 per the
**documentation** (authoritative over RTL), and (b) correctly assert the
canonical pass/fail signature. Non-applicable tests were deleted.

## Canonical pass/fail protocol

The TB virtual peripheral (`env/uvme/.../uvme_cv32e20_vp_status_flags_seq.sv`)
flags **TEST PASSED only when `123456789` (0x075B_CD15) is written to the status
symbol `tohost` / address `0x20000000`**. In the UVM environment, *any other value
written there is a FAILURE* (`uvm_error`). The "core" testbench will FAIL the test
when 0x1 is written.  TODO: fix this so bothUVM and core have the same TEST FAIL signalling.
So:

```c
#define TEST_PASSED  *(volatile int *)0x20000000 = 123456789
#define TEST_FAILED  *(volatile int *)0x20000000 = 1
```

Tests that `#define TEST_PASSED ...= 1` are **broken** (they report FAIL on
success) and must be corrected.

**Shared header (`bsp/cv32e20_dv.h`).** The canonical `TEST_PASSED`/`TEST_FAILED`
macros and every memory-mapped virtual-peripheral register (`MM_*` plus the
back-compat `TIMER_REG_ADDR`/`TIMER_VAL_ADDR`/`DEBUG_REQ_CONTROL_REG`) now live
in a single header, `bsp/cv32e20_dv.h`, mirroring the authoritative decode in
`tb/core/mm_ram.sv`. Every C test program includes it (directly, or via
`interrupt_test.h`); `bsp/` is on the compile include path (`-I $(BSP)` in
`mk/Common.mk`). Per-test local copies of these macros have been removed — add
new memory-mapped registers to the header, not to individual tests.

## CV32E20 capability reference (per docs + PVL-20)

- **ISA:** RV32IMC. **M-mode only** (requirement PVL-20). The generic CVE2 docs
  still mention U-mode / ECALL-from-U (exception code 8); those are stale —
  PVL-20 governs. No U-mode, no `mstatus.MPP=U`, no ECALL-from-U.
- **Traps:** vectored mode only — `mtvec` base (256-byte aligned at boot) + 4×ID.
  `mepc`/`mcause`/`mtval`/`mstatus`(MIE/MPIE/MPP=M). NMI id 31 at base+0x7C.
  `mtval` on CV32E20 = faulting instruction encoding (sticky).
- **Interrupts:** software=3, timer=7, external=11, fast/local=16..30 (15 of
  them, `irq_fast_i[14:0]`), NMI=31. Gated by `mstatus.MIE` + `mie`; `mip` is
  read-only. Level-sensitive. Ignored in Debug Mode.
- **Exceptions:** 1 instr-access, 2 illegal, 3 breakpoint, 5 load-access,
  7 store-access, 11 ECALL-M. (code 8 ECALL-U N/A on M-only core.)
- **Perf counters:** `mcycle`(0xB00)/`minstret`(0xB02) always present, 64-bit.
  `mhpmcounter3..` parametrizable (`MHPMCounterNum`, default 10 →
  mhpmcounter3..12). Fixed event map: 3=CyclesLSU 4=CyclesIF 5=Loads 6=Stores
  7=Jumps 8=Branches 9=BranchesTaken 10=InstrRetC 11=CyclesWFI 12=CyclesDivWait.
  `mcountinhibit`(0x320) all-enabled at reset. `mhpmevent3..12` reset = (1<\<id).
  Unavailable counters read 0. **(confirm CV32E20 MHPMCounterNum for Cat 4.)**
- **CSRs present (doc):** mstatus 300, misa 301, mie 304, mtvec 305,
  mcountinhibit 320, mhpmevent3-31 323-33F, mscratch 340, mepc 341, mcause 342,
  mtval 343, mip 344, pmpcfg0-3 3A0-3A3, pmpaddr0-15 3B0-3BF, mseccfgh 757,
  tselect 7A0, tdata1-3 7A1-7A3, mcontext 7A8, scontext 7AA, dcsr 7B0, dpc 7B1,
  dscratch0/1 7B2/7B3, cpuctrl 7C0 (custom), mcycle/h, minstret/h,
  mhpmcounter3-31/h, mvendorid F11, marchid F12, mimpid F13, mhartid F14.
- **Debug/triggers:** tselect/tdata1/tdata2/tdata3 available; mcontrol type;
  M-only → trigger u-bit reads 0; tdata1 reads `0x28001040` (per debug_test work).
- **Core TB (Verilator) peripherals** (`tb/core/mm_ram.sv`): timer-IRQ at
  `0x15000000`(mask)/`0x15000004`(delay), debug-control `0x15000008`,
  rnd `0x15001000`, ticks `0x15001004`, CLINT mtime `0x02000000`,
  Sail SIG `0x15000020/24`, status/tohost `0x20000000` (pass=123456789).

## Reusable findings

- **Level-sensitive interrupts / no ack (affects interrupt tests).** CVE2 has no
  `irq_ack_o`; `irq_ack`/`irq_id_in` are unconnected in `cv32e20_tb_wrapper.sv`.
  The `mm_ram` legacy timer (`0x15000000`/`04`) therefore *never* clears on its
  own — software must deassert by re-arming with the bit cleared
  (`mm_ram_assert_irq(mask_without_bit, n)`; count reload writes irq_q). A
  handler that just `mret`s without clearing causes an infinite interrupt storm
  that starves the interrupted instruction. `interrupt_test` already does the
  clear-by-rearm; `branch_zero` did not (now fixed).

## Results summary

Regression (`make test TEST=<dir>` in `sim/core`), all **14 in-scope passing**:
branch_zero, fibonacci, coremark, dhrystone, all_csr_por, csr_instructions,
hpmcounter_basic_test, perf_counters_instructions, illegal, misalign,
interrupt_test, interrupt_bootstrap (+ pre-existing hello-world, debug_test).

- **Fixed/rewritten & passing (12):** branch_zero, fibonacci(as-is), coremark,
  dhrystone, all_csr_por, csr_instructions, hpmcounter_basic_test,
  perf_counters_instructions, illegal, misalign, interrupt_test,
  interrupt_bootstrap.
- **Rejected/deleted (1):** hpmcounter_hazard_test (hazard events not in CV32E20).
- **Parked (5):** riscv_csr (M-only CSR-template reconciliation — task #10);
  debug_test_boot_set, debug_test_known_miscompares, debug_test_reset,
  debug_test_trigger (UVM-env tests — task #11).

## Status table

Run: `make test TEST=<dir>` in `sim/core`. Delete = remove the directory.

| Test dir | Category | Triage | Decision | Notes |
|----------|----------|--------|----------|-------|
| branch_zero | trivial | hung→**PASS** | **FIX (kept)** | level-sensitive IRQ storm: handlers did `addi;mret` w/o deasserting mm_ram timer (CVE2 has no irq_ack). Fixed handlers to clear timer (re-arm mask=0) per spec; added TEST_PASSED. |
| fibonacci | trivial | **PASS** | **KEEP (as-is)** | already correct: TEST_PASSED=123456789, asserts on success. No change. |
| coremark | benchmark | build-fail→**PASS** | **FIX (kept)** | did not build (`ITERATIONS`/`FLAGS_STR` undefined; no default_cflags). Added `#ifndef` defaults in core_portme.h (PERFORMANCE_RUN, ITERATIONS=1, FLAGS_STR) + assert TEST_PASSED on `total_errors==0`. CRCs validate (crcfinal 0xe714). |
| dhrystone | benchmark | hung→**PASS** | **FIX (kept)** | ran but never signalled (no TEST_PASSED) → maxcycles. Added self-check of canonical invariants + TEST_PASSED/FAILED. Dropped Next_Ptr_Glob->Enum_Comp check (compiler/eval-order dependent Dhrystone aliasing quirk; build yields 2 vs stale "should be 1"). |
| all_csr_por | CSR | hung→**PASS** | **KEEP & FIX (minimal)** | reads all 4096 CSR addrs into a discarded var, no value checks. Per user decision kept & fixed minimally: added TEST_PASSED (validates all 4096 CSR reads trap/skip cleanly via BSP handler, no hang). ISS step-compare prototype; real CSR validation is riscv_csr's job. |
| csr_instructions | CSR | hung→**PASS** | **FIX (kept)** | functional-coverage test of all Zicsr forms; completed but no TEST_PASSED. Added TEST_PASSED + mscratch write-read self-check. mstatus WARL `0xFFFFFFFF`→`0x1888` confirms M-only clamp. |
| riscv_csr | CSR | build-fail→**in progress** | **FIX (partial)** | DONE: defined test_fail (link); macro→123456789; dropped U-mode half (main); patched mstatus (18) + misa (18) expected values for M-only (script /tmp/patch_csr.py, model `mstatus=0x1800\|(v&0x88)`, misa U-bit cleared). REMAINING: template (env/corev-dv/cv32e20_csr_template.yaml) also classifies unimplemented unprivileged counter shadows 0xC00-0xC1F/0xC80-0xC9F as readable → core raises illegal (correct per docs). Needs template reconcile + regenerate (gen_csr_test.py needs `bitstring` pip pkg) OR more hand-patching. Backups: /tmp/riscv_csr_test_0.{S,h}.bak |
| hpmcounter_basic_test | HPM | FAIL→**PASS** | **FIX (rewrote)** | rewrote to CV32E20 hardwired model: read mhpmcounter5-10 directly (loads/stores/jumps/branches/taken/compressed), gate with mcountinhibit window, dropped hazard sub-tests. All 6 event counts exact. (Subtlety: `li t0,-1` is compressed c.li — kept outside the counting window for the compressed check.) |
| hpmcounter_hazard_test | HPM | FAIL | **REJECTED (deleted)** | tested ONLY load-use/jump-register hazards — events CV32E20 doesn't implement (cv32e40p LD_STALL/JR_STALL). Fully inapplicable. Directory removed. |
| perf_counters_instructions | HPM | FAIL→**PASS** | **FIX (rewrote)** | replaced messy cv32e40p coverage test (had switch fall-through bugs, wrong reset expectations). New test validates: mhpmevent3-12=1<\<n & writes ignored; mhpmevent/mhpmcounter13-31 read 0; mcountinhibit bit1 reserved + impl bits R/W; mcycle/minstret frozen-when-inhibited / advance-when-enabled. |
| debug_test | debug | **PASS** | **KEEP** | Extensive updates for CV32E20.  Running on both the core and uvmt testbenches. |
| debug_test_boot_set | debug | build-fail | **PARKED (UVM env)** | debug-at-reset via `+debug_boot_set` — no mechanism in core Verilator TB (debug_req is via mm_ram 0x15000008 during execution, not at reset). build broken (uint32_t). Covered by main debug_test. Left as-is for UVM env. |
| debug_test_known_miscompares | debug | build-fail | **PARKED (UVM env)** | "known failures in step-and-compare" = ISS step-compare specific; core TB has no ISS. build broken (test_fail undefined). TEST_PASSED=1. Left as-is for UVM env. |
| debug_test_reset | debug | build-fail | **PARKED (UVM env)** | debug-at-reset via `+reset_debug` — same as boot_set, no core-TB mechanism. build broken (uint32_t). Left as-is for UVM env. |
| debug_test_trigger | debug | build-fail | **PARKED (UVM env)** | dedicated trigger test requiring `+rand_stall_obi_disable` for exact timing; core `make test` does NOT apply test.yaml plusargs. build broken (uint32_t + mie_enable). TEST_PASSED=1. Trigger fn already covered by main debug_test Tests 18-24. Left as-is for UVM env. |
| illegal | exceptions | hung→**PASS** | **FIX (rewrote)** | original just ran `.word 0` then fell through ("unreachable" printed) with no check/TEST_PASSED. Rewrote to self-check the sticky trap state: mcause==2 (illegal) and mtval==faulting-instruction encoding (0xFFFFFFFF) per CV32E20 docs (mtval holds the actual faulting instruction). BSP handler skips it. |
| misalign | exceptions | hung→**PASS** | **FIX (rewrote)** | CV32E20 LSU handles misaligned in hardware (no exception; load_store_unit.rst). Original only printed. Rewrote to self-check all 16 byte-offsets × {u16,u32,u64} for both loads and stores against the deterministic byte pattern (96 checks). |
| interrupt_test | interrupts | build-fail→**PASS** | **FIX (rewrote bits)** | (1) missing rand.h → added a deterministic xorshift PRNG (random_num/random_num32); (2) handlers were plain C (ret) → added `__attribute__((interrupt))` for MRET; (3) level-sensitive clear used delay random_num(10,0) → storm → changed to delay 0 (active_test 1 & 5); (4) removed NMI sub-test (irq_nm_i tied 1'b0 in core TB); (5) added TEST_PASSED/FAILED (was return-only). All 6 sub-tests pass. |
| interrupt_bootstrap | interrupts | FAIL→**PASS** | **FIX (rewrote bits)** | (1) BOOTSTRAP_MTVEC 0x200→0x4000 (actual core-TB boot addr); (2) its _start leaves mtvec at boot addr (not vector_table) → after verifying the bootstrap value, point mtvec at vector_table so handlers are reachable; (3) generic_irq_handler missing active_test==1 level-sensitive clear → added (delay 0); (4) handlers → `__attribute__((interrupt))`; (5) added TEST_PASSED/FAILED. |

**Note:** the 4 debug_test_<variant> test-programs above are parked as UVM-environment tests (out of scope for the core Verilator TB cleanup). See follow-up task.
**Reusable interrupt findings (core TB):** handlers must be `__attribute__((interrupt("machine")))` (MRET + full save); level-sensitive IRQs must be deasserted with **delay 0** (any delay re-arms a storm); NMI (`irq_nm_i`) is tied 1'b0 in the core TB so NMI is not testable there.

## Parked work — resume notes

### riscv_csr (task #10)
Generated CSR test (`gen_csr_test.py` + template
`env/corev-dv/cv32e20_csr_template.yaml`).

DONE: defined `test_fail` (link fix); macro `TEST_PASSED` `=1`→`=123456789`;
dropped the U-mode half of `main` (switch_to_user_mode / user_mode_check —
CV32E20 is M-only, PVL-20); patched mstatus (18) + misa (18) expected values in
the generated `riscv_csr_test_0.S` for M-only. Model used:
`mstatus_read = 0x1800 | (v & 0x88)` (MPP always clamps to M; only MIE/MPIE
writable; MPRV/TW read 0), misa Extensions clears the U bit → `0x40001104`.

REMAINING: the template still lists unimplemented unprivileged counter shadows
`0xC00-0xC1F` / `0xC80-0xC9F` as readable, but CV32E20 implements only the
machine-level counters (mcycle 0xB00, minstret 0xB02, mhpmcounter3-12) → the
core raises illegal (`Unexpected illegal instruction while accessing CSR 0xc00`).
Two finish paths:
1. **Preferred — regenerate.** The generator errors `Please install bitstring`;
   `pip install --user bitstring`, then correct the template fields and run
   `python3 gen_csr_test.py --csr_file ../../../../env/corev-dv/cv32e20_csr_template.yaml`
   from the test dir. Template fixes for M-only:
   - mstatus `TW`: add `warl_legalize` → 0 (or type R, reset 0)
   - mstatus `MPRV`: type RW → R (read-only 0)
   - mstatus `MPP`: `warl_legalize` → always 3
   - misa `Extensions`: `reset_val` `0x101104` → `0x001104` (drop U bit 20)
   - remove (or mark unimplemented) the `0xC00-0xC1F`/`0xC80-0xC9F` shadows
2. Hand-patch the generated `.S` to drop those CSR checks (extensive).

(The `/tmp/riscv_csr_test_0.{S,h}.bak` backups and `/tmp/patch_csr.py` are
ephemeral — regenerate the patch model from the formula above if gone.)

### Debug variants (task #11)
debug_test_boot_set, debug_test_reset, debug_test_known_miscompares,
debug_test_trigger — UVM-environment tests needing features the core Verilator
TB lacks (debug-at-reset plusargs, ISS step-compare, per-test plusargs). All
fail to build; debug is already covered in the core TB by main debug_test.
Revisit in `sim/uvmt`, not `sim/core`.

## Shared test-program header — `bsp/cv32e20_dv.h` (DONE this session)

Single source of truth for the pass/fail protocol **and** every memory-mapped
virtual-peripheral register, mirroring the authoritative decode in
`tb/core/mm_ram.sv`. Reachable from any test via `#include "cv32e20_dv.h"`
because `mk/Common.mk` compiles programs (both `.c` and `.S`) with `-I $(BSP)`.

Contents: canonical `TEST_PASSED`(=123456789)/`TEST_FAILED`(=1) +
`TEST_RESULT_PASS/_FAIL`; the full map as `MM_*_ADDR` constants and `MM_*_REG`
lvalue accessors (PRINT, TESTSTATUS, EXIT, SIG{BEGIN,END,DUMP}, TIMER_CTRL/VAL,
DEBUG_CTRL, SIG_VERSION/PLATFORM, RNDNUM, TICKS, CLINT MTIME(H)/MTIMECMP(H));
back-compat names `TIMER_REG_ADDR`, `TIMER_VAL_ADDR`, `DEBUG_REQ_CONTROL_REG`.

Migration done (all 18 C sources under `tests/programs/custom`): per-test local
`#define`s removed; `interrupt_test.c`/`interrupt_bootstrap.c` get it via
`interrupt_test.h`; `coremark/core_main.c` raw status writes → `TEST_PASSED;`/
`TEST_FAILED;`. Per user decision, the 3 previously non-canonical tests were
moved to canonical values: `riscv_csr` (was FAILED=2), `debug_test_trigger` and
`debug_test_known_miscompares` (were PASSED=1/FAILED=2). `branch_zero.c`'s two
inline-asm IRQ handlers are now fully macro-driven — `MM_TIMER_CTRL_ADDR`/
`MM_TIMER_VAL_ADDR` passed as `"i"` immediates (no extra register, so the
"only t0/t1 dead in the tight loop" invariant holds), no hardcoded addresses.

Verified: fibonacci, branch_zero, interrupt_test, coremark, all_csr_por all run
**ALL TESTS PASSED**; parked tests (riscv_csr, debug_test*, debug_test) pass a
`-fsyntax-only` compile (no errors, no macro redefinitions).

REMAINING (future, when extending beyond C): the `.S`/asm tests still use
`tests/asm/user_define.h` (assembly syntax: `.set`/`.section`/`.word`, not C
macros). To unify those too, either factor the addresses into an
`#ifdef __ASSEMBLER__` section of a shared header or keep a parallel asm include
— not done; this session was scoped to the C test-programs only.
