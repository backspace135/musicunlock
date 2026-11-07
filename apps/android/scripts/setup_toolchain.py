#!/usr/bin/env python3
"""Install the JDK and Android SDK packages the Android scripts build with.

    python apps/android/scripts/setup_toolchain.py             # Windows: always the in-repository toolchain
    python3 apps/android/scripts/setup_toolchain.py            # macOS/Linux: packages into the existing SDK
    python3 apps/android/scripts/setup_toolchain.py --in-repo  # macOS/Linux: the in-repository toolchain

The in-repository toolchain goes to build/android-toolchain/ of the main checkout (see android_env.py): a pinned
Temurin JDK 21 and the Android command-line tools, both checked against the SHA-256 sums below, then an SDK into
which the Android CLI of the command-line tools installs platform-tools and the platform, build tools, NDK and CMake
pinned in gradle/libs.versions.toml, accepting the SDK license on the way. Without it, only those packages are
installed, into the SDK build.py would use (JAVA_HOME / ANDROID_HOME, or ~/Library/Android/sdk on macOS).

Running it again skips whatever is already installed.
"""
from pathlib import Path
import argparse
import hashlib
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile

import android_env
from android_env import EXE, JDK_FOLDER, MAC, TOOLCHAIN, TOOLCHAIN_JDK, TOOLCHAIN_SDK, WINDOWS, java_binary, pinned

TEMURIN = "https://github.com/adoptium/temurin21-binaries/releases/download/jdk-21.0.11%2B10/"
JDK_ARCHIVES = {
    ("windows", "x64"): ("OpenJDK21U-jdk_x64_windows_hotspot_21.0.11_10.zip",
                         "d3625e7cadf23787ea540229544b6e2ab494b3b54da1801879e583e1dfee0a64"),
    ("mac", "aarch64"): ("OpenJDK21U-jdk_aarch64_mac_hotspot_21.0.11_10.tar.gz",
                         "6ebcf221c9b41507b14c098e93c6ead6440b8d9bd154f8ec666c4c73abbdb201"),
    ("mac", "x64"): ("OpenJDK21U-jdk_x64_mac_hotspot_21.0.11_10.tar.gz",
                     "34180eb03e6d207c388cce3da668f6cc7cd7508c185c24782fadac2c9c0e66f9"),
    ("linux", "x64"): ("OpenJDK21U-jdk_x64_linux_hotspot_21.0.11_10.tar.gz",
                       "4b2220e232a97997b436ca6ab15cbf70171ecff52958a46159dfa5a8c44ca4de"),
}
# Command-line tools 23.0; the SHA-1 sums match Google's repository2-3.xml.
REPOSITORY = "https://dl.google.com/android/repository/"
COMMAND_LINE_TOOLS = {
    ("windows", "x64"): ("commandlinetools-win-16111833_latest.zip",
                         "e5885e2e59038c0778a85fc19f01de7ce7c567c105553f7617b936b1e0e87b2b"),
    ("mac", "aarch64"): ("commandlinetools-mac_arm64-16111833_latest.zip",
                         "58da8f215781cc0208743e88b65ae259780c55e7eb40e50373d377cf369232d6"),
    ("mac", "x64"): ("commandlinetools-mac_x86_64-16111833_latest.zip",
                     "7e601c04e7173754b3d051a87bb4c3a8243c65a9d7bfa9356ed6499fb8953fa3"),
    ("linux", "x64"): ("commandlinetools-linux-16111833_latest.zip",
                       "0877a1d048fe4a24efe2eff536ca4223f7adeb58648bb81909d33c446918cfa8"),
}


def host() -> tuple[str, str]:
    system = "windows" if WINDOWS else "mac" if MAC else "linux"
    machine = {"amd64": "x64", "x86_64": "x64", "arm64": "aarch64", "aarch64": "aarch64"}.get(platform.machine().lower())
    if (system, machine) not in JDK_ARCHIVES:
        sys.exit(f"不支持的主机：{system} {platform.machine()}")
    return system, machine


def sdk_packages() -> list[str]:
    return ["platform-tools", f"platforms;android-{pinned('compileSdk')}.0", f"build-tools;{pinned('buildTools')}",
            f"ndk;{pinned('ndk')}", f"cmake;{pinned('cmake')}"]


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as file:
        while chunk := file.read(1 << 20):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, expected: str) -> Path:
    """Download into build/android-toolchain/downloads/ (reusing a verified earlier download) and check the sum."""
    target = TOOLCHAIN / "downloads" / url.rsplit("/", 1)[1]
    if target.is_file() and sha256(target) == expected:
        return target
    target.parent.mkdir(parents=True, exist_ok=True)
    partial = target.with_name(target.name + ".part")
    print(f"下载 {url}", flush=True)
    with urllib.request.urlopen(url, timeout=60) as response, partial.open("wb") as file:
        total = int(response.headers.get("Content-Length") or 0)
        done, shown = 0, 0
        while chunk := response.read(1 << 20):
            file.write(chunk)
            done += len(chunk)
            if total and done * 10 // total > shown:
                shown = done * 10 // total
                print(f"  {done * 100 // total}%", flush=True)
    actual = sha256(partial)
    if actual != expected:
        partial.unlink()
        sys.exit(f"SHA-256 不匹配：{url}\n  期望 {expected}\n  实际 {actual}")
    partial.replace(target)
    return target


def unpack(archive: Path, inner: str, destination: Path) -> None:
    """Extract archive and move its top-level folder `inner` to destination."""
    staging = archive.with_name(archive.name + ".extract")
    shutil.rmtree(staging, ignore_errors=True)
    print(f"解压 {archive.name} 到 {destination}", flush=True)
    if archive.suffix == ".zip":
        with zipfile.ZipFile(archive) as bundle:
            for member in bundle.infolist():
                extracted = bundle.extract(member, staging)
                mode = member.external_attr >> 16
                if mode and not WINDOWS:
                    os.chmod(extracted, mode & 0o777)  # zipfile drops the executable bits
    else:
        with tarfile.open(archive) as bundle:
            if hasattr(tarfile, "data_filter"):
                bundle.extractall(staging, filter="data")
            else:
                bundle.extractall(staging)
    destination.parent.mkdir(parents=True, exist_ok=True)
    shutil.rmtree(destination, ignore_errors=True)
    (staging / inner).replace(destination)
    shutil.rmtree(staging)


def install_jdk(system_machine: tuple[str, str]) -> None:
    if java_binary(TOOLCHAIN_JDK).is_file():
        print(f"JDK 已安装：{TOOLCHAIN_JDK}", flush=True)
        return
    name, digest = JDK_ARCHIVES[system_machine]
    unpack(download(TEMURIN + name, digest), JDK_FOLDER, TOOLCHAIN / JDK_FOLDER)


def command_line_tool(sdk: Path, name: str) -> Path:
    return sdk / "cmdline-tools" / "latest" / "bin" / name


def install_command_line_tools(system_machine: tuple[str, str]) -> None:
    if command_line_tool(TOOLCHAIN_SDK, f"android{EXE}").is_file():
        print(f"Android 命令行工具已安装：{TOOLCHAIN_SDK / 'cmdline-tools' / 'latest'}", flush=True)
        return
    name, digest = COMMAND_LINE_TOOLS[system_machine]
    unpack(download(REPOSITORY + name, digest), "cmdline-tools", TOOLCHAIN_SDK / "cmdline-tools" / "latest")


def install_packages(env: dict, sdk: Path) -> None:
    # A package's folder is its path with ';' as the separator, and holds source.properties once installed.
    missing = [package for package in sdk_packages()
               if not (sdk.joinpath(*package.split(";")) / "source.properties").is_file()]
    for package in sdk_packages():
        print(f"{'安装' if package in missing else '已安装'} {package}", flush=True)
    if not missing:
        return
    android = command_line_tool(sdk, f"android{EXE}")
    sdkmanager = command_line_tool(sdk, "sdkmanager")
    if android.is_file():
        # Command-line tools 23 replaced sdkmanager with the Android CLI, which accepts the SDK license itself.
        # It keeps its own files in ANDROID_USER_HOME.
        subprocess.run([str(android), "--no-metrics", f"--sdk={sdk}", "sdk", "install",
                        *[package.replace(";", "/") for package in missing]], env=env, check=True)
    elif sdkmanager.is_file() and not WINDOWS:  # an older SDK outside the repository
        subprocess.run([str(sdkmanager), f"--sdk_root={sdk}", "--licenses"], input="y\n" * 50, text=True, env=env,
                       check=True, stdout=subprocess.DEVNULL)
        subprocess.run([str(sdkmanager), f"--sdk_root={sdk}", "--install", *missing], env=env, check=True)
    else:
        sys.exit(f"未找到 {android}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--in-repo", action="store_true",
                        help="macOS/Linux: install the in-repository toolchain (always the case on Windows)")
    args = parser.parse_args()

    if WINDOWS or args.in_repo:
        system_machine = host()
        print(f"仓库内工具链：{TOOLCHAIN}", flush=True)
        install_jdk(system_machine)
        install_command_line_tools(system_machine)
        if not android_env.uses_toolchain():
            sys.exit(f"JDK 没有装到 {TOOLCHAIN_JDK}")
        env = android_env.android_environment(check=False)
    else:
        env = android_env.android_environment()
    install_packages(env, Path(env["ANDROID_HOME"]))
    android_env.print_environment(env)


if __name__ == "__main__":
    main()
