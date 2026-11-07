"""Paths, pinned versions and the process environment shared by the Android scripts.

Every JDK, SDK and cache location the scripts hand to Gradle and the SDK tools is decided here. There are two
setups:

- In-repository toolchain (always on Windows; on macOS and Linux once setup_toolchain.py --in-repo has installed
  it): the JDK, the SDK, Gradle's and the SDK tools' caches all live in build/android-toolchain/ of the main
  checkout, shared by every worktree so the NDK is downloaded once and all worktrees sign and pair with the same
  keys. MUSICUNLOCK_ANDROID_TOOLCHAIN overrides that folder.
- Otherwise (macOS and Linux): JAVA_HOME / ANDROID_HOME when set, else the platform defaults, with Gradle's and
  the SDK tools' own default cache folders.

Temporary files always go to apps/android/build/tmp of the current checkout.
"""
from pathlib import Path
import os
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent.parent
BUILD = ROOT / "build"
WINDOWS = os.name == "nt"
MAC = sys.platform == "darwin"
EXE = ".exe" if WINDOWS else ""
JDK_FOLDER = "jdk-21.0.11+10"


def pinned(key: str) -> str:
    """A version from the [versions] table of gradle/libs.versions.toml, the single place versions are pinned."""
    catalog = (ROOT / "gradle" / "libs.versions.toml").read_text(encoding="utf-8")
    return re.search(rf'^{key} = "([^"]+)"', catalog, re.MULTILINE).group(1)


def main_checkout() -> Path:
    """The main checkout of this repository: the parent of the .git folder all worktrees share."""
    try:
        common = subprocess.run(["git", "-C", str(REPO), "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                capture_output=True, text=True, check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return REPO
    return Path(common).parent


TOOLCHAIN = Path(os.environ.get("MUSICUNLOCK_ANDROID_TOOLCHAIN") or main_checkout() / "build" / "android-toolchain")
TOOLCHAIN_JDK = TOOLCHAIN / JDK_FOLDER / ("Contents/Home" if MAC else "")
TOOLCHAIN_SDK = TOOLCHAIN / "sdk"
# adb ignores ANDROID_USER_HOME: its key and known hosts go to <home>/.android, see adb_environment.
ADB_HOME = TOOLCHAIN / "adb-home"


def java_binary(java: Path) -> Path:
    return java / "bin" / f"java{EXE}"


def uses_toolchain() -> bool:
    """Whether the scripts use the in-repository toolchain."""
    return WINDOWS or java_binary(TOOLCHAIN_JDK).is_file()


def android_environment(base: dict | None = None, check: bool = True) -> dict:
    """Return the environment for Gradle and the SDK tools; check=False skips checking the JDK and SDK exist."""
    env = dict(os.environ if base is None else base)
    tmp = BUILD / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(tmp), TEMP=str(tmp), TMPDIR=str(tmp))
    if uses_toolchain():
        java, sdk = TOOLCHAIN_JDK, TOOLCHAIN_SDK
        env["GRADLE_USER_HOME"] = str(TOOLCHAIN / "gradle-home")
        env["ANDROID_USER_HOME"] = str(TOOLCHAIN / "android-user-home")
        # A separate Kotlin daemon would keep its run files in the user's application data folder.
        env["ORG_GRADLE_PROJECT_kotlin.compiler.execution.strategy"] = "in-process"
        for legacy in ["ANDROID_SDK_HOME", "ANDROID_PREFS_ROOT"]:
            env.pop(legacy, None)
        setup = "python apps/android/scripts/setup_toolchain.py"
    else:
        java = Path(env.get("JAVA_HOME") or Path.home() / "jdk" / JDK_FOLDER / "Contents" / "Home")
        sdk = Path(env.get("ANDROID_HOME") or env.get("ANDROID_SDK_ROOT") or Path.home() / "Library" / "Android" / "sdk")
        setup = "设置 JAVA_HOME / ANDROID_HOME，或运行 python3 apps/android/scripts/setup_toolchain.py --in-repo"
    if check and not java_binary(java).is_file():
        sys.exit(f"未找到 JDK 21：{java}；{setup}")
    if check and not (sdk / "platforms").is_dir():
        sys.exit(f"未找到 Android SDK：{sdk}；{setup}")
    env["JAVA_HOME"] = str(java)
    env["ANDROID_HOME"] = str(sdk)
    env.pop("ANDROID_SDK_ROOT", None)
    env["PATH"] = os.pathsep.join([str(java / "bin"), str(sdk / "platform-tools"), env.get("PATH", "")])
    return env


def print_environment(env: dict) -> None:
    for key in ["JAVA_HOME", "ANDROID_HOME", "GRADLE_USER_HOME", "ANDROID_USER_HOME", "TMP"]:
        print(f"{key}: {env.get(key, '(default)')}", flush=True)


def adb_environment(env: dict) -> tuple[dict, Path | None]:
    """The environment and working folder for adb, whose server outlives the command that starts it.

    With the in-repository toolchain the server runs in, and logs to, the toolchain folder rather than this
    checkout, so it never holds a worktree that is about to be removed. On macOS and Linux adb takes its home
    folder from HOME, which then points into the toolchain too.
    """
    if not uses_toolchain():
        return env, None
    ADB_HOME.mkdir(parents=True, exist_ok=True)
    env = dict(env, ANDROID_ADB_LOG_PATH=str(ADB_HOME / "adb.log"))
    if not WINDOWS:
        env["HOME"] = str(ADB_HOME)
    return env, ADB_HOME


def adb(env: dict, serial: str | None, *args: str, **kwargs) -> subprocess.CompletedProcess:
    tool = Path(env["ANDROID_HOME"]) / "platform-tools" / f"adb{EXE}"
    target = ["-s", serial] if serial else []
    adb_env, folder = adb_environment(env)
    return subprocess.run([str(tool), *target, *args], env=adb_env, cwd=folder, **{"text": True, **kwargs})
