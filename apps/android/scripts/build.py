#!/usr/bin/env python3
"""Build the Android app (arm64-v8a) with the Gradle wrapper, then run the shared core's tests.

The core tests run on the host with the portable AES Android uses (macOS and Linux hosts; Windows covers them with
apps/windows/scripts/build.py). With --device they are also cross-compiled for arm64 Android and run on an emulator
through adb, together with the fixtures. Only emulators are accepted, never a connected phone.

Runs on macOS, Windows and Linux (CI) hosts. android_env.py decides which JDK and Android SDK to use (the
in-repository toolchain from setup_toolchain.py, or JAVA_HOME / ANDROID_HOME and the platform defaults). The NDK and
CMake are the versions pinned in gradle/libs.versions.toml, installed into that SDK by setup_toolchain.py.

All output and temporary files stay under apps/android/build/.
"""
from pathlib import Path
import argparse
import subprocess
import sys

from android_env import BUILD, EXE, REPO, ROOT, WINDOWS, adb, android_environment, pinned, print_environment

NATIVE_PROJECT = ROOT / "core" / "src" / "main" / "cpp"
DEVICE_DIR = "/data/local/tmp/musicunlock-core-tests"


def gradle(env: dict, *tasks: str) -> None:
    wrapper = ROOT / ("gradlew.bat" if WINDOWS else "gradlew")
    subprocess.run([str(wrapper), *tasks], cwd=ROOT, env=env, check=True)


def suffix_property(suffix: str | None) -> list[str]:
    """The Gradle property giving a debug build its applicationIdSuffix (see app/build.gradle.kts)."""
    return [f"-Pmusicunlock.suffix={suffix}"] if suffix else []


def native_tools(env: dict) -> tuple[Path, Path]:
    """Return the pinned NDK folder and the bin folder of the pinned CMake (which also holds Ninja)."""
    sdk = Path(env["ANDROID_HOME"])
    ndk = sdk / "ndk" / pinned("ndk")
    cmake_bin = sdk / "cmake" / pinned("cmake") / "bin"
    if not ndk.is_dir() or not (cmake_bin / f"cmake{EXE}").is_file():
        sys.exit(f"未找到固定版本的 NDK 或 CMake（{pinned('ndk')}、{pinned('cmake')}），"
                 "请运行 python apps/android/scripts/setup_toolchain.py")
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


def run_device_tests(env: dict, ndk: Path, cmake_bin: Path, serial: str, device_dir: str = DEVICE_DIR) -> None:
    """Cross-compile musicunlock_core_tests for arm64 and run it with the fixtures in device_dir, then delete that."""
    tests = build_core_tests(
        env, cmake_bin, BUILD / "core-tests-android",
        f"-DCMAKE_TOOLCHAIN_FILE={(ndk / 'build' / 'cmake' / 'android.toolchain.cmake').as_posix()}",
        "-DANDROID_ABI=arm64-v8a", f"-DANDROID_PLATFORM=android-{pinned('minSdk')}",
        f"-DMU_TEST_FIXTURES_DIR={device_dir}/fixtures", f"-DMU_TEST_SCRATCH_DIR={device_dir}/scratch")
    print(f"在 {serial} 上运行核心测试", flush=True)
    adb(env, serial, "shell", f"rm -rf {device_dir} && mkdir -p {device_dir}/scratch", check=True)
    try:
        adb(env, serial, "push", str(REPO / "tests" / "fixtures" / "qmc"), f"{device_dir}/fixtures", check=True)
        adb(env, serial, "push", str(tests), f"{device_dir}/musicunlock_core_tests", check=True)
        result = adb(env, serial, "shell", f"cd {device_dir} && chmod 755 musicunlock_core_tests && "
                     "./musicunlock_core_tests")
        if result.returncode != 0:
            sys.exit(f"{serial} 上的核心测试失败（退出码 {result.returncode}）")
    finally:
        adb(env, serial, "shell", f"rm -rf {device_dir}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="debug", choices=["debug", "release"])
    parser.add_argument("--no-test", action="store_true", help="skip the core tests on the host")
    parser.add_argument("--device", action="store_true", help="also run the core tests on an arm64 emulator")
    parser.add_argument("--serial", help="emulator for --device (default: ANDROID_SERIAL or the only one running)")
    parser.add_argument("--suffix", help="debug only: install as io.github.backspace135.musicunlock.<suffix>, "
                        "named 音匣 (<suffix>), next to other branches' builds")
    args = parser.parse_args()
    if args.suffix and args.config != "debug":
        parser.error("--suffix 只用于 debug 版")

    env = android_environment()
    ndk, cmake_bin = native_tools(env)
    print_environment(env)
    print(f"NDK: {ndk}\nCMake: {cmake_bin}", flush=True)
    # Choose the emulator before the long build, so a wrong serial fails fast.
    serial = emulator_serial(env, args.serial) if args.device else None

    gradle(env, f":app:assemble{args.config.capitalize()}", *suffix_property(args.suffix))
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
