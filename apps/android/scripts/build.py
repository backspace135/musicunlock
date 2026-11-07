#!/usr/bin/env python3
"""Build the Android app (arm64-v8a) with the Gradle wrapper.

Runs on macOS and Windows hosts. The JDK and Android SDK come from JAVA_HOME / ANDROID_HOME when set,
otherwise from the platform defaults below.

All output and temporary files stay under apps/android/build/.
"""
from pathlib import Path
import argparse
import os
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent
BUILD = ROOT / "build"
WINDOWS = os.name == "nt"


def android_environment(base: dict | None = None) -> dict:
    """Return the environment for Gradle and the SDK tools. Every JDK, SDK and cache location is decided here."""
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


def gradle(env: dict, *tasks: str) -> None:
    wrapper = ROOT / ("gradlew.bat" if WINDOWS else "gradlew")
    subprocess.run([str(wrapper), *tasks], cwd=ROOT, env=env, check=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="debug", choices=["debug", "release"])
    args = parser.parse_args()

    env = android_environment()
    for key in ["JAVA_HOME", "ANDROID_HOME", "GRADLE_USER_HOME", "ANDROID_USER_HOME", "TMP"]:
        print(f"{key}: {env.get(key, '(default)')}", flush=True)

    gradle(env, f":app:assemble{args.config.capitalize()}")
    outputs = BUILD / "app" / "outputs" / "apk" / args.config
    apk = outputs / ("app-debug.apk" if args.config == "debug" else "app-release-unsigned.apk")
    if not apk.is_file():
        sys.exit(f"未找到 APK：{apk}")
    print(f"APK: {apk}")


if __name__ == "__main__":
    main()
