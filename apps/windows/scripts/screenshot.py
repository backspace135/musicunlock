#!/usr/bin/env python3
"""Render the WinUI app's previews on a private desktop (Windows only).

A one-shot scheduled task (MusicUnlockShot-<checkout id>) starts tools/desktop_shot.exe in the signed-in session
(an SSH session has no desktop compositor). The helper creates a separate desktop, runs the app there in
render mode and closes the desktop; the user's own desktop is never shown or captured. The task is deleted
right after the run. PNGs go to build/previews. --convert also imports and converts sample files and checks the output.
"""
from pathlib import Path
import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
import time
from xml.sax.saxutils import escape

from build import BUILD, ROOT, msvc_environment
from smoke_cli import wave

# Task and desktop names are unique per checkout, so parallel worktrees can render at the same time.
CHECKOUT_ID = hashlib.sha256(str(ROOT).lower().encode()).hexdigest()[:8]
TASK = f"MusicUnlockShot-{CHECKOUT_ID}"
DESKTOP = f"MusicUnlockShot-{CHECKOUT_ID}"


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


def task_xml(program: Path, arguments: list[str], user: str) -> str:
    """On-demand task in the signed-in session. XML instead of schtasks /TR, which caps the whole command
    line at 261 characters (too short for worktree paths), and so it also runs on battery."""
    command_line = " ".join(f'"{argument}"' for argument in arguments)
    return f"""<?xml version="1.0" encoding="UTF-16"?>
<Task version="1.2" xmlns="http://schemas.microsoft.com/windows/2004/02/mit/task">
  <RegistrationInfo><Description>MusicUnlock preview render; deleted after the run.</Description></RegistrationInfo>
  <Principals>
    <Principal id="Author">
      <UserId>{escape(user)}</UserId>
      <LogonType>InteractiveToken</LogonType>
      <RunLevel>LeastPrivilege</RunLevel>
    </Principal>
  </Principals>
  <Settings>
    <MultipleInstancesPolicy>IgnoreNew</MultipleInstancesPolicy>
    <DisallowStartIfOnBatteries>false</DisallowStartIfOnBatteries>
    <StopIfGoingOnBatteries>false</StopIfGoingOnBatteries>
    <ExecutionTimeLimit>PT5M</ExecutionTimeLimit>
    <Enabled>true</Enabled>
  </Settings>
  <Actions Context="Author">
    <Exec>
      <Command>{escape(str(program))}</Command>
      <Arguments>{escape(command_line)}</Arguments>
    </Exec>
  </Actions>
</Task>
"""


def prepare_conversion() -> tuple[Path, bytes]:
    """A NetEase cache file that should convert and a broken NCM that should fail."""
    folder = BUILD / "tmp" / "app-e2e"
    shutil.rmtree(folder, ignore_errors=True)
    (folder / "子目录").mkdir(parents=True)
    audio = wave()
    (folder / "子目录" / "中文 歌曲.uc").write_bytes(bytes(b ^ 0xA3 for b in audio))
    (folder / "坏文件.ncm").write_bytes(b"CTENFDAM")
    return folder, audio


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--convert", action="store_true", help="import and convert sample files, then capture")
    args = parser.parse_args()
    if os.name != "nt":
        sys.exit("只能在 Windows 上运行")
    app = BUILD / "app-release" / "MusicUnlock.exe"
    if not app.exists():
        sys.exit("请先运行 scripts/build_app.py")
    helper = build_helper()
    output = BUILD / "previews"
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    arguments = [DESKTOP, str(app), str(output)]
    if args.convert:
        folder, audio = prepare_conversion()
        arguments.append(str(folder))
    definition = BUILD / "tmp" / f"{TASK}.xml"
    definition.write_text(task_xml(helper, arguments, os.environ["USERNAME"]), encoding="utf-16")
    subprocess.run(["schtasks", "/Create", "/TN", TASK, "/XML", str(definition), "/F"], check=True, capture_output=True)
    definition.unlink()
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
    if args.convert:
        converted = folder / "输出" / "中文 歌曲.wav"
        if not converted.exists() or converted.read_bytes() != audio:
            sys.exit("端到端转换失败：输出缺失或内容不一致")
        print("end-to-end conversion: output matches the source audio")
        saved = json.loads((output / "appdata" / "settings.json").read_text(encoding="utf-8"))
        if Path(saved.get("outputDirectory", "")) != folder / "输出":
            sys.exit(f"设置未保存输出文件夹：{saved}")
        print("settings saved:", saved)


if __name__ == "__main__":
    main()
