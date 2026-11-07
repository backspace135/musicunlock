"""Paths, pinned versions and the process environment shared by the Android scripts.

Every JDK, SDK and cache location the scripts hand to Gradle and the SDK tools is decided here.
"""
from pathlib import Path
import os
import re
import sys

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent.parent
BUILD = ROOT / "build"
WINDOWS = os.name == "nt"
EXE = ".exe" if WINDOWS else ""


def pinned(key: str) -> str:
    """A version from the [versions] table of gradle/libs.versions.toml, the single place versions are pinned."""
    catalog = (ROOT / "gradle" / "libs.versions.toml").read_text(encoding="utf-8")
    return re.search(rf'^{key} = "([^"]+)"', catalog, re.MULTILINE).group(1)


def android_environment(base: dict | None = None) -> dict:
    """Return the environment for Gradle and the SDK tools."""
    env = dict(os.environ if base is None else base)
    home = Path.home()
    tmp = BUILD / "tmp"
    tmp.mkdir(parents=True, exist_ok=True)
    env.update(TMP=str(tmp), TEMP=str(tmp), TMPDIR=str(tmp))
    if WINDOWS:
        # Keep Gradle's and the SDK tools' caches inside the repository instead of the user profile.
        env["GRADLE_USER_HOME"] = str(BUILD / "gradle-home")
        env["ANDROID_USER_HOME"] = str(BUILD / "android-user-home")
        java_default = None
        sdk_default = Path(env["LOCALAPPDATA"]) / "Android" / "Sdk" if env.get("LOCALAPPDATA") else None
    else:
        java_default = home / "jdk" / "jdk-21.0.11+10" / "Contents" / "Home"
        sdk_default = home / "Library" / "Android" / "sdk"
    java = Path(env["JAVA_HOME"]) if env.get("JAVA_HOME") else java_default
    sdk_path = env.get("ANDROID_HOME") or env.get("ANDROID_SDK_ROOT")
    sdk = Path(sdk_path) if sdk_path else sdk_default
    if not java or not (java / "bin" / ("java.exe" if WINDOWS else "java")).is_file():
        sys.exit(f"未找到 JDK 21：{java or '请设置 JAVA_HOME'}")
    if not sdk or not (sdk / "platforms").is_dir():
        sys.exit(f"未找到 Android SDK：{sdk or '请设置 ANDROID_HOME'}")
    env["JAVA_HOME"] = str(java)
    env["ANDROID_HOME"] = str(sdk)
    env.pop("ANDROID_SDK_ROOT", None)
    env["PATH"] = os.pathsep.join([str(java / "bin"), str(sdk / "platform-tools"), env.get("PATH", "")])
    return env
