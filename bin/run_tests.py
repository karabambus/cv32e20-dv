#!/usr/bin/env python3
# Copyright (c) 2026 Eclipse Foundation
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""
Run the cleaned-up directed test-programs (C and assembly) on a CV32E20
testbench and print a pass/fail summary.  Either testbench can be selected
with --tb:

    core   the Verilator core testbench in sim/core            (default)
    uvmt   the UVM testbench in sim/uvmt, run with SIMULATOR=vsim

For each selected test the script invokes `make test TEST=<name> ...` in the
chosen testbench's sim directory, then parses the per-test log it leaves behind
for the verdict banner:

    core : sim/core/simulation_results/<name>/<run>/test_program/<name>.log
           "ALL TESTS PASSED"   -> PASS   (tb/core/tb_top.sv)
           "TEST(S) FAILED!"    -> FAIL

    uvmt : sim/uvmt/vsim_results/<cfg>/<name>/<run>/vsim-<name>.log
           "SIMULATION PASSED"  -> PASS   (tb/uvmt/uvmt_cv32e20_tb.sv;
                                           includes "PASSED with WARNINGS")
           "SIMULATION FAILED"  -> FAIL

Anything else (no banner, build error, timeout) is reported as ERROR.
The process exit code is 0 only if every test that was run reported PASS.

corev-dv tests (see COREV_DV_TESTS below) additionally need `make corev-dv`
run once beforehand to clone and compile the corev-dv/riscv-dv packages
(uvmt only). Whenever the selected set includes any corev-dv test, this
script runs `make corev-dv` itself before running any tests, and aborts if
that setup step fails.

Usage
-----
The script needs no arguments and can be run from anywhere (paths are resolved
relative to its own location in <repo>/bin).  A working RISC-V toolchain plus
the relevant simulator (Verilator for core, Questa vsim for uvmt) must be on
PATH, as for a normal `make test`.

    # Run the full self-checking set (C + assembly) on the core TB:
    bin/run_tests.py
    python3 bin/run_tests.py                    # equivalent

    # Run the same set on the UVM testbench (vsim):
    bin/run_tests.py --tb uvmt
    # (equivalent to `make test TEST=<name> SIMULATOR=vsim` per test; the
    #  script sets SIMULATOR=vsim for you, so no need to export it yourself.)

    # Run only specific tests (by directory name under tests/programs/custom):
    bin/run_tests.py fibonacci misalign
    bin/run_tests.py --tb uvmt hello-world

    # Also run the parked tests (step-compare / debug; meaningful under --tb uvmt):
    bin/run_tests.py --include-parked
    bin/run_tests.py --tb uvmt --include-parked

    # Also run the passing corev-dv generated regression tests (uvmt only):
    bin/run_tests.py --tb uvmt --include-corev-dv

    # Run ONLY the corev-dv generated regression tests (uvmt only):
    bin/run_tests.py --tb uvmt --corev-dv-only

    # Skip simulation; just re-summarize logs from a previous run:
    bin/run_tests.py --parse-only
    bin/run_tests.py --tb uvmt --parse-only

    # Other options:
    #   --cfg NAME      uvmt config subdirectory                (default: default)
    #   --run-index N   RUN_INDEX subdirectory                  (default: 0)
    #   --timeout SECS  per-test timeout                        (default: 600)
    #   --quiet         suppress per-test simulation banners
    #   -h / --help     full option help

Exit status is 0 only when every selected test PASSED, so the script is
suitable for use as a CI gate.
"""

import argparse
import os
import subprocess
import sys
from pathlib import Path

# Repository layout: this script lives in <repo>/bin.
SCRIPT_DIR = Path(__file__).resolve().parent
REPO = SCRIPT_DIR.parent

# Per-testbench configuration.  Each entry knows where to run make, which extra
# make arguments to pass, the verdict banners its testbench prints, and how to
# locate a test's log file.
TESTBENCHES = {
    "core": {
        "sim_dir": REPO / "sim" / "core",
        "make_args": [],
        "pass_banner": "ALL TESTS PASSED",
        "fail_banner": "TEST(S) FAILED!",
        # Progress lines worth echoing while a test runs.
        "markers": ("tb_top]", "SUCCESS", "FAIL"),
    },
    "uvmt": {
        "sim_dir": REPO / "sim" / "uvmt",
        "make_args": ["SIMULATOR=vsim"],
        "pass_banner": "SIMULATION PASSED",
        "fail_banner": "SIMULATION FAILED",
        "markers": ("SIMULATION PASSED", "SIMULATION FAILED",
                    "UVM_ERROR :", "UVM_FATAL :", "TEST PASSED", "TEST FAILED"),
    },
}

# Self-checking C test-programs cleaned up on this branch.  These verify their
# own results and signal the canonical pass/fail, so they pass on the core TB.
C_TESTS = [
    "hello-world",
    "fibonacci",
    "branch_zero",
    "coremark",
    "dhrystone",
    "all_csr_por",
    "csr_instructions",
    "hpmcounter_basic_test",
    "illegal",
    "misalign",
    "interrupt_test",
    "interrupt_bootstrap",
    "debug_test",
]

# Self-checking assembly test-programs cleaned up on this branch.  Each was
# fixed to build and to signal end-of-test through the canonical protocol
# (TEST_PASS/TEST_FAIL in bsp/cv32e20_dv.h, writing 123456789 / 1); each passes
# on the core TB.
ASM_TESTS = [
    "load_store_rs1_zero",
    "illegal_instr_test",
    "generic_exception_test",
    "csr_instr_asm",
]

# Default (core TB) selection: every self-checking test, C and assembly.
TESTS = C_TESTS + ASM_TESTS

# Parked tests: these build and signal correctly, but their *meaningful*
# verification is not a self-check on the core TB.  Included only with
# --include-parked, and intended to be run with --tb uvmt:
#   * riscv_arithmetic_basic_test_0/1 and csr_instr_asm exercise long fixed
#     instruction streams whose correctness is checked by the RVFI step-compare
#     against the Spike ISS (sim/uvmt); on the core TB they pass *vacuously*.
#   * riscv_csr additionally needs the M-only counter-CSR reconciliation (see
#     README parked-work notes).
#   * the debug_test variants need the uvmt debug-request stimulus; on the core
#     TB they report FAIL because debug mode is never entered.
PARKED = [
    "riscv_arithmetic_basic_test_0",
    "riscv_arithmetic_basic_test_1",
    "csr_instr_asm",
    "riscv_csr",
    "perf_counters_instructions",
    "debug_test_boot_set",
    "debug_test_reset",
    "debug_test_known_miscompares",
    "debug_test_trigger",
]

# corev-dv (OpenHW's class extensions of Google's riscv-dv) generated regression
# templates under tests/programs/corev-dv/.  Unlike C_TESTS/ASM_TESTS/PARKED,
# each of these needs its randomized test program generated before it can be
# built and run, i.e. `make gen_corev-dv test TEST=<name>` rather than plain
# `make test TEST=<name>` -- run_test() adds the extra target automatically for
# names in this list. Meaningful only under --tb uvmt (corev-dv generation is
# wired up for the uvmt testbench only); included only with --include-corev-dv
# or --corev-dv-only.
#
# Before any test in this list can build, `make corev-dv` (clone + compile the
# corev-dv/riscv-dv packages) must have been run once in sim/uvmt; main() does
# this automatically whenever the selected set includes a corev-dv test.
#
# This is the set confirmed passing (0 UVM_ERROR/UVM_FATAL, clean end-of-test)
# with SIMULATOR=vsim as of 2026-07-14, after: the mk/uvmt/uvmt.mk corev-dv
# path-wiring fix and OPT_RUN_INDEX_SUFFIX fix; the exception_req_withdrawn SVA
# fix in core-v-cores/cv32e20/rtl/cve2_controller.sv (verification-only
# bookkeeping, no functional RTL change); the RVFI-gated interrupt-noise
# throttle in env/uvme/vseq/uvme_cv32e20_interrupt_noise_vseq.sv; and reverting
# the id_in_ready driver-side wait in uvma_interrupt_drv.sv/uvma_debug_drv.sv
# (it could deadlock a core parked in WFI).  See
# E20DV/tmp/exception_req_pending_finding.md and the cv32e20-corev-dv-restore
# project memory for the full investigation.
#
# Not yet included:
#   * corev_rand_interrupt_exception -- completes cleanly (no livelock) but
#     still hits one residual CVE2SetExceptionPCOnSpecialReqIfExpected
#     assertion during a rapid back-to-back exception-retrigger burst; root
#     cause not yet confirmed.
#   * corev_rand_debug, corev_rand_debug_ebreak, corev_rand_debug_single_step,
#     corev_rand_illegal_instr_test, corev_rand_instr_long_stall -- not yet run
#     against the current fixes.
COREV_DV_TESTS = [
    "corev_rand_arithmetic_base_test",
    "corev_rand_instr_test",
    "corev_rand_interrupt",
    "corev_rand_interrupt_debug",
    "corev_rand_interrupt_nested",
    "corev_rand_interrupt_wfi",
    "corev_rand_interrupt_wfi_mem_stress",
    "corev_rand_jump_stress_test",
]


def log_path(tb, test, run_index, cfg):
    """Location of the per-test simulation log for the given testbench."""
    sim_dir = TESTBENCHES[tb]["sim_dir"]
    if tb == "core":
        return (sim_dir / "simulation_results" / test / str(run_index)
                / "test_program" / f"{test}.log")
    # uvmt / vsim
    return (sim_dir / "vsim_results" / cfg / test / str(run_index)
            / f"vsim-{test}.log")


def classify(text, tbcfg):
    """Return 'PASS', 'FAIL', or 'ERROR' for a chunk of log/console text."""
    if tbcfg["pass_banner"] in text:
        return "PASS"
    if tbcfg["fail_banner"] in text:
        return "FAIL"
    return "ERROR"


def make_env(tbcfg):
    """Environment for a make invocation: os.environ plus tbcfg's make_args
    (e.g. SIMULATOR=vsim) mirrored in, so any sub-make/shell that reads the
    variable directly behaves consistently with what's on the command line."""
    env = os.environ.copy()
    for arg in tbcfg["make_args"]:
        key, _, val = arg.partition("=")
        if val:
            env[key] = val
    return env


def setup_corev_dv(tb, timeout):
    """Run `make corev-dv` once: clones and compiles the corev-dv/riscv-dv
    packages.  Required before any COREV_DV_TESTS entry can build; aborts the
    script on failure since no corev-dv test can proceed without it."""
    tbcfg = TESTBENCHES[tb]
    cmd = ["make", "corev-dv"] + tbcfg["make_args"]
    env = make_env(tbcfg)

    print(f"[Setup/{tb}] make corev-dv ...")
    try:
        proc = subprocess.run(cmd, cwd=tbcfg["sim_dir"], timeout=timeout, env=env)
    except subprocess.TimeoutExpired:
        sys.exit(f"error: 'make corev-dv' timed out after {timeout}s")
    except OSError as exc:
        sys.exit(f"error: could not launch 'make corev-dv': {exc}")

    if proc.returncode != 0:
        sys.exit(f"error: 'make corev-dv' failed (exit {proc.returncode}); "
                  "no corev-dv test can build without it")


def run_test(tb, test, run_index, cfg, timeout, quiet):
    """Build+run one test on the chosen testbench; return (outcome, detail)."""
    tbcfg = TESTBENCHES[tb]
    targets = ["gen_corev-dv", "test"] if test in COREV_DV_TESTS else ["test"]
    cmd = (["make"] + targets + [f"TEST={test}", f"RUN_INDEX={run_index}"]
           + tbcfg["make_args"])
    env = make_env(tbcfg)

    try:
        proc = subprocess.run(
            cmd,
            cwd=tbcfg["sim_dir"],
            capture_output=True,
            text=True,
            timeout=timeout,
            env=env,
        )
    except subprocess.TimeoutExpired:
        return "ERROR", f"timed out after {timeout}s"
    except OSError as exc:
        return "ERROR", f"could not launch make: {exc}"

    if not quiet:
        for line in proc.stdout.splitlines():
            if any(m in line for m in tbcfg["markers"]):
                print("    " + line)

    # The console output is authoritative for the run we just launched; fall
    # back to the on-disk log if the banner did not reach stdout.
    console = proc.stdout + proc.stderr
    outcome = classify(console, tbcfg)
    if outcome == "ERROR":
        path = log_path(tb, test, run_index, cfg)
        if path.is_file():
            outcome = classify(path.read_text(errors="replace"), tbcfg)

    if outcome != "ERROR":
        return outcome, ""
    if proc.returncode != 0:
        return "ERROR", f"make exited {proc.returncode}"
    return "ERROR", "no verdict banner"


def parse_only(tb, test, run_index, cfg):
    """Read an existing log file without re-running the simulation."""
    path = log_path(tb, test, run_index, cfg)
    if not path.is_file():
        return "ERROR", "no log file"
    return classify(path.read_text(errors="replace"), TESTBENCHES[tb]), ""


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("tests", nargs="*",
                    help="specific test names to run (default: the full cleaned-up set)")
    ap.add_argument("--tb", choices=sorted(TESTBENCHES), default="core",
                    help="testbench to run on: 'core' (Verilator) or 'uvmt' (vsim) "
                         "(default: core)")
    ap.add_argument("--include-parked", action="store_true",
                    help="also run the parked tests (step-compare arithmetic/CSR and "
                         "debug variants; meaningful under --tb uvmt)")
    ap.add_argument("--include-corev-dv", action="store_true",
                    help="also run the passing corev-dv generated regression tests "
                         "(uvmt only; each needs an extra gen_corev-dv step)")
    ap.add_argument("--corev-dv-only", action="store_true",
                    help="run ONLY the corev-dv generated regression tests (uvmt "
                         "only); cannot be combined with test names, "
                         "--include-parked, or --include-corev-dv")
    ap.add_argument("--parse-only", action="store_true",
                    help="do not run; just parse existing logs in the results directory")
    ap.add_argument("--cfg", default="default",
                    help="uvmt config subdirectory under vsim_results (default: default)")
    ap.add_argument("--run-index", type=int, default=0,
                    help="RUN_INDEX subdirectory to use (default: 0)")
    ap.add_argument("--timeout", type=int, default=600,
                    help="per-test timeout in seconds (default: 600)")
    ap.add_argument("--quiet", action="store_true",
                    help="suppress per-test simulation banner output")
    args = ap.parse_args()

    if args.corev_dv_only:
        if args.tests or args.include_parked or args.include_corev_dv:
            ap.error("--corev-dv-only cannot be combined with test names, "
                      "--include-parked, or --include-corev-dv")
        selected = list(COREV_DV_TESTS)
    elif args.tests:
        selected = args.tests
    else:
        selected = list(TESTS)
        if args.include_parked:
            selected += PARKED
        if args.include_corev_dv:
            selected += COREV_DV_TESTS

    sim_dir = TESTBENCHES[args.tb]["sim_dir"]
    if not sim_dir.is_dir():
        sys.exit(f"error: sim directory for --tb {args.tb} not found at {sim_dir}")

    needs_corev_dv = any(test in COREV_DV_TESTS for test in selected)
    if needs_corev_dv and not args.parse_only:
        if args.tb != "uvmt":
            sys.exit("error: corev-dv tests require --tb uvmt")
        setup_corev_dv(args.tb, args.timeout)

    results = []
    width = max(len(t) for t in selected)
    for test in selected:
        action = "Parsing" if args.parse_only else "Running"
        print(f"[{action}/{args.tb}] {test} ...")
        if args.parse_only:
            outcome, detail = parse_only(args.tb, test, args.run_index, args.cfg)
        else:
            outcome, detail = run_test(args.tb, test, args.run_index, args.cfg,
                                       args.timeout, args.quiet)
        results.append((test, outcome, detail))

    # Summary.
    print()
    print("=" * (width + 24))
    print(f"testbench: {args.tb}")
    print("-" * (width + 24))
    print(f"{'TEST'.ljust(width)}   RESULT   DETAIL")
    print("-" * (width + 24))
    counts = {"PASS": 0, "FAIL": 0, "ERROR": 0}
    for test, outcome, detail in results:
        counts[outcome] = counts.get(outcome, 0) + 1
        print(f"{test.ljust(width)}   {outcome:<6}   {detail}")
    print("=" * (width + 24))
    total = len(results)
    print(f"{total} test(s): {counts['PASS']} passed, "
          f"{counts['FAIL']} failed, {counts['ERROR']} error(s)")

    # Exit non-zero unless everything ran and passed.
    sys.exit(0 if counts["PASS"] == total else 1)


if __name__ == "__main__":
    main()
