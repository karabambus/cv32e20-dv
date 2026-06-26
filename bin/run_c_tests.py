#!/usr/bin/env python3
# Copyright 2026 Eclipse Foundation AISBL
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""
Run the cleaned-up C test-programs on a CV32E20 testbench and print a pass/fail
summary.  Either testbench can be selected with --tb:

    core   the Verilator core testbench in sim/core            (default)
    uvmt   the UVM testbench in sim/uvmt, run with SIMULATOR=dsim

For each selected test the script invokes `make test TEST=<name> ...` in the
chosen testbench's sim directory, then parses the per-test log it leaves behind
for the verdict banner:

    core : sim/core/simulation_results/<name>/<run>/test_program/<name>.log
           "ALL TESTS PASSED"   -> PASS   (tb/core/tb_top.sv)
           "TEST(S) FAILED!"    -> FAIL

    uvmt : sim/uvmt/dsim_results/<cfg>/<name>/<run>/dsim-<name>.log
           "SIMULATION PASSED"  -> PASS   (tb/uvmt/uvmt_cv32e20_tb.sv;
                                           includes "PASSED with WARNINGS")
           "SIMULATION FAILED"  -> FAIL

Anything else (no banner, build error, timeout) is reported as ERROR.
The process exit code is 0 only if every test that was run reported PASS.

Usage
-----
The script needs no arguments and can be run from anywhere (paths are resolved
relative to its own location in <repo>/bin).  A working RISC-V toolchain plus
the relevant simulator (Verilator for core, Metrics dsim for uvmt) must be on
PATH, as for a normal `make test`.

    # Run the full cleaned-up C set on the core TB and print the summary:
    bin/run_c_tests.py
    python3 bin/run_c_tests.py                 # equivalent

    # Run the same set on the UVM testbench (dsim):
    bin/run_c_tests.py --tb uvmt
    # (equivalent to `make test TEST=<name> SIMULATOR=dsim` per test; the
    #  script sets SIMULATOR=dsim for you, so no need to export it yourself.)

    # Run only specific tests (by directory name under tests/programs/custom):
    bin/run_c_tests.py fibonacci misalign
    bin/run_c_tests.py --tb uvmt hello-world

    # Also run the parked tests (riscv_csr + debug_test* variants):
    bin/run_c_tests.py --include-parked

    # Skip simulation; just re-summarize logs from a previous run:
    bin/run_c_tests.py --parse-only
    bin/run_c_tests.py --tb uvmt --parse-only

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
        "make_args": ["SIMULATOR=dsim"],
        "pass_banner": "SIMULATION PASSED",
        "fail_banner": "SIMULATION FAILED",
        "markers": ("SIMULATION PASSED", "SIMULATION FAILED",
                    "UVM_ERROR :", "UVM_FATAL :", "TEST PASSED", "TEST FAILED"),
    },
}

# C test-programs cleaned up on this branch that are expected to run on the
# M-only CV32E20.
TESTS = [
    "hello-world",
    "fibonacci",
    "branch_zero",
    "coremark",
    "dhrystone",
    "all_csr_por",
    "csr_instructions",
    "hpmcounter_basic_test",
    "perf_counters_instructions",
    "illegal",
    "misalign",
    "interrupt_test",
    "interrupt_bootstrap",
]

# Parked C programs: migrated to the shared header but not expected to pass on
# the core TB.  riscv_csr still needs the M-only counter-CSR reconciliation; the
# debug_test variants target the UVM environment (sim/uvmt) and may hang on the
# core TB.  Included only with --include-parked.
PARKED = [
    "riscv_csr",
    "debug_test",
    "debug_test_boot_set",
    "debug_test_reset",
    "debug_test_known_miscompares",
    "debug_test_trigger",
]


def log_path(tb, test, run_index, cfg):
    """Location of the per-test simulation log for the given testbench."""
    sim_dir = TESTBENCHES[tb]["sim_dir"]
    if tb == "core":
        return (sim_dir / "simulation_results" / test / str(run_index)
                / "test_program" / f"{test}.log")
    # uvmt / dsim
    return (sim_dir / "dsim_results" / cfg / test / str(run_index)
            / f"dsim-{test}.log")


def classify(text, tbcfg):
    """Return 'PASS', 'FAIL', or 'ERROR' for a chunk of log/console text."""
    if tbcfg["pass_banner"] in text:
        return "PASS"
    if tbcfg["fail_banner"] in text:
        return "FAIL"
    return "ERROR"


def run_test(tb, test, run_index, cfg, timeout, quiet):
    """Build+run one test on the chosen testbench; return (outcome, detail)."""
    tbcfg = TESTBENCHES[tb]
    cmd = (["make", "test", f"TEST={test}", f"RUN_INDEX={run_index}"]
           + tbcfg["make_args"])
    # SIMULATOR is passed on the make command line above; also export it in the
    # environment so any sub-make or shell that reads it behaves consistently.
    env = os.environ.copy()
    for arg in tbcfg["make_args"]:
        key, _, val = arg.partition("=")
        if val:
            env[key] = val

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
                    help="testbench to run on: 'core' (Verilator) or 'uvmt' (dsim) "
                         "(default: core)")
    ap.add_argument("--include-parked", action="store_true",
                    help="also run the parked tests (riscv_csr, debug_test variants)")
    ap.add_argument("--parse-only", action="store_true",
                    help="do not run; just parse existing logs in the results directory")
    ap.add_argument("--cfg", default="default",
                    help="uvmt config subdirectory under dsim_results (default: default)")
    ap.add_argument("--run-index", type=int, default=0,
                    help="RUN_INDEX subdirectory to use (default: 0)")
    ap.add_argument("--timeout", type=int, default=600,
                    help="per-test timeout in seconds (default: 600)")
    ap.add_argument("--quiet", action="store_true",
                    help="suppress per-test simulation banner output")
    args = ap.parse_args()

    if args.tests:
        selected = args.tests
    else:
        selected = list(TESTS)
        if args.include_parked:
            selected += PARKED

    sim_dir = TESTBENCHES[args.tb]["sim_dir"]
    if not sim_dir.is_dir():
        sys.exit(f"error: sim directory for --tb {args.tb} not found at {sim_dir}")

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
