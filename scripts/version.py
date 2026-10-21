"""
PlatformIO pre-build script: defines NETIGNITE_FW_VERSION.

Release builds set the NETIGNITE_FW_VERSION environment variable (CI passes the tag without the
leading "v"). Local builds use `git describe` (e.g. "2.0.0-3-gabc1234-dirty"), or
"2.0.0-dev+<commit>" while the repository has no tags.
"""

import os
import subprocess

Import("env")  # noqa: F821 - provided by PlatformIO (SCons)

DEFAULT_BASE = "2.0.0-dev"


def git(*args):
    try:
        return subprocess.check_output(
            ["git", *args], cwd=env.subst("$PROJECT_DIR"), stderr=subprocess.DEVNULL  # noqa: F821
        ).decode().strip()
    except Exception:
        return ""


def firmware_version():
    version = os.environ.get("NETIGNITE_FW_VERSION", "").strip()
    if version:
        return version.lstrip("v")

    described = git("describe", "--tags", "--dirty", "--match", "v*")
    if described:
        return described.lstrip("v")

    commit = git("rev-parse", "--short", "HEAD")
    dirty = ".dirty" if git("status", "--porcelain") else ""
    return f"{DEFAULT_BASE}+{commit}{dirty}" if commit else DEFAULT_BASE


version = firmware_version()
print(f"NetIgnite firmware version: {version}")
env.Append(CPPDEFINES=[("NETIGNITE_FW_VERSION", env.StringifyMacro(version))])  # noqa: F821
