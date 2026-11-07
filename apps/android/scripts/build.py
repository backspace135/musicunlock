#!/usr/bin/env python3
"""Build the Android app (arm64-v8a) with the Gradle wrapper, then run the shared core's tests.

The core tests run on the host with the portable AES Android uses (macOS and Linux hosts; Windows covers them with
apps/windows/scripts/build.py). With --device they are also cross-compiled for arm64 Android and run on an emulator
through adb, together with the fixtures. Only emulators are accepted, never a connected phone.

Runs on macOS, Windows and Linux (CI) hosts. The JDK and Android SDK come from JAVA_HOME / ANDROID_HOME when set,
otherwise from the platform defaults below. The NDK and CMake are the versions pinned in gradle/libs.versions.toml,
installed with sdkmanager into that SDK.

All output and temporary files stay under apps/android/build/.
"""
from pathlib import Path
import argparse
import subprocess
import sys

from android_env import BUILD, EXE, REPO, ROOT, WINDOWS, android_environment, pinned

NATIVE_PROJECT = ROOT / "core" / "src" / "main" / "cpp"
DEVICE_DIR = "/data/local/tmp/musicunlock-core-tests"


def gradle(env: dict, *tasks: str) -> None:
    wrapper = ROOT / ("gradlew.bat" if WINDOWS else "gradlew")
    subprocess.run([str(wrapper), *tasks], cwd=ROOT, env=env, check=True)


def native_tools(env: dict) -> tuple[Path, Path]:
    """Return the pinned NDK folder and the bin folder of the pinned CMake (which also holds Ninja)."""
    sdk = Path(env["ANDROID_HOME"])
    ndk = sdk / "ndk" / pinned("ndk")
    cmake_bin = sdk / "cmake" / pinned("cmake") / "bin"
    if not ndk.is_dir() or not (cmake_bin / f"cmake{EXE}").is_file():
        sys.exit(f'未找到固定版本的 NDK 或 CMake，请运行：sdkmanager "ndk;{pinned("ndk")}" "cmake;{pinned("cmake")}"')
    return ndk, cmake_bin


def build_core_tests(env: dict, cmake_bin: Path, build_dir: Path, *options: str) -> Path:
    """Configure the :core native project and build musicunlock_core_tests; returns the executable."""
    subprocess.run([str(cmake_bin / f"cmake{EXE}"), "-S", str(NATIVE_PROJECT), "-B", str(build_dir), "-G", "Ninja",
                    f"-DCMAKE_MAKE_PROGRAM={cmake_bin / f'ninja{EXE}'}", "-DCMAKE_BUILD_TYPE=Release", *options],
                   env=env, check=True)
    subprocess.run([str(cmake_bin / f"cmake{EXE}"), "--build", str(build_dir), "--target", "musicunlock_core_tests"],
                   env=env, check=True)
    return build_dir / "musicunlock_core_tests"


def run_host_tests(env: dict, cmake_bin: Path) -> None:
    if WINDOWS:
        print("Windows 主机上的核心测试由 apps/windows/scripts/build.py 运行，这里跳过", flush=True)
        return
    tests = build_core_tests(env, cmake_bin, BUILD / "core-tests-host", "-DMU_PORTABLE_AES=ON")
    subprocess.run([str(tests)], env=env, check=True)


def adb(env: dict, serial: str | None, *args: str, **kwargs) -> subprocess.CompletedProcess:
    tool = Path(env["ANDROID_HOME"]) / "platform-tools" / f"adb{EXE}"
    target = ["-s", serial] if serial else []
    return subprocess.run([str(tool), *target, *args], env=env, text=True, **kwargs)


def emulator_serial(env: dict, requested: str | None) -> str:
    """Pick the emulator to test on. Physical devices are refused: the tests write to /data/local/tmp."""
    listing = adb(env, None, "devices", capture_output=True, check=True).stdout.splitlines()[1:]
    online = [line.split("\t")[0] for line in listing if line.endswith("\tdevice")]
    emulators = [serial for serial in online if serial.startswith("emulator-")]
    serial = requested or env.get("ANDROID_SERIAL")
    if serial:
        if not serial.startswith("emulator-"):
            sys.exit(f"核心测试只在模拟器上运行，{serial} 不是模拟器")
        if serial not in emulators:
            sys.exit(f"模拟器 {serial} 没有在线；在线的模拟器：{', '.join(emulators) or '无'}")
    elif len(emulators) == 1:
        serial = emulators[0]
    else:
        sys.exit(f"请用 --serial 指定模拟器；在线的模拟器：{', '.join(emulators) or '无'}")
    qemu = {adb(env, serial, "shell", "getprop", prop, capture_output=True, check=True).stdout.strip()
            for prop in ["ro.kernel.qemu", "ro.boot.qemu"]}
    if "1" not in qemu:
        sys.exit(f"{serial} 不是模拟器，拒绝运行")
    return serial


def run_device_tests(env: dict, ndk: Path, cmake_bin: Path, serial: str) -> None:
    tests = build_core_tests(
        env, cmake_bin, BUILD / "core-tests-android",
        f"-DCMAKE_TOOLCHAIN_FILE={(ndk / 'build' / 'cmake' / 'android.toolchain.cmake').as_posix()}",
        "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{pinned('minSdk')}",
        f"-DMU_TEST_FIXTURES_DIR={DEVICE_DIR}/fixtures", f"-DMU_TEST_SCRATCH_DIR={DEVICE_DIR}/scratch")
    print(f"在模拟器 {serial} 上运行核心测试", flush=True)
    adb(env, serial, "shell", f"rm -rf {DEVICE_DIR} && mkdir -p {DEVICE_DIR}/scratch", check=True)
    try:
        adb(env, serial, "push", str(REPO / "tests" / "fixtures" / "qmc"), f"{DEVICE_DIR}/fixtures", check=True)
        adb(env, serial, "push", str(tests), f"{DEVICE_DIR}/musicunlock_core_tests", check=True)
        result = adb(env, serial, "shell", f"cd {DEVICE_DIR} && chmod 755 musicunlock_core_tests && "
                     "./musicunlock_core_tests")
        if result.returncode != 0:
            sys.exit(f"模拟器上的核心测试失败（退出码 {result.returncode}）")
    finally:
        adb(env, serial, "shell", f"rm -rf {DEVICE_DIR}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="debug", choices=["debug", "release"])
    parser.add_argument("--no-test", action="store_true", help="skip the core tests on the host")
    parser.add_argument("--device", action="store_true", help="also run the core tests on an arm64 emulator")
    parser.add_argument("--serial", help="emulator for --device (default: ANDROID_SERIAL or the only one running)")
    args = parser.parse_args()

    env = android_environment()
    ndk, cmake_bin = native_tools(env)
    for key in ["JAVA_HOME", "ANDROID_HOME", "GRADLE_USER_HOME", "ANDROID_USER_HOME", "TMP"]:
        print(f"{key}: {env.get(key, '(default)')}", flush=True)
    print(f"NDK: {ndk}\nCMake: {cmake_bin}", flush=True)
    # Choose the emulator before the long build, so a wrong serial fails fast.
    serial = emulator_serial(env, args.serial) if args.device else None

    gradle(env, f":app:assemble{args.config.capitalize()}")
    outputs = BUILD / "app" / "outputs" / "apk" / args.config
    apk = outputs / ("app-debug.apk" if args.config == "debug" else "app-release-unsigned.apk")
    if not apk.is_file():
        sys.exit(f"未找到 APK：{apk}")
    print(f"APK: {apk}", flush=True)
    if not args.no_test:
        run_host_tests(env, cmake_bin)
    if serial:
        run_device_tests(env, ndk, cmake_bin, serial)


if __name__ == "__main__":
    main()
