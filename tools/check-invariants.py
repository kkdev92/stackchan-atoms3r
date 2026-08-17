#!/usr/bin/env python3
"""The checks that cost a second and would otherwise cost several minutes.

Two invariants, and one fact that has to agree in more than one file.

Seven invariants hold this design together (see
docs/architecture/design-principles.md). Three of them are meant to be
mechanical rather than remembered, and two of those are checkable by reading
the source alone:

  1. No ESP-IDF in src/core
  2. Dependencies point downward only

The third mechanical one, that capabilities match the implementation, is
enforced by generating them from CommandRegistry rather than by a check here.

Both of these already fail the build when broken -- the host environment
cannot see ESP-IDF's headers, and CMake refuses an unknown component. This
script exists to fail them in a second, with a sentence saying which
invariant was broken and why it matters, instead of several minutes later
inside a compiler error that does not mention the design at all.

The version is different in kind. Nothing breaks when a document states a
version the firmware does not report -- it just sends someone hunting for a
difference between their device and the documentation that is not there. So it
is checked here, against PROJECT_VER, which is the copy device.describe reads.

Run it directly, or as part of tools/check.ps1 or CI.

Exit code 0 if everything holds, 1 otherwise.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
CORE = os.path.join(ROOT, "src", "core")

# Every header the C++17 standard library provides, plus the C headers it
# inherits. An include in src/core has to be one of these or a stackchan one;
# anything else is either ESP-IDF or a new dependency that needs a decision.
#
# An explicit allowlist also catches newly introduced dependency headers.
STANDARD_HEADERS = {
    # containers and strings
    "array", "bitset", "deque", "forward_list", "list", "map", "queue", "set",
    "stack", "string", "string_view", "unordered_map", "unordered_set",
    "vector",
    # general utilities
    "algorithm", "any", "chrono", "functional", "initializer_list", "iterator",
    "limits", "memory", "memory_resource", "numeric", "optional", "random",
    "ratio", "scoped_allocator", "tuple", "type_traits", "typeindex",
    "typeinfo", "utility", "variant", "version",
    # diagnostics, concurrency, numerics
    "atomic", "cassert", "cerrno", "complex", "condition_variable", "exception",
    "execution", "future", "mutex", "new", "shared_mutex", "stdexcept",
    "system_error", "thread", "valarray",
    # text and streams
    "charconv", "codecvt", "filesystem", "fstream", "iomanip", "ios", "iosfwd",
    "iostream", "istream", "locale", "ostream", "regex", "sstream", "streambuf",
    "strstream",
    # C library
    "cctype", "cfenv", "cfloat", "cinttypes", "climits", "clocale", "cmath",
    "csetjmp", "csignal", "cstdarg", "cstddef", "cstdint", "cstdio", "cstdlib",
    "cstring", "ctime", "cuchar", "cwchar", "cwctype",
}

# Which core components each core component is allowed to require. A component
# may require anything below it and nothing at its own level or above, which is
# what keeps the graph acyclic.
#
# Kept here rather than derived from the files so a wider dependency boundary
# is an explicit, reviewable edit.
CORE_LAYERS = {
    "stackchan_domain": set(),
    "stackchan_board": set(),
    "stackchan_runtime": set(),
    "stackchan_ports": {"stackchan_domain", "stackchan_runtime"},
    "stackchan_app": {"stackchan_domain", "stackchan_runtime", "stackchan_ports"},
}

failures = []


def fail(invariant, path, message):
    failures.append((invariant, os.path.relpath(path, ROOT), message))


def sources():
    for base, _, names in os.walk(CORE):
        for name in names:
            if name.endswith((".cpp", ".hpp", ".h", ".cc")):
                yield os.path.join(base, name)


def check_no_esp_idf_in_core():
    """Invariant 1: nothing in src/core may reach for ESP-IDF."""
    include = re.compile(r'^\s*#\s*include\s*[<"]([^">]+)[">]')
    for path in sources():
        with open(path, encoding="utf-8") as handle:
            for number, line in enumerate(handle, 1):
                match = include.match(line)
                if not match:
                    continue
                header = match.group(1)
                if header.startswith("stackchan/"):
                    continue
                if header.split("/")[0].split(".")[0] in STANDARD_HEADERS and "/" not in header:
                    continue
                fail(1, path,
                     "line %d includes <%s>. src/core may include the C++ "
                     "standard library and stackchan/ headers, nothing else"
                     % (number, header))


def requires_of(cmakelists):
    """The component names in a REQUIRES or PRIV_REQUIRES clause."""
    with open(cmakelists, encoding="utf-8") as handle:
        text = handle.read()

    # Strip comments first: the CMakeLists files explain why REQUIRES is empty,
    # and those sentences contain the word REQUIRES.
    text = re.sub(r"#[^\n]*", "", text)

    found = set()
    for match in re.finditer(r"\b(?:PRIV_)?REQUIRES\b(.*?)(?=\b[A-Z_]{4,}\b|\))",
                             text, re.DOTALL):
        for word in match.group(1).split():
            if word.isidentifier():
                found.add(word)
    return found


def check_dependencies_point_downward():
    """Invariant 2: core never requires platform, and core layers are ordered."""
    platform_dir = os.path.join(ROOT, "src", "platform")
    platform_components = {
        name for name in os.listdir(platform_dir)
        if os.path.isdir(os.path.join(platform_dir, name))
    }

    for component, allowed in CORE_LAYERS.items():
        cmakelists = os.path.join(CORE, component, "CMakeLists.txt")
        if not os.path.isfile(cmakelists):
            fail(2, CORE, "%s has no CMakeLists.txt" % component)
            continue

        for required in sorted(requires_of(cmakelists)):
            if required in platform_components:
                fail(2, cmakelists,
                     "requires %s, which is a platform component. Dependencies "
                     "point downward only; this would take invariant 1 with it, "
                     "because platform components carry ESP-IDF" % required)
            elif required in CORE_LAYERS:
                if required not in allowed:
                    fail(2, cmakelists,
                         "requires %s, which is not below it. %s may require: %s"
                         % (required, component,
                            ", ".join(sorted(allowed)) or "nothing"))
            else:
                fail(2, cmakelists,
                     "requires %s, which is neither a core nor a platform "
                     "component. If that is an ESP-IDF component, it does not "
                     "belong in src/core (invariant 1)" % required)

    # Every core component must be accounted for above, or the check silently
    # stops covering a new one.
    for name in sorted(os.listdir(CORE)):
        if os.path.isdir(os.path.join(CORE, name)) and name not in CORE_LAYERS:
            fail(2, CORE,
                 "%s is a core component this check does not know about. Add it "
                 "to CORE_LAYERS in tools/check-invariants.py, with what it is "
                 "allowed to require" % name)


def check_the_version_agrees():
    """The version a reader is told must be the one the device reports.

    PROJECT_VER is the only authority: ESP-IDF puts it in the application
    descriptor, and device.describe reads it back from there. Everywhere else
    the version appears, it is a copy, and a copy that disagrees sends someone
    looking for a difference between their device and the documentation that is
    not there.
    """
    cmakelists = os.path.join(ROOT, "CMakeLists.txt")
    with open(cmakelists, encoding="utf-8") as handle:
        found = re.search(r'set\(PROJECT_VER\s+"([^"]+)"\)', handle.read())
    if not found:
        fail(0, cmakelists, "PROJECT_VER is not set, so nothing pins the "
                            "version the device reports.")
        return
    version = found.group(1)

    # Each copy, with the pattern that should carry the version.
    copies = [
        ("README.md", r'^>\s*\*\*Status:\*\*\s*(\S+)', "the status line"),
        ("CHANGELOG.md", r'^##\s*\[?(\d+\.\d+\.\d+)\]?', "the newest heading"),
        ("docs/api/device-interface.md", r'"version":\s*"([^"]+)"',
         "the device.describe example"),
    ]
    for relative, pattern, what in copies:
        path = os.path.join(ROOT, relative)
        with open(path, encoding="utf-8") as handle:
            text = handle.read()
        seen = re.findall(pattern, text, re.M)
        if not seen:
            fail(0, path, "%s does not state a version, so it cannot be checked "
                          "against PROJECT_VER (%s)." % (what, version))
            continue
        for other in sorted(set(seen)):
            if other != version:
                fail(0, path, "%s says %s; PROJECT_VER is %s, and PROJECT_VER is "
                              "what device.describe reports."
                              % (what, other, version))


def check_the_mechanism_is_still_there():
    """Invariant 1 holds because the host build cannot see anything else."""
    path = os.path.join(ROOT, "platformio.ini")
    with open(path, encoding="utf-8") as handle:
        text = handle.read()
    if not re.search(r"^\s*lib_extra_dirs\s*=\s*src/core\s*$", text, re.MULTILINE):
        fail(1, path,
             "[env:native] no longer sets lib_extra_dirs = src/core. That "
             "setting is what makes invariant 1 physical rather than a rule: "
             "without it the host build can see src/platform, and an ESP-IDF "
             "include in src/core stops being a compile error")


def main():
    check_no_esp_idf_in_core()
    check_dependencies_point_downward()
    check_the_mechanism_is_still_there()
    check_the_version_agrees()

    if not failures:
        print("invariants 1 and 2 hold, and the version agrees everywhere")
        return 0

    print("PROBLEMS: %d\n" % len(failures))
    for invariant, path, message in failures:
        label = "invariant %d" % invariant if invariant else "version    "
        print("  %s  %s" % (label, path))
        print("  %s  %s\n" % (" " * len(label), message))
    if any(invariant for invariant, _, _ in failures):
        print("See docs/architecture/design-principles.md for what each "
              "invariant is for.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
