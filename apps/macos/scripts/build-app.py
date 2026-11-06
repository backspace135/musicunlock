#!/usr/bin/env python3
"""Build a local SwiftUI .app using only Apple system frameworks."""
from pathlib import Path
import plistlib
import shutil
import subprocess

ROOT = Path(__file__).resolve().parent.parent
REPO = ROOT.parent.parent
BUILD = ROOT / "build"
APP = BUILD / "音匣.app"


def run(*args: str) -> str:
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def main() -> None:
    # Native build mode avoids case-insensitive target collisions in Xcode's package builder.
    subprocess.run(["swift", "build", "--build-system", "native", "-c", "release"], cwd=ROOT, check=True)
    binaries = Path(run("swift", "build", "--build-system", "native", "-c", "release", "--show-bin-path"))
    macos = APP / "Contents" / "MacOS"
    resources = APP / "Contents" / "Resources"
    macos.mkdir(parents=True, exist_ok=True)
    resources.mkdir(parents=True, exist_ok=True)
    shutil.copy2(binaries / "MusicUnlock", macos / "MusicUnlock")
    shutil.copy2(binaries / "MusicUnlockCLI", BUILD / "musicunlock")
    shutil.copy2(REPO / "LICENSE", resources)
    shutil.copy2(REPO / "THIRD_PARTY_NOTICES.txt", resources)
    shutil.copy2(REPO / "resources/kgm.mask", resources)
    # assets/AppIcon.svg is the source; AppIcon-1024.png is its rendered master.
    iconset = BUILD / "AppIcon.iconset"
    shutil.rmtree(iconset, ignore_errors=True)
    iconset.mkdir()
    for size in [16, 32, 128, 256, 512]:
        for scale in [1, 2]:
            name = f"icon_{size}x{size}{'@2x' if scale == 2 else ''}.png"
            run("sips", "-z", str(size * scale), str(size * scale), str(ROOT / "assets/AppIcon-1024.png"),
                "--out", str(iconset / name))
    run("iconutil", "--convert", "icns", "--output", str(resources / "AppIcon.icns"), str(iconset))
    extensions = ["ncm", "uc", "qmc0", "qmc2", "qmc3", "qmcflac", "qmcogg", "mflac", "mflac0", "mgg", "mgg0", "mgg1", "mggl", "tkm", "kwm", "kgm", "kgma", "vpr", "xm", "tm0", "tm2", "tm3", "tm6"]
    info = {
        "CFBundleDevelopmentRegion": "zh_CN",
        "CFBundleDisplayName": "音匣",
        "CFBundleName": "音匣",
        "CFBundleExecutable": "MusicUnlock",
        "CFBundleIdentifier": "app.musicunlock.native",
        "CFBundlePackageType": "APPL",
        "CFBundleShortVersionString": "0.2.1",
        "CFBundleVersion": "3",
        "CFBundleIconFile": "AppIcon",
        "LSMinimumSystemVersion": "26.0",
        "NSHighResolutionCapable": True,
        "NSPrincipalClass": "NSApplication",
        "NSHumanReadableCopyright": "© 2026 backspace135 and MusicUnlock contributors (MIT). Decoding portions © 2019–2021 MengYX; see third-party notices.",
        "CFBundleDocumentTypes": [{
            "CFBundleTypeName": "Encrypted audio",
            "CFBundleTypeRole": "Viewer",
            "LSHandlerRank": "Alternate",
            "CFBundleTypeExtensions": extensions,
        }],
    }
    with (APP / "Contents/Info.plist").open("wb") as file:
        plistlib.dump(info, file)
    run("codesign", "--force", "--sign", "-", str(APP))
    run("codesign", "--verify", "--deep", "--strict", str(APP))
    archive = BUILD / "MusicUnlock-macOS-arm64.zip"
    run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(APP), str(archive))
    print(f"App: {APP}")
    print(f"Archive: {archive}")
    print(f"CLI: {BUILD / 'musicunlock'}")


if __name__ == "__main__":
    main()
