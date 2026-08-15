"""PlatformIO pre-script: stamp the firmware with a version string.

Defines AIRDECK_VERSION for the C++ sources so a binary can self-report
which release it is (boot banner, /health endpoint, HTTP User-Agent).
The value comes from, in order of preference:

  1. The AIRDECK_VERSION environment variable — set by the release
     workflow from the git tag, so release binaries always match their
     tag exactly.
  2. `git describe --tags --always --dirty` — local and CI builds from a
     checkout get e.g. "v1.2.0-3-gabc1234" or a bare commit hash.
  3. The literal "dev" — tarball builds with no .git directory.

Deliberately avoids __DATE__/__TIME__ so identical sources produce
identical binaries (reproducible builds).
"""
import os
import subprocess

Import("env")  # noqa: F821  (injected by the PlatformIO/SCons runtime)


def _version() -> str:
    explicit = os.environ.get("AIRDECK_VERSION")
    if explicit:
        return explicit
    try:
        return subprocess.check_output(
            ["git", "describe", "--tags", "--always", "--dirty"],
            stderr=subprocess.DEVNULL,
            text=True,
        ).strip()
    except (OSError, subprocess.CalledProcessError):
        return "dev"


env.Append(CPPDEFINES=[("AIRDECK_VERSION", env.StringifyMacro(_version()))])
