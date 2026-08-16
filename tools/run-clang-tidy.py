#!/usr/bin/env python3
"""Run the host clang-tidy installation over src/core.

The script derives include paths from the core component layout, applies
src/.clang-tidy, and treats diagnostics or tool failures as errors. Direct
invocation keeps the analyzer's resource headers and compiler configuration
together.
"""

import argparse
import os
import re
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "src", "core")
CONFIG = os.path.join(ROOT, "src", ".clang-tidy")

# Matches what platformio.ini gives the host build. Analysing under a
# different standard than the one that compiles would be analysing a
# different program.
STANDARD = "-std=gnu++17"


def find_clang_tidy():
    """clang-tidy, from the environment or the PATH."""
    from shutil import which

    explicit = os.environ.get("CLANG_TIDY")
    if explicit:
        return explicit if os.path.isfile(explicit) else which(explicit)
    return which("clang-tidy")


def include_flags():
    """One -I per component, derived from the layout rather than listed.

    A new component under src/core is picked up by existing here, which is
    the same rule CMake follows: these directories are searched one level
    deep.
    """
    flags = []
    for component in sorted(os.listdir(CORE)):
        for part in ("include", "src"):
            path = os.path.join(CORE, component, part)
            if os.path.isdir(path):
                flags.append("-I" + path)
    return flags


def sources():
    """Every translation unit, and every header.

    Headers are passed explicitly as well as being reached through the
    sources that include them, because two components are header-only and
    would otherwise never be looked at.
    """
    found = []
    for base, _, names in os.walk(CORE):
        for name in sorted(names):
            if name.endswith((".cpp", ".hpp")):
                found.append(os.path.join(base, name))
    return sorted(found)


def analyse(tool, path, extra):
    """Run clang-tidy on one file. Returns its diagnostic lines."""
    command = [tool, "--quiet", "--config-file=" + CONFIG, path, "--", STANDARD]
    command.extend(include_flags())
    command.extend(extra)

    result = subprocess.run(command, capture_output=True, text=True,
                            encoding="utf-8", errors="replace", cwd=ROOT)

    # clang-tidy writes findings to stdout and progress/tally output to stderr.
    # The tally includes diagnostics suppressed by HeaderFilterRegex.
    lines = [line for line in result.stdout.splitlines()
             if re.search(r": (warning|error): ", line)]

    # A crash or a bad argument produces no findings and a non-zero status,
    # which would otherwise pass silently.
    if result.returncode >= 2 and not lines:
        lines.append("%s: error: clang-tidy exited %d\n%s"
                     % (path, result.returncode, result.stderr.strip()))
    return lines


def main():
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument(
        "--allow-missing", action="store_true",
        help="report clang-tidy's absence and succeed; intended for optional "
             "local checks, not CI")
    parser.add_argument(
        "--jobs", type=int, default=os.cpu_count() or 4,
        help="how many files to analyse at once")
    parser.add_argument(
        "extra", nargs="*",
        help="further arguments for the compiler, after --")
    arguments = parser.parse_args()

    tool = find_clang_tidy()
    if not tool:
        message = ("clang-tidy was not found. Install it, or set CLANG_TIDY to "
                   "its path.\n"
                   "  Debian and Ubuntu:  apt install clang-tidy\n"
                   "  macOS:              brew install llvm\n"
                   "  Windows (MSYS2):    pacman -S mingw-w64-ucrt-x86_64-clang-tools-extra")
        if arguments.allow_missing:
            print("SKIPPED: " + message)
            return 0
        print("ERROR: " + message, file=sys.stderr)
        return 1

    version = subprocess.run([tool, "--version"], capture_output=True, text=True)
    print("%s\n%s" % (tool, version.stdout.strip()))

    files = sources()
    print("analysing %d files in src/core with %d jobs\n" % (len(files), arguments.jobs))

    with ThreadPoolExecutor(max_workers=arguments.jobs) as pool:
        results = pool.map(lambda f: analyse(tool, f, arguments.extra), files)

    # The same header reached through several sources reports the same
    # finding each time. Order is kept so the output reads by file.
    seen, diagnostics = set(), []
    for lines in results:
        for line in lines:
            if line not in seen:
                seen.add(line)
                diagnostics.append(line)

    if not diagnostics:
        print("no findings")
        return 0

    for line in diagnostics:
        print(line)
    print("\n%d finding(s)" % len(diagnostics))
    return 1


if __name__ == "__main__":
    sys.exit(main())
