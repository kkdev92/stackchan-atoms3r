"""Put --coverage on the link line as well as the compile line.

PlatformIO passes build_flags to the compiler. The gcov runtime is a link-time
dependency, so a coverage build that only sets --coverage in build_flags
compiles cleanly and then fails at link with undefined references to
__gcov_init, __gcov_exit and __gcov_merge_add.

Used only by [env:native-coverage].
"""

Import("env")  # noqa: F821  (injected by PlatformIO)

env.Append(LINKFLAGS=["--coverage"])
