#!/usr/bin/env python3
"""Render the WinUI app's previews on a private desktop (Windows only).

A one-shot scheduled task (MusicUnlockShot-<checkout id>) starts tools/desktop_shot.exe in the signed-in session
(an SSH session has no desktop compositor). The helper creates a separate desktop, runs the app there in
render mode and closes the desktop; the user's own desktop is never shown or captured. The task is deleted
right after the run. PNGs go to build/previews. --convert also imports and converts sample files and checks the output;
--dist runs the staged folder from stage_app.py (build/dist/MusicUnlock) instead of build/app-release;
--single runs the single-file build/dist/MusicUnlock.exe from pack.py, which extracts into build/runtime (kept
between runs, so the second run is a warm start). With --convert it also gets a file on its command line, as if
the file had been dropped on the exe. Every run checks the taskbar identity the app read back from its window:
through the single exe it pins and relaunches that exe, run directly it sets none. --shortcut plain|app-id starts
the single exe from a shortcut without or with the app's AppUserModelID; without it, the shortcut keeps the
identity and the app sets none.
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
    # Rebuild when the source or the build flags below change.
    newest = max(source.stat().st_mtime, Path(__file__).stat().st_mtime)
    if helper.exists() and helper.stat().st_mtime >= newest:
        return helper
    helper.parent.mkdir(parents=True, exist_ok=True)
    tmp = BUILD / "tmp"
    env = msvc_environment(dict(os.environ, TMP=str(tmp), TEMP=str(tmp)))
    env.update(TMP=str(tmp), TEMP=str(tmp))
    cl = shutil.which("cl", path=env.get("PATH") or env.get("Path"))
    subprocess.run([cl, "/nologo", "/utf-8", "/W4", "/EHsc", "/O2", "/MT", "/std:c++20", str(source),
                    f"/Fe{helper}", f"/Fo{helper.parent}\\", "/link", "user32.lib", "ole32.lib", "shlwapi.lib",
                    # No console: the task starts it in the user's session, where a console window would show.
                    "/SUBSYSTEM:WINDOWS", "/ENTRY:wmainCRTStartup"], env=env, check=True)
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


def check_taskbar(output: Path, single_exe: Path | None) -> None:
    """app/Taskbar.cpp: through the single exe the process and window carry the app's ID and the window relaunches
    the single exe (cleared again before it is destroyed); run directly, nothing is set."""
    log = output / "taskbar.log"
    lines = log.read_text(encoding="utf-8-sig").splitlines() if log.exists() else []
    print("\n".join(lines))
    found = dict(line.split(": ", 1) for line in lines)
    names = ["ID", "RelaunchCommand", "RelaunchDisplayNameResource", "RelaunchIconResource"]
    none = {f"window {name}": "(none)" for name in names}
    if single_exe:
        expected = {"process ID": "MusicUnlock.App", "window ID": "MusicUnlock.App",
                    "window RelaunchCommand": f'"{single_exe}"', "window RelaunchDisplayNameResource": "音匣",
                    "window RelaunchIconResource": f"{single_exe},-1",
                    **{f"cleared {name}": "(none)" for name in names}}
    else:
        expected = {"process ID": "(none)", **none}
    # Paths compare case-insensitively, as Windows does.
    if {k: v.lower() for k, v in found.items()} != {k: v.lower() for k, v in expected.items()}:
        sys.exit("任务栏属性不对：\n" + "\n".join(lines))
    print("taskbar:", "pins and relaunches the single exe" if single_exe else "no explicit identity")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--convert", action="store_true", help="import and convert sample files, then capture")
    shape = parser.add_mutually_exclusive_group()
    shape.add_argument("--dist", action="store_true", help="run build/dist/MusicUnlock from stage_app.py")
    shape.add_argument("--single", action="store_true", help="run the single build/dist/MusicUnlock.exe from pack.py")
    parser.add_argument("--shortcut", choices=["plain", "app-id"],
                        help="with --single: start from a shortcut without or with the app's AppUserModelID")
    args = parser.parse_args()
    if args.shortcut and not args.single:
        parser.error("--shortcut 需要 --single")
    if os.name != "nt":
        sys.exit("只能在 Windows 上运行")
    if args.single:
        app = BUILD / "dist" / "MusicUnlock.exe"
    else:
        app = BUILD / ("dist/MusicUnlock" if args.dist else "app-release") / "MusicUnlock.exe"
    if not app.exists():
        sys.exit("请先运行 scripts/pack.py" if args.single else
                 "请先运行 scripts/stage_app.py" if args.dist else "请先运行 scripts/build_app.py")
    print("app:", app)
    helper = build_helper()
    output = BUILD / "previews"
    shutil.rmtree(output, ignore_errors=True)
    output.mkdir(parents=True)
    arguments = [*(["--shortcut", args.shortcut] if args.shortcut else []), DESKTOP, str(app), str(output)]
    if args.convert:
        folder, audio = prepare_conversion()
        arguments.append(str(folder))
        if args.single:
            # Outside the imported folder: it only reaches the queue through the command line.
            dropped = BUILD / "tmp" / "app-e2e-drop" / "拖入 歌曲.uc"
            shutil.rmtree(dropped.parent, ignore_errors=True)
            dropped.parent.mkdir(parents=True)
            dropped.write_bytes(bytes(b ^ 0xA3 for b in audio))
            arguments.append(str(dropped))
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
    if args.single:
        launcher_log = output / "launcher.log"
        text = launcher_log.read_text(encoding="utf-8-sig") if launcher_log.exists() else ""
        print(text)
        if " started " not in text:
            sys.exit("启动器没有启动 App")
    check_taskbar(output, app if args.single and args.shortcut != "plain" else None)
    for png in sorted(output.glob("*.png")):
        print(png)
    if not args.convert:
        # The account previews start WebView2 once; its data must follow MUSICUNLOCK_DATA_DIR.
        webview = output / "appdata" / "WebView2" / "qq"
        if not webview.is_dir() or "ready" not in (log.read_text(encoding="utf-8-sig") if log.exists() else ""):
            sys.exit(f"WebView2 未能启动，或数据没有放在 {webview}")
        print("WebView2 data folder:", webview)
    if args.convert:
        converted = folder / "输出" / "中文 歌曲.wav"
        if not converted.exists() or converted.read_bytes() != audio:
            sys.exit("端到端转换失败：输出缺失或内容不一致")
        print("end-to-end conversion: output matches the source audio")
        if args.single:
            converted = folder / "输出" / "拖入 歌曲.wav"
            if not converted.exists() or converted.read_bytes() != audio:
                sys.exit("通过命令行传入的文件没有导入或转换")
            print("command-line file: imported and converted through the launcher")
        saved = json.loads((output / "appdata" / "settings.json").read_text(encoding="utf-8"))
        if Path(saved.get("outputDirectory", "")) != folder / "输出":
            sys.exit(f"设置未保存输出文件夹：{saved}")
        print("settings saved:", saved)


if __name__ == "__main__":
    main()
