#!/usr/bin/env python3
"""Install, test and debug the app on a phone or emulator through adb (run from the repository root).

    python apps/android/scripts/device.py devices
    python apps/android/scripts/device.py pair <ip:port> <pairing code>
    python apps/android/scripts/device.py connect <ip:port>
    python apps/android/scripts/device.py install               # build this branch's debug APK, install, start it
    python apps/android/scripts/device.py uninstall
    python apps/android/scripts/device.py logcat [--dump]       # only this branch's app
    python apps/android/scripts/device.py screenshot <name>     # build/screenshots/<name>.png
    python apps/android/scripts/device.py test                  # instrumented tests, then musicunlock_core_tests
    python apps/android/scripts/device.py pull <device path> build/<folder>/
    python apps/android/scripts/device.py shell -- <command>
    python apps/android/scripts/device.py input tap <x> <y>     # also swipe, text, keyevent; only into this app

Each branch installs its own copy: the last part of the branch name becomes the application ID suffix and the
launcher name, so android/unlock installs io.github.backspace135.musicunlock.unlock as "音匣 (unlock)" and several
sessions can share one phone. --suffix overrides it; MUSICUNLOCK_ANDROID_BRANCH names the branch where the checkout
is detached (remote.py sets it). Commands that talk to a device use --serial, else ANDROID_SERIAL, else the only
device online. adb, Gradle and their caches come from android_env.py, like build.py. Device paths given to pull and
shell may start with / in Git Bash too: the conversion Git Bash applies to them is undone.

Taps, swipes, text and key presses go through `input`, which first checks that this branch's app is in front and has
the focus (not another app, the notification shade or a system dialog) and refuses otherwise; `shell` refuses to
run `input` or `monkey` itself.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import subprocess
import sys

import build
from android_env import ADB_HOME, REPO, WINDOWS, adb, adb_environment, android_environment, uses_toolchain

APPLICATION_ID = "io.github.backspace135.musicunlock"
SCREENSHOTS = REPO / "build" / "screenshots"


def branch_suffix(requested: str | None) -> str:
    """android/unlock -> unlock; the result is a valid application ID segment."""
    branch = requested or os.environ.get("MUSICUNLOCK_ANDROID_BRANCH")
    if not branch:
        branch = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--abbrev-ref", "HEAD"],
                                capture_output=True, text=True).stdout.strip()
    if not branch or branch == "HEAD":
        sys.exit("当前不在分支上，请用 --suffix 指定后缀")
    suffix = re.sub(r"[^a-z0-9_]+", "_", branch.rsplit("/", 1)[-1].lower()).strip("_")
    if not suffix:
        sys.exit(f"无法从 {branch} 生成后缀，请用 --suffix 指定")
    return suffix if suffix[0].isalpha() else f"b{suffix}"


def online_devices(env: dict) -> list[str]:
    listing = adb(env, None, "devices", capture_output=True, check=True).stdout.splitlines()[1:]
    return [line.split("\t")[0] for line in listing if line.endswith("\tdevice")]


def pick_device(env: dict, requested: str | None) -> str:
    online = online_devices(env)
    serial = requested or env.get("ANDROID_SERIAL")
    if serial:
        if serial not in online:
            sys.exit(f"设备 {serial} 没有在线；在线的设备：{', '.join(online) or '无'}")
        return serial
    if len(online) != 1:
        sys.exit(f"请用 --serial 指定设备；在线的设备：{', '.join(online) or '无'}")
    return online[0]


def app_uid(env: dict, serial: str, package: str) -> str:
    # pm filters by substring, so the unsuffixed ID also lists every suffixed copy; keep the exact match.
    listing = adb(env, serial, "shell", "pm", "list", "packages", "-U", package, capture_output=True, check=True)
    for line in listing.stdout.splitlines():
        match = re.fullmatch(r"package:(\S+) uid:(\d+)", line.strip())
        if match and match.group(1) == package:
            return match.group(2)
    sys.exit(f"{serial} 上没有安装 {package}，请先运行 device.py install")


def git_bash_root() -> str | None:
    """Where Git Bash's / is ("C:/Program Files/Git/"), when this runs from Git Bash on Windows."""
    if not WINDOWS or not os.environ.get("MSYSTEM"):
        return None
    try:
        return subprocess.run(["cygpath", "-m", "/"], capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return None


def device_paths(args: list[str]) -> list[str]:
    """Undo Git Bash's path conversion: it hands /sdcard/x to Windows programs as C:/Program Files/Git/sdcard/x."""
    root = git_bash_root()
    if not root:
        return args
    return ["/" + arg[len(root):] if arg.startswith(root) else arg for arg in args]


def inside_build(path: str) -> Path:
    """A local folder under the repository's build/, the only place device files are copied to."""
    folder = Path(path).resolve()
    allowed = (REPO / "build").resolve()
    if folder != allowed and allowed not in folder.parents:
        sys.exit(f"本地目录必须在 {allowed} 下：{folder}")
    return folder


def install(env: dict, serial: str, suffix: str) -> None:
    package = f"{APPLICATION_ID}.{suffix}"
    build.gradle(env, ":app:assembleDebug", *build.suffix_property(suffix))
    apk = build.BUILD / "app" / "outputs" / "apk" / "debug" / "app-debug.apk"
    print(f"安装 {package}（音匣 ({suffix})）到 {serial}", flush=True)
    if adb(env, serial, "install", "-r", str(apk)).returncode != 0:
        sys.exit("安装失败。签名不一致（INSTALL_FAILED_UPDATE_INCOMPATIBLE）时先运行 device.py uninstall；"
                 "INSTALL_FAILED_USER_RESTRICTED 是手机拒绝了 USB 安装，需要在手机上确认（小米：开发者选项 → USB 安装）")
    adb(env, serial, "shell", "am", "start", "-n", f"{package}/{APPLICATION_ID}.MainActivity", check=True)


INPUT_ACTIONS = {"tap", "swipe", "text", "keyevent"}
# `input` or `monkey` where the device shell starts a command: at the start, or after ; & | ( or a quote.
RAW_INPUT = re.compile(r"(?:^|[;&|(`'\"])\s*(?:input|monkey)\b")


def foreground(env: dict, serial: str) -> tuple[str | None, str | None]:
    """The package of the top resumed activity and the package owning the focused window, when known."""
    activities = adb(env, serial, "shell", "dumpsys", "activity", "activities", capture_output=True, check=True).stdout
    windows = adb(env, serial, "shell", "dumpsys", "window", capture_output=True, check=True).stdout
    resumed = re.search(r"(?:topResumedActivity=|ResumedActivity: ?)ActivityRecord\{\S+ \S+ ([\w.]+)/", activities)
    focus = re.search(r"mCurrentFocus=Window\{\S+ \S+ ([\w.]+)/", windows)
    return resumed and resumed.group(1), focus and focus.group(1)


def require_foreground(env: dict, serial: str, package: str) -> None:
    resumed, focus = foreground(env, serial)
    if resumed != package or focus != package:
        sys.exit(f"拒绝发送输入：最前面的不是 {package}（当前 Activity：{resumed or '未知'}，焦点窗口：{focus or '无'}）。"
                 "先用 device.py install 启动它，或请用户把它切到前台；不要操作其他 App")


def run_tests(env: dict, serial: str, suffix: str) -> None:
    # Start the server with adb's own environment before Gradle talks to it.
    adb(env, None, "start-server", check=True)
    build.gradle(dict(adb_environment(env)[0], ANDROID_SERIAL=serial), "connectedDebugAndroidTest",
                 *build.suffix_property(suffix))
    ndk, cmake_bin = build.native_tools(env)
    build.run_device_tests(env, ndk, cmake_bin, serial, f"/data/local/tmp/musicunlock-core-tests-{suffix}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-s", "--serial", help="device to use (default: ANDROID_SERIAL or the only one online)")
    parser.add_argument("--suffix", help="application ID suffix (default: from the branch name)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("devices", help="list connected devices")
    pair = commands.add_parser("pair", help="pair with a phone over Wi-Fi (Wireless debugging > Pair with code)")
    pair.add_argument("address", help="ip:port shown next to the pairing code")
    pair.add_argument("code")
    connect = commands.add_parser("connect", help="connect to a paired phone over Wi-Fi")
    connect.add_argument("address", help="ip:port shown under Wireless debugging")
    commands.add_parser("install", help="build this branch's debug APK, install it and start it")
    commands.add_parser("uninstall", help="uninstall this branch's app")
    logcat = commands.add_parser("logcat", help="show this branch's app's log")
    logcat.add_argument("--dump", action="store_true", help="print the current log and exit instead of following")
    screenshot = commands.add_parser("screenshot", help="save the screen to build/screenshots/<name>.png")
    screenshot.add_argument("name")
    commands.add_parser("test", help="run the instrumented tests and musicunlock_core_tests on the device")
    pull = commands.add_parser("pull", help="copy a file or folder from the device into build/")
    pull.add_argument("remote")
    pull.add_argument("local", help="a folder under the repository's build/")
    shell = commands.add_parser("shell", help="run a command on the device: shell -- <command>")
    shell.add_argument("argv", nargs=argparse.REMAINDER)
    send = commands.add_parser("input", help="tap, swipe, type or press a key, only while this app is in front")
    send.add_argument("action", choices=sorted(INPUT_ACTIONS))
    send.add_argument("values", nargs=argparse.REMAINDER, help="the arguments of adb shell input <action>")
    args = parser.parse_args()

    env = android_environment()
    if args.command == "devices":
        if uses_toolchain():
            keys = "Windows 用户目录下的 .android" if WINDOWS else ADB_HOME / ".android"
            print(f"ANDROID_HOME: {env['ANDROID_HOME']}\nadb 密钥：{keys}", flush=True)
        sys.exit(adb(env, None, "devices", "-l").returncode)
    if args.command == "pair":
        sys.exit(adb(env, None, "pair", args.address, args.code).returncode)
    if args.command == "connect":
        # adb connect reports failures on stdout with exit code 0.
        result = adb(env, None, "connect", args.address, capture_output=True)
        print(result.stdout.strip() or result.stderr.strip())
        sys.exit(0 if result.returncode == 0 and "connected to" in result.stdout else 1)

    serial = pick_device(env, args.serial)
    suffix = branch_suffix(args.suffix)
    package = f"{APPLICATION_ID}.{suffix}"
    if args.command == "install":
        install(env, serial, suffix)
    elif args.command == "uninstall":
        sys.exit(adb(env, serial, "uninstall", package).returncode)
    elif args.command == "logcat":
        app = f"--uid={app_uid(env, serial, package)}"
        sdk = adb(env, serial, "shell", "getprop", "ro.build.version.sdk", capture_output=True, check=True).stdout
        if int(sdk.strip()) < 30:  # logcat --uid arrived in Android 11; before that, follow the running process
            pid = adb(env, serial, "shell", "pidof", package, capture_output=True).stdout.strip()
            if not pid:
                sys.exit(f"{package} 没有在运行")
            app = f"--pid={pid}"
        sys.exit(adb(env, serial, "logcat", app, *(["-d"] if args.dump else [])).returncode)
    elif args.command == "screenshot":
        name = args.name.removesuffix(".png")
        if not re.fullmatch(r"[\w.-]+", name):
            sys.exit(f"截图名只能包含字母、数字、. _ -：{args.name}")
        png = adb(env, serial, "exec-out", "screencap", "-p", capture_output=True, text=False, check=True).stdout
        if not png.startswith(b"\x89PNG"):
            sys.exit(f"截图失败：{png[:200]!r}")
        SCREENSHOTS.mkdir(parents=True, exist_ok=True)
        target = SCREENSHOTS / f"{name}.png"
        target.write_bytes(png)
        print(f"截图：{target}")
    elif args.command == "test":
        run_tests(env, serial, suffix)
    elif args.command == "pull":
        folder = inside_build(args.local)
        folder.mkdir(parents=True, exist_ok=True)
        sys.exit(adb(env, serial, "pull", *device_paths([args.remote]), str(folder)).returncode)
    elif args.command == "shell":
        command = device_paths(args.argv[1:] if args.argv[:1] == ["--"] else args.argv)
        if not command:
            sys.exit("请在 -- 后面给出要在设备上运行的命令")
        # One argument is a shell command line (pipes allowed); several are quoted so the device shell keeps them.
        line = command[0] if len(command) == 1 else shlex.join(command)
        if RAW_INPUT.search(line):
            sys.exit("不要用 shell 发送输入，改用 device.py input（它会先确认最前面的是音匣）")
        sys.exit(adb(env, serial, "shell", line).returncode)
    elif args.command == "input":
        if not args.values:
            sys.exit(f"请给出 input {args.action} 的参数")
        require_foreground(env, serial, package)
        sys.exit(adb(env, serial, "shell", shlex.join(["input", args.action, *args.values])).returncode)


if __name__ == "__main__":
    main()
