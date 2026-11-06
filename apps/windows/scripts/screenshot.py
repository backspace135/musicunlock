#!/usr/bin/env python3
"""Render the WinUI app's previews on a private desktop (Windows only).

A one-shot scheduled task named MusicUnlockShot starts tools/desktop_shot.exe in the signed-in session
(an SSH session has no desktop compositor). The helper creates a separate desktop, runs the app there in
render mode and closes the desktop; the user's own desktop is never shown or captured. The task is deleted
right after the run. PNGs go to build/previews.
"""
from pathlib import Path
import os
import shutil
import subprocess
import sys
import time

from build import BUILD, ROOT, msvc_environment

TASK = "MusicUnlockShot"


def build_helper() -> Path:
    source = ROOT / "tools" / "desktop_shot.cpp"
    helper = BUILD / "tools" / "desktop_shot.exe"
    if helper.exists() and helper.stat().st_mtime >= source.stat().st_mtime:
        return helper
    helper.parent.mkdir(parents=True, exist_ok=True)
    tmp = BUILD / "tmp"
    env = msvc_environment(dict(os.environ, TMP=str(tmp), TEMP=str(tmp)))
    env.update(TMP=str(tmp), TEMP=str(tmp))
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    subprocess.run([cl, "/nologo", "/utf-8", "/W4", "/EHsc", "/O2", "/MT", "/std:c++20", str(source),
                    f"/Fe{helper}", f"/Fo{helper.parent}\\", "/link", "user32.lib"], env=env, check=True)
    return helper


def main() -> None:
    if os.name != "nt":
        sys.exit("只能在 Windows 上运行")
    app = BUILD / "app-release" / "MusicUnlock.exe"
    if not app.exists():
        sys.exit("请先运行 scripts/build_app.py")
    helper = build_helper()
    output = BUILD / "previews"
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    command = f'"{helper}" "{app}" "{output}"'
    user = os.environ["USERNAME"]
    subprocess.run(["schtasks", "/Create", "/TN", TASK, "/TR", command, "/SC", "ONCE", "/ST", "23:59",
                    "/RU", user, "/IT", "/F"], check=True, capture_output=True)
    try:
        subprocess.run(["schtasks", "/Run", "/TN", TASK], check=True, capture_output=True)
        done = output / "shot.done"
        deadline = time.time() + 150
        while not done.exists() and time.time() < deadline:
            time.sleep(1)
    finally:
        subprocess.run(["schtasks", "/Delete", "/TN", TASK, "/F"], capture_output=True)
    if not done.exists():
        sys.exit("截图超时；计划任务已删除")
    print("app exit:", done.read_text().strip())
    log = output / "app.log"
    if log.exists():
        print(log.read_text(encoding="utf-8-sig"))
    for png in sorted(output.glob("*.png")):
        print(png)


if __name__ == "__main__":
    main()
