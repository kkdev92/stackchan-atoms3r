#!/usr/bin/env python3
"""Measure how much of src/core the host tests actually reach.

The host suite is the reason src/core holds no ESP-IDF: the whole decision
layer can be exercised on a PC. That argument is only worth as much as the
share of it the tests actually execute, and "we have 409 tests" says nothing
about that. This produces the number instead of an impression.

Why it runs the suites one at a time:

PlatformIO builds each test directory into its own program in one shared build
directory, rebuilding as it goes, so each suite's .gcda files replace the
previous suite's. Reading the result after a whole-lane run therefore reports
the last suite alone, which looks like near-zero coverage and is really a
measurement artefact. Each suite is instead run on its own and its coverage
written out as a gcovr tracefile before the next one overwrites the data, and
the tracefiles are merged at the end.

The floors below are a ratchet, not a target. They exist so that coverage
cannot quietly fall; raise them when the real number rises well past them.

Run it directly, or as the coverage lane in CI. Needs gcovr:

    pip install gcovr

Exit code 0 if coverage is at or above the floors, 1 otherwise.
"""

import argparse
import glob
import json
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TESTS = os.path.join(ROOT, "src", "test")
ENVIRONMENT = "native-coverage"

# The ratchet. Both are percentages of src/core.
#
# The branch floor is 80 because that is where someone else's bar sits, not
# because it is a round number: the OpenSSF Best Practices criterion for
# dynamic analysis accepts "an automated test suite with at least 80% branch
# coverage" in place of a fuzzer, and that is the route this project takes
# rather than running one. Falling below 80 would remove the grounds for that,
# so it fails the build instead of going quietly.
MINIMUM_LINE = 90.0
MINIMUM_BRANCH = 80.0


def suites():
    """Every native test directory, in a stable order."""
    found = sorted(name for name in os.listdir(TESTS)
                   if name.startswith("test_native_")
                   and os.path.isdir(os.path.join(TESTS, name)))
    if not found:
        sys.exit("no test_native_* directories under src/test")
    return found


def run(command, **kwargs):
    return subprocess.run(command, cwd=ROOT, text=True, **kwargs)


def collect(into):
    """Run each suite and save its coverage before the next one erases it."""
    names = suites()
    for number, suite in enumerate(names, 1):
        print("[%2d/%d] %s" % (number, len(names), suite), flush=True)

        result = run(["pio", "test", "-e", ENVIRONMENT, "-f", suite],
                     capture_output=True)
        if result.returncode != 0:
            sys.stdout.write(result.stdout)
            sys.stderr.write(result.stderr)
            sys.exit("%s failed under coverage instrumentation" % suite)

        # --json rather than a summary: only a tracefile can be merged, and a
        # per-suite percentage would be meaningless on its own anyway.
        tracefile = os.path.join(into, "%s.json" % suite)
        result = run(["gcovr", "--root", ".", "--filter", "src/core/",
                      "--json", tracefile], capture_output=True)
        if result.returncode != 0:
            sys.stderr.write(result.stderr)
            sys.exit("gcovr failed to read the coverage data for %s" % suite)


def merge(tracefiles_in):
    """Combine the per-suite tracefiles into one set of totals."""
    tracefiles = sorted(glob.glob(os.path.join(tracefiles_in, "*.json")))
    if not tracefiles:
        sys.exit("no tracefiles were produced, so nothing was measured")

    command = ["gcovr", "--root", ".", "--filter", "src/core/", "--json-summary"]
    for tracefile in tracefiles:
        command += ["--add-tracefile", tracefile]

    result = run(command, capture_output=True)
    if result.returncode != 0:
        sys.stderr.write(result.stderr)
        sys.exit("gcovr failed to merge the per-suite coverage")
    return json.loads(result.stdout), len(tracefiles)


def main():
    parser = argparse.ArgumentParser(
        description="Measure host-test coverage of src/core.")
    parser.add_argument(
        "--report-only", action="store_true",
        help="print the numbers without failing when they are below the floor. "
             "For working out where the floor should be.")
    arguments = parser.parse_args()

    if shutil.which("gcovr") is None:
        sys.exit("gcovr is not installed. pip install gcovr")

    into = tempfile.mkdtemp(prefix="stackchan-coverage-")
    try:
        collect(into)
        summary, counted = merge(into)
    finally:
        shutil.rmtree(into, ignore_errors=True)

    line = summary.get("line_percent", 0.0)
    branch = summary.get("branch_percent", 0.0)
    function = summary.get("function_percent", 0.0)

    print()
    print("src/core, merged over %d suites" % counted)
    print("  lines      %5.1f%%  (%d of %d)"
          % (line, summary.get("line_covered", 0), summary.get("line_total", 0)))
    print("  branches   %5.1f%%  (%d of %d)"
          % (branch, summary.get("branch_covered", 0), summary.get("branch_total", 0)))
    print("  functions  %5.1f%%  (%d of %d)"
          % (function, summary.get("function_covered", 0),
             summary.get("function_total", 0)))

    if arguments.report_only:
        return 0

    failures = []
    if line < MINIMUM_LINE:
        failures.append("line coverage %.1f%% is below the %.1f%% floor"
                        % (line, MINIMUM_LINE))
    if branch < MINIMUM_BRANCH:
        failures.append("branch coverage %.1f%% is below the %.1f%% floor"
                        % (branch, MINIMUM_BRANCH))

    if failures:
        print()
        for failure in failures:
            print("  %s" % failure)
        print("\nThe floors are in tools/check-coverage.py. Lowering one to go "
              "green is the wrong fix; the point of the ratchet is that it "
              "cannot happen quietly.")
        return 1

    print("\nat or above the floors (line %.0f%%, branch %.0f%%)"
          % (MINIMUM_LINE, MINIMUM_BRANCH))
    return 0


if __name__ == "__main__":
    sys.exit(main())
