#!/usr/bin/env python3
"""Restore NuGet packages and build the WinUI 3 app (Windows only).

    python scripts/build_app.py                 # Release
    python scripts/build_app.py --config Debug

Output: build/app-<config>/MusicUnlock.exe plus the self-contained Windows App SDK files.
TMP/TEMP point into build/tmp, so MSBuild inline tasks and the XAML compiler stay inside the repo.
"""
from pathlib import Path
import argparse
import os
import shutil
import subprocess
import sys

from build import BUILD, ROOT, msvc_environment


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    args = parser.parse_args()
    if os.name != "nt":
        sys.exit("WinUI 3 界面只能在 Windows 上编译")

    subprocess.run([sys.executable, str(ROOT / "scripts" / "restore.py")], check=True)
    tmp = BUILD / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    env = msvc_environment(dict(os.environ, TMP=str(tmp), TEMP=str(tmp)))
    env.update(TMP=str(tmp), TEMP=str(tmp))
    msbuild = shutil.which("msbuild", path=env.get("PATH") or env.get("Path"))
    if not msbuild:
        sys.exit("未找到 MSBuild")
    subprocess.run([msbuild, str(ROOT / "app" / "MusicUnlock.vcxproj"), f"-p:Configuration={args.config}",
                    "-p:Platform=x64", "-m", "-nologo", "-v:minimal", "-nodeReuse:false"], env=env, check=True)
    print(f"已生成：{BUILD / f'app-{args.config.lower()}' / 'MusicUnlock.exe'}")


if __name__ == "__main__":
    main()
