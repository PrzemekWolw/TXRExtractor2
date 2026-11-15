#!/usr/bin/env python3
"""Configure, build, self-test, and zip TXRExtractor2.

    python build_release.py --linux --x64
    python build_release.py --windows --arm64
"""

import argparse
import os
import platform
import shutil
import stat
import subprocess
import sys
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent
APP = "TXRExtractor2"


def host_target():
    system = platform.system().lower()
    machine = platform.machine().lower()
    name = {"linux": "linux", "darwin": "macos", "windows": "windows"}.get(system)
    arch = "arm64" if machine in {"arm64", "aarch64"} else "x64" if machine in {"x86_64", "amd64"} else None
    return name, arch


def run(cmd, **kwargs):
    print("+", " ".join(cmd))
    subprocess.run(cmd, check=True, **kwargs)


def build(name, arch, clean):
    system, host_arch = host_target()
    build_dir = ROOT / ".build" / f"{name}-{arch}"
    if clean and build_dir.exists():
        shutil.rmtree(build_dir)
    cmake = ["cmake", "-S", str(ROOT), "-B", str(build_dir), "-DCMAKE_BUILD_TYPE=Release"]
    if name == "windows":
        cmake += ["-A", "ARM64" if arch == "arm64" else "x64"]
    elif name == "macos":
        cmake += ["-DCMAKE_OSX_ARCHITECTURES=arm64"]
    elif name == "linux" and arch == "arm64" and host_arch != "arm64":
        cmake += [
            "-DCMAKE_SYSTEM_NAME=Linux",
            "-DCMAKE_SYSTEM_PROCESSOR=aarch64",
            "-DCMAKE_C_COMPILER=aarch64-linux-gnu-gcc",
            "-DCMAKE_CXX_COMPILER=aarch64-linux-gnu-g++",
        ]
    run(cmake)
    run(["cmake", "--build", str(build_dir), "--config", "Release", "--parallel"])
    exe = build_dir / "Release" / (APP + ".exe")
    if not exe.exists():
        exe = build_dir / (APP + (".exe" if name == "windows" else ""))
    if name == system and arch == host_arch:
        run([str(exe), "--selftest"])
    dist = ROOT / "dist"
    dist.mkdir(exist_ok=True)
    zip_path = dist / f"{APP}-{name}-{arch}.zip"
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.write(exe, exe.name)
        for folder in ("fonts", "data"):
            for path in (ROOT / folder).rglob("*"):
                if path.is_file():
                    archive.write(path, path.relative_to(ROOT).as_posix())
        for name_ in ("README.md", "LICENSE", "NOTICE"):
            archive.write(ROOT / name_, name_)
    os.chmod(exe, exe.stat().st_mode | stat.S_IEXEC)
    print(zip_path)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--linux", action="store_true")
    parser.add_argument("--macos", action="store_true")
    parser.add_argument("--windows", action="store_true")
    parser.add_argument("--x64", action="store_true")
    parser.add_argument("--arm64", action="store_true")
    parser.add_argument("--clean", action="store_true")
    args = parser.parse_args()
    names = [name for name, flag in (("linux", args.linux), ("macos", args.macos), ("windows", args.windows)) if flag]
    arches = [arch for arch, flag in (("x64", args.x64), ("arm64", args.arm64)) if flag]
    if not names or not arches:
        print("choose a platform and an architecture", file=sys.stderr)
        return 2
    for name in names:
        for arch in arches:
            build(name, arch, args.clean)
    return 0


if __name__ == "__main__":
    sys.exit(main())
