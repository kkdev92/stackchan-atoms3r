"""Refresh generated sdkconfig files when project fragments change.

An existing sdkconfig.<env> overrides SDKCONFIG_DEFAULTS, and PlatformIO's
change tracking does not include the project fragments passed through that
option. If a fragment is newer, this pre-build script removes the generated
file so CMake regenerates it. Changes made only through menuconfig can be lost
after a fragment update; persist intended settings in a source fragment.
"""

import os
import shlex

Import("env")  # noqa: F821  (injected by PlatformIO)


def _fragments_for_env(pio_env):
    """The files this environment passes through SDKCONFIG_DEFAULTS."""
    raw = pio_env.GetProjectOption("board_build.cmake_extra_args", "")
    if not raw:
        return []

    paths = []
    # The form is -DSDKCONFIG_DEFAULTS="a;b;c". shlex strips the quoting,
    # then the value splits on semicolons.
    for token in shlex.split(raw.replace("\n", " ")):
        if not token.startswith("-DSDKCONFIG_DEFAULTS="):
            continue
        value = token.split("=", 1)[1]
        for item in value.split(";"):
            item = item.strip()
            if item:
                paths.append(os.path.join(pio_env.subst("$PROJECT_DIR"), item))
    return paths


def _stale_sdkconfig(pio_env):
    generated = os.path.join(
        pio_env.subst("$PROJECT_DIR"), "sdkconfig.%s" % pio_env.subst("$PIOENV")
    )
    if not os.path.isfile(generated):
        return None, []

    newer = [
        f
        for f in _fragments_for_env(pio_env)
        if os.path.isfile(f) and os.path.getmtime(f) > os.path.getmtime(generated)
    ]
    return generated, newer


generated_path, newer_fragments = _stale_sdkconfig(env)  # noqa: F821
if newer_fragments:
    # Keep diagnostics compatible with Windows consoles using non-UTF-8 code
    # pages.
    for fragment in newer_fragments:
        print(
            "sdkconfig: %s is newer than the generated config"
            % os.path.relpath(fragment, env.subst("$PROJECT_DIR"))  # noqa: F821
        )
    print(
        "sdkconfig: removing %s so the defaults are read again"
        % os.path.basename(generated_path)
    )
    os.remove(generated_path)
