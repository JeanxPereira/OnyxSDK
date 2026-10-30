#!/usr/bin/env python3
"""Single build verdict: configure, build, ctest, lint.

Usage: check.py [--preset P] [--no-build] [--no-test] [--lint-only] [--ctest-regex R] [--jobs N]
"""
import argparse
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
IS_WINDOWS = os.name == "nt"
TESTS_PRESETS = {"windows-debug", "linux-debug"}


def find_vcvars():
    pf86 = os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)")
    vswhere = Path(pf86) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    if not vswhere.exists():
        raise RuntimeError(f"vswhere.exe not found at {vswhere}")
    out = subprocess.run(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
         "-property", "installationPath"],
        capture_output=True, text=True).stdout.strip()
    if not out:
        raise RuntimeError("no Visual Studio install with the C++ workload found")
    vcvars = Path(out.splitlines()[0]) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    if not vcvars.exists():
        raise RuntimeError(f"{vcvars} not found")
    return vcvars


def bat_quote(arg):
    """Quote one argument for a .bat line: cmd splits unquoted text at | & < > ^ ( )."""
    arg = arg.replace("%", "%%")
    if arg and not any(c in arg for c in ' \t"|&<>^()'):
        return arg
    return subprocess.list2cmdline([arg]) if '"' in arg or not arg else f'"{arg}"'


def run(args, vcvars):
    """Run a command line in ROOT, inside the MSVC environment on Windows."""
    if IS_WINDOWS:
        tmp = tempfile.mkdtemp()
        try:
            bat = Path(tmp) / "run.bat"
            bat.write_text(
                "@echo off\r\n"
                f'call "{vcvars}" >nul\r\n'
                "if errorlevel 1 exit /b 1\r\n"
                # ninja matches the English "Note: including file:" prefix of
                # /showIncludes; a localized cl.exe silently drops header deps.
                "set VSLANG=1033\r\n"
                + " ".join(bat_quote(x) for x in args) + "\r\n"
                "exit /b %errorlevel%\r\n")
            return subprocess.run(["cmd", "/c", str(bat)], cwd=ROOT,
                                  capture_output=True, text=True, errors="replace")
        finally:
            shutil.rmtree(tmp, ignore_errors=True)
    return subprocess.run(args, cwd=ROOT, capture_output=True, text=True, errors="replace")


def main():
    default = "windows-debug" if IS_WINDOWS else "linux-debug"
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--preset", default=default)
    ap.add_argument("--no-build", action="store_true", help="skip the build step")
    ap.add_argument("--no-test", action="store_true", help="skip the ctest step")
    ap.add_argument("--lint-only", action="store_true", help="run only the lint step")
    ap.add_argument("--ctest-regex", help="pass -R <regex> to ctest")
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 1,
                    help="parallel build jobs (default: CPU count)")
    a = ap.parse_args()
    p = a.preset

    vcvars = find_vcvars() if IS_WINDOWS and not a.lint_only else None

    ctest = ["ctest", "--preset", p] if p in TESTS_PRESETS else \
        ["ctest", "--test-dir", f"build/{p}", "--output-on-failure"]
    ctest += ["--no-tests=error"]
    if a.ctest_regex:
        ctest += ["-R", a.ctest_regex]

    steps = []
    if not a.lint_only:
        steps.append(("configure", ["cmake", "--preset", p], True))
        if not a.no_build:
            steps.append(("build", ["cmake", "--build", "--preset", p, "--parallel", str(a.jobs)], True))
        if not a.no_test:
            steps.append(("ctest", ctest, True))
    steps.append(("lint", [sys.executable, str(ROOT / "tools" / "lint.py")], False))

    for name, cmd, in_env in steps:
        t0 = time.monotonic()
        r = run(cmd, vcvars) if in_env else subprocess.run(
            cmd, cwd=ROOT, capture_output=True, text=True, errors="replace")
        out = (r.stdout or "") + (r.stderr or "")
        code = r.returncode
        dt = time.monotonic() - t0
        if code == 0:
            print(f"ok   {name} ({dt:.1f}s)")
        else:
            print(f"FAIL {name} ({dt:.1f}s)")
            print(out)
            return code
    return 0


if __name__ == "__main__":
    sys.exit(main())
