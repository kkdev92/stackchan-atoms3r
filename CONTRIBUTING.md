# Contributing

Thank you for helping improve stackchan-atoms3r. Bug fixes, documentation
updates, tests, and focused feature work are welcome. Draft pull requests are
also welcome when you want early feedback.

## Development setup

Required:

- Python 3
- PlatformIO Core 6.1.19
- A C++17 compiler for native tests

Optional checks require:

- `clang-tidy`, installed separately
- WSL and QEMU for the emulator boot check
- An AtomS3R and voice base for hardware-dependent changes

The repository pins the firmware toolchain and managed component versions.
PlatformIO downloads those dependencies during the first build.

## Repository structure

```text
src/core/       hardware-independent protocol, state, parsers, and policies
src/platform/   hardware and transport adapters
src/main/       component construction and dependency wiring
src/test/       native test suites
```

`src/core` must remain buildable without platform headers. The native
environment enforces this boundary.

## Checks

Run the checks that apply to your change.

For documentation-only changes:

```bash
python tools/check-invariants.py
```

For changes in `src/core`:

```bash
python tools/check-invariants.py
pio test -e native
pio check -e native
python tools/run-clang-tidy.py
```

For firmware, configuration, or platform changes, also build the affected
environment. Before release, build all four firmware environments:

```bash
pio run -e atoms3r-safe
pio run -e atoms3r-debug
pio run -e atoms3r-qemu
pio run -e atoms3r-release
```

For startup changes:

```bash
tools/run-qemu.sh --check
# Windows:
.\tools\run-qemu.ps1 -Check
```

Before the first run, use `tools/run-qemu.sh --install-only` on Linux or
`.\tools\run-qemu.ps1 -Install` on Windows.

On Windows, `.\tools\check.ps1` runs the local checks.
`.\tools\check.ps1 -All` also builds every firmware environment and runs
the emulator check.

If a check is unavailable in your environment, describe what you ran and what
remains in the pull request. A maintainer can complete environment-specific
verification.

## Design requirements

These requirements keep behavior testable and the external interface
consistent:

1. Keep platform dependencies out of `src/core`.
2. Register commands through `CommandRegistry`; capability output is
   generated from that registry.
3. Give blocking operations a deadline.
4. Make long-running operations observe cancellation.
5. Keep mutable runtime state owned by one task and share snapshots or
   messages across tasks.
6. Keep board pins and measured limits in the board profile or hardware
   documentation.
7. Update the device-interface document and tests with any protocol change.

The rationale is in
[docs/architecture/design-principles.md](docs/architecture/design-principles.md).

## Tests

A behavior change should include a test when the behavior can be exercised
without hardware. A bug fix should preferably include a test that fails before
the fix.

Native test directories must start with `test_native_` to match the configured
test filter.
Use fixed input and observable results; avoid timing tests that depend on host
load.

Hardware-dependent changes should include:

- the board and base used
- the firmware environment
- the behavior observed
- relevant startup or command output with secrets removed

## Static analysis and formatting

- cppcheck: `pio check -e native`
- clang-tidy: `python tools/run-clang-tidy.py`

A suppression should be narrow and include a short reason. Do not reformat
unrelated files as part of a functional change. The current tree is not
enforced as globally clang-format-clean.

**clang-tidy's check set depends on its version.** `src/.clang-tidy` selects whole
groups (`bugprone-*`, `cppcoreguidelines-*`, and so on), so a clang-tidy newer
than the one CI installs runs checks CI does not, and can report findings on
unchanged code. The tree is clean under LLVM 22 as well as under CI's version. If
a newer one surfaces something else, fix it or add it to that file **with the
reason** — pinning your clang-tidy to an older release only hides it until CI
catches up.

If `clang-tidy` is missing, `tools/check.ps1` reports it as *skipped* rather than
passed, so a green local gate on a machine without it has not checked this at all.
The CI lane installs its own.

## Documentation

Document current behavior rather than planned behavior. Keep design proposals in
an issue until they are implemented.

When recording hardware facts, include the board revision, dependency version,
test method, and a stable source link where available.

## Pull requests

Please keep a pull request focused and include:

- what changed and why
- related issue, if any
- checks performed
- hardware verification, when applicable
- documentation updates for user-visible behavior

Do not include API tokens, Wi-Fi credentials, setup passphrases, private
addresses, or unredacted core dumps in an issue or pull request.

## Releases

`PROJECT_VER` in the top-level `CMakeLists.txt` is the only authority on the
version. ESP-IDF puts it in the application descriptor and `device.describe`
reads it back, so it is what a running device answers. The README status line,
the newest `CHANGELOG.md` heading, the `device.describe` example in the API
document, and the release tag are all copies of it, and
`tools/check-invariants.py` fails when a copy disagrees.

Because this repository distributes source rather than firmware images, the tag
is what someone checks out to get a version. So derive it rather than typing it:

```bash
# From a checkout of the commit being released.
version=$(sed -n 's/^set(PROJECT_VER "\(.*\)")$/\1/p' CMakeLists.txt)
git tag -a "v${version}" -m "v${version}"
git push origin "v${version}"
```

CI runs on `v*` tags and checks the tag against `PROJECT_VER`, so a tag cut at
the wrong commit, or one cut before `PROJECT_VER` was raised, fails there rather
than reaching anyone. Release notes come from the matching `CHANGELOG.md`
section.

By contributing, you agree that your contribution is licensed under this
repository's [MIT License](LICENSE).
