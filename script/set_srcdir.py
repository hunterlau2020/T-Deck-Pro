# script/set_srcdir.py
# PlatformIO pre-build script.
#
# src_dir is a *global-only* option in PlatformIO (and the .ino discovery only
# looks at $PROJECT_SRC_DIR/*.ino at the top level), so per-env builds are done
# by overriding PROJECT_SRC_DIR here. Env name == example folder name, except
# T-Deck-Pro which builds examples/test_GPS.
#
# An externally supplied PLATFORMIO_SRC_DIR (absolute or project-relative path)
# wins over the env mapping — CI (.github/workflows/platformio.yml) exports it
# per matrix entry, and without this check the Replace below would silently
# redirect every default-env build to examples/test_GPS and void the matrix
# (review 2026-08-07-20, P2 "Preserve CI's selected source directory").
#
# Add to the common [env] section:
#   extra_scripts = pre:script/set_srcdir.py
import os

Import("env")

ENV_TO_EXAMPLE = {
    "T-Deck-Pro": "test_GPS",
}


def _example_dir():
    ext = os.environ.get("PLATFORMIO_SRC_DIR", "").strip()
    if ext:
        return os.path.join(env.subst("$PROJECT_DIR"), ext)
    envname = env.subst("$PIOENV")
    example = ENV_TO_EXAMPLE.get(envname, envname)
    return os.path.join(env.subst("$PROJECT_DIR"), "examples", example)


env.Replace(PROJECT_SRC_DIR=_example_dir())
