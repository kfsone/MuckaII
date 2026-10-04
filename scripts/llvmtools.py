"""Locate the pinned LLVM tools (clang-format, clang-tidy) for the style gates.

docs/build.md pins LLVM 22. clang-format output differs between major versions, so a different
major is refused rather than used: a gate that disagrees with the CI gate is worse than no gate.

Search order: the environment override (CLANG_FORMAT or CLANG_TIDY), PATH (also the `-22`
suffixed names apt.llvm.org installs), then the Visual Studio 18 (2026) install that vswhere
reports (any edition, prereleases included), under VC\\Tools\\Llvm\\x64\\bin. A tool that cannot
be found at the pinned major raises ToolError, whose message names every candidate and its
version.
"""

import glob
import os
import re
import shutil
import subprocess

PINNED_MAJOR = 22


class ToolError(Exception):
    pass


def _version(exe):
    try:
        out = subprocess.run([exe, "--version"], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                             timeout=60).stdout.decode("ascii", "replace")
    except (OSError, subprocess.SubprocessError):
        return None
    m = re.search(r"version (\d+)\.(\d+)\.(\d+)", out)
    return tuple(int(x) for x in m.groups()) if m else None


def vswhere_roots():
    """Installation paths of Visual Studio 18 (2026), whatever the edition or channel."""
    pf86 = os.environ.get("ProgramFiles(x86)")
    if not pf86:
        return []
    vswhere = os.path.join(pf86, "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not os.path.isfile(vswhere):
        return []
    out = subprocess.run([vswhere, "-all", "-prerelease", "-products", "*", "-version", "[18.0,19.0)",
                          "-property", "installationPath"], stdout=subprocess.PIPE).stdout
    return [p.strip() for p in out.decode("ascii", "replace").splitlines() if p.strip()]


def candidates(tool):
    found = []
    override = os.environ.get(tool.upper().replace("-", "_"))
    if override:
        found.append(override)
    for name in (tool, "%s-%d" % (tool, PINNED_MAJOR)):
        path = shutil.which(name)
        if path:
            found.append(path)
    for root in vswhere_roots():
        found += glob.glob(os.path.join(root, "VC", "Tools", "Llvm", "x64", "bin", tool + ".exe"))
    seen = []
    for c in found:
        if c not in seen:
            seen.append(c)
    return seen


def find(tool):
    """Path of `tool` at the pinned major version, or ToolError."""
    report = []
    for exe in candidates(tool):
        v = _version(exe)
        if v and v[0] == PINNED_MAJOR:
            return exe
        report.append("%s (%s)" % (exe, ".".join(map(str, v)) if v else "version unreadable"))
    var = tool.upper().replace("-", "_")
    raise ToolError(
        "%s %d.x not found, so the style gate cannot run. Found: %s. Install LLVM %d (apt.llvm.org, "
        "the Visual Studio 2026 C++ Clang tools, or `choco install llvm --version=22`) or point %s at it."
        % (tool, PINNED_MAJOR, "; ".join(report) if report else "nothing",
           PINNED_MAJOR, var))
