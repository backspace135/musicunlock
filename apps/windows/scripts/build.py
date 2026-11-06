#!/usr/bin/env python3
"""Configure, build and test the portable core and CLI with CMake + Ninja.

Windows: uses the MSVC x64 toolchain found by vswhere (static CRT).
macOS: builds the same core and tests with Apple clang for quick local checks.

All output, caches and temporary files stay under apps/windows/build/.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"


def msvc_environment(base: dict) -> dict:
    vswhere = Path(os.environ["ProgramFiles(x86)"]) / "Microsoft Visual Studio" / "Installer" / "vswhere.exe"
    install = subprocess.check_output(
        [str(vswhere), "-latest", "-products", "*", "-requires",
         "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
        text=True).strip()
    if not install:
        sys.exit("未找到带 MSVC x64 工具的 Visual Studio")
    vcvars = Path(install) / "VC" / "Auxiliary" / "Build" / "vcvars64.bat"
    # /u makes cmd's `set` emit UTF-16 so non-ASCII values survive.
    raw = subprocess.run(f'cmd.exe /d /u /c ""{vcvars}" >nul && set"', capture_output=True, env=base,
                         check=True).stdout
    env = {}
    for line in raw.decode("utf-16-le").splitlines():
        key, sep, value = line.partition("=")
        if sep and key:
            env[key] = value
    return env


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    parser.add_argument("--no-test", action="store_true", help="skip ctest")
    args = parser.parse_args()

    platform = "windows" if os.name == "nt" else "macos"
    build_dir = BUILD / f"{platform}-{args.config.lower()}"
    tmp = BUILD / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TMP=str(tmp), TEMP=str(tmp), TMPDIR=str(tmp))
    if platform == "windows":
        env = msvc_environment(env)
        env.update(TMP=str(tmp), TEMP=str(tmp))
    search = env.get("PATH") or env.get("Path")
    if platform == "windows":
        # Prefer the CMake/Ninja bundled with Visual Studio over whatever else is on PATH.
        bundled = Path(env["VSINSTALLDIR"]) / "Common7" / "IDE" / "CommonExtensions" / "Microsoft" / "CMake"
        search = os.pathsep.join([str(bundled / "CMake" / "bin"), str(bundled / "Ninja"), search])
    cmake = shutil.which("cmake", path=search)
    ninja = shutil.which("ninja", path=search)
    if not cmake or not ninja:
        sys.exit("需要 cmake 和 ninja")
    print(f"cmake: {cmake}\nninja: {ninja}", flush=True)

    configure = [cmake, "-S", str(ROOT), "-B", str(build_dir), "-G", "Ninja",
                 f"-DCMAKE_BUILD_TYPE={args.config}", f"-DCMAKE_MAKE_PROGRAM={ninja}"]
    if platform == "windows":
        configure += ["-DCMAKE_CXX_COMPILER=cl", "-DCMAKE_RC_COMPILER=rc"]
    subprocess.run(configure, env=env, check=True)
    subprocess.run([cmake, "--build", str(build_dir)], env=env, check=True)
    # clangd looks for build/compile_commands.json above the sources.
    shutil.copy2(build_dir / "compile_commands.json", BUILD / "compile_commands.json")
    if not args.no_test:
        ctest = str(Path(cmake).with_name("ctest" + (".exe" if platform == "windows" else "")))
        subprocess.run([ctest, "--test-dir", str(build_dir), "--output-on-failure"], env=env, check=True)


if __name__ == "__main__":
    main()
