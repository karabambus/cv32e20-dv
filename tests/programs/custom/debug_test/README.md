# debug_test

A directed test that exercises the **CV32E20** run-control debug and trigger
features against the RISC-V Debug Specification (v0.13.2) and the CV32E20 User
Manual. It runs bare-metal on the core testbench (Verilator + `tb/core`).

The test drives debug entry three ways — external halt request (`debug_req_i`),
`ebreak`, and single-step — and checks the resulting `dcsr`/`dpc`/`mepc`/`mcause`/
`mtval` state, CSR access rules, exception behaviour in debug mode, and the
trigger CSRs.

> **CV32E20 is M-mode only** (requirement PVL-20). Several sub-tests were
> originally written for the U-mode-capable cv32e40p and have been ported to the
> M-only CV32E20 (see [CV32E20-specific notes](#cv32e20-specific-notes)).

## Source files

| File | Role |
|------|------|
| `debug_test.c` | Test driver / `main`. Sequences the tests, drives `debug_req_i`, checks status globals. |
| `debugger.S` | The "debugger" — code executed in Debug Mode (entered at `dm_halt_addr`). Dispatches on `glb_hart_status` / `glb_step_info`. |
| `debugger_exception.S` | Debug-mode exception handler (entered at `dm_exception_addr`), e.g. illegal CSR / `ecall` while in Debug Mode. |
| `handlers.S` | M-mode trap vector + handlers: illegal-instruction, `ebreak`, `ecall`, IRQ (`__no_irq_handler`). |
| `single_step.S` | The single-step exercise driven by Test 18 (`_single_step`), with sub-tests selected by `glb_step_info`. |
| `trigger_code.S` | Code region used as a trigger-match target. |
| `test.yaml` | Test definition. |

### Key status globals (driver ↔ debugger handshake)
- `glb_hart_status` — selects which debugger routine runs in Debug Mode.
- `glb_debug_status` — set by the debugger; the driver polls it to confirm completion.
- `glb_step_info` — selects the single-step sub-test (Test 18).
- `glb_expect_debug_entry` / `glb_expect_debug_exception` / `glb_expect_illegal_insn` /
  `glb_expect_ebreak_handler` / `glb_expect_irq_entry` — "expected event" flags; a
  handler entered without its flag set fails the test.
- `glb_illegal_insn_status`, `glb_ebreak_status`, `glb_step_count` — running counters.

## Running

```
make -C sim/core test TEST=debug_test \
  CFLAGS="-Os -g -static -mabi=ilp32 -march=rv32imc_zicsr_zifencei -Wno-error=implicit-function-declaration" \
  VERI_RUN_FLAGS="+maxcycles=2000000"
```

Pass is reported by the testbench as `ALL TESTS PASSED`; the program asserts the
pass signature (`*0x20000000 = 123456789`) on reaching the end of `main`. Any
failed check writes a non-zero value to `0x20000000` (`TEST_FAILED`).
Log: `sim/core/simulation_results/debug_test/0/test_program/debug_test.log`.

## Test summary

Tests are numbered historically; some numbers are unimplemented placeholders and
the **execution order is not strictly numeric** (see below).

| Test | Purpose |
|------|---------|
| **1** | Read initial `mstatus` / `mie` values. |
| **2** | Debug & Trigger CSR access rules. |
| 2.1 | Read/write of Debug CSRs (`dcsr`,`dpc`,`dscratch0/1`) **outside** Debug Mode → illegal-instruction exception. |
| 2.2 | Writes to Trigger CSRs (`tselect`,`tdata1/2/3`) outside Debug Mode are ignored; `tinfo`/`mcontext`/`scontext` handled (see notes). |
| 2.3 | Reads of Trigger CSRs return defaults; `tinfo` (`0x7a4`) and non-existent CSR (`0xea8`) reads → illegal. |
| **3** | `EBREAK` with `dcsr.ebreakm`=0. |
| 3.1 | `c.ebreak` runs the M-mode ebreak **handler** (not the debugger). |
| 3.2 | uncompressed `ebreak` — same. |
| **4** | **Debugger Simple:** assert `debug_req_i` (halt request) → enter Debug Mode; debugger checks `dcsr` cause = halt-req. |
| **5** | **Debugger Ebreak:** debugger executes `ebreak` 3× (incl. compressed), re-entering Debug Mode each time. |
| **6** | **Debugger CSR:** read Machine + Debug + Trigger CSRs in Debug Mode; check `dcsr` and `tdata1` defaults. |
| 7–9 | *Not implemented (placeholders).* |
| **10** | **Debugger Ebreak Entry:** `ebreak` with `dcsr.ebreakm`=1 enters the **debugger**; check entry cause. |
| **11** | Illegal-CSR access in Debug Mode → debug-mode exception (debugger-exception handler); CSRs unmodified. |
| **12** | `ecall` in Debug Mode → debug-mode exception. |
| **13** | `mret` in Debug Mode → debug-mode exception. *(currently `ecall` stands in for `mret` in `debugger.S`.)* |
| **14** | Exception path entering debug: illegal insn → handler executes `c.ebreak` → Debug Mode. |
| 15 | *Not implemented.* |
| **16** | `dret` executed in M-mode (outside Debug) → illegal-instruction exception. |
| **17** | `WFI` before `debug_req_i`, and `WFI` in Debug Mode (must behave as `NOP`; test hangs if not). |
| *(irq check)* | Assert & service a timer IRQ — prerequisite plumbing for later tests. |
| **21** | `dcsr.stopcount`=1 has no effect (`mcycle`/`minstret` still advance). *(runs before 18)* |
| **18** | **Single stepping** — see [sub-tests](#test-18-single-step-sub-tests). |
| **19** | IRQ asserted while in Debug Mode is **not** taken until debug exit (timeout-checked). |
| **20** | `debug_req_i` and IRQ asserted on the same cycle — debug entry vs. interrupt timing. |
| **22** | `fence` / `fence.i` execute in Debug Mode. |
| **23** | Trigger with match **disabled** does not fire in Debug Mode. |
| **24** | Trigger register R/W in Debug Mode (`tdata3`, `tselect`) to close coverage holes. |

**Execution order:** 1, 2.x, 3, 4, 5, 6, 10, 11, 12, 13, 14, 16, 17, *(irq check)*,
21, 18, 19, 20, 22, 23, 24.

### Test 18 single-step sub-tests

Selected by `glb_step_info` in `single_step.S` / `debugger.S`:

| `step_info` | Sub-test |
|:-----------:|----------|
| 1 | Enable single-step (`dcsr.step`), advance `dpc` past the entry `ebreak`. |
| 0 | Normal single-step: `dpc` advances by 2/4 each step. |
| 3 | Step over an **illegal** instruction (`csrr dcsr`, then `dret`-in-M): step enters Debug at the trap vector; `mtval` holds the faulting instruction. |
| 4 | **Trigger match** during single-step (execute match on `_step_trig_point`). |
| 5, 6 | *Stepping with interrupts (`stepie`=1/0).* **SKIPPED** — `dcsr.stepie` is **not implemented** on CV32E20 (interrupts always masked during step). Code preserved behind `j _skip_stepie_tests` for a separate feature review. |
| 7, 8 | `c.ebreak` / `ebreak` with `dcsr.ebreakm`=1 during step — ebreak vs. step cause priority (ebreak wins). |
| 9, 10 | `ebreak` / `c.ebreak` with `dcsr.ebreakm`=0 during step — breakpoint exception to the M-mode handler; `ebreakm` restored afterwards. |
| 2 | Disable single-step. |

## CV32E20-specific notes

These reflect the M-only / reduced feature set of CV32E20; the RTL and the
CV32E20 manual are authoritative, and the test was adapted to match:

- **M-mode only (PVL-20).** No U-mode. The trigger `mcontrol.u` bit reads 0, so
  `tdata1` reads `0x28001040` (Tests 2.3 and 6). `dcsr.prv` WARL-clamps to M.
- **`mtval` holds the faulting instruction** on illegal-instruction exceptions
  (and is sticky), unlike cv32e40p's `mtval`==0. Test 18 does not assert `mtval`==0.
- **`dcsr.stepie` not implemented** (bit 11 reads 0): interrupts are always
  masked while single-stepping. Test 18 `step_info` 5/6 are skipped.
- **`tinfo` (`0x7a4`) not implemented:** any access raises an illegal instruction
  (verified by Test 2.x). Test 24 does not access `tinfo`.
- **Single-step over a trapping instruction** (illegal / `ebreak`-without-`ebreakm`)
  enters Debug Mode at the trap handler's first PC with `dcsr.cause`=step, then the
  handler is itself single-stepped.
