#!/usr/bin/env python3
"""Stage the built WinUI app as a slim, self-contained folder for distribution.

    python scripts/stage_app.py           # build/app-release -> build/dist/MusicUnlock
    python scripts/stage_app.py --zip     # also write build/dist/MusicUnlock.zip and report its size

Everything in the build output is copied except what the app never loads:
- WinUI's language resources (<culture>/*.mui) other than LANGUAGES;
- development files (DEV_EXTENSIONS): import libraries, linker leftovers, and .winmd metadata, which
  C++/WinRT and the XAML compiler only read at build time;
- .pdb symbols, which go to build/dist/symbols for crash analysis instead.
So new runtime files (for example WebView2Loader.dll) are picked up without changes here. If a file of a
dropped class turns out to be needed at run time, add its name to KEEP. REQUIRED is checked afterwards.
Runs anywhere (Python only); the app itself is Windows only.
"""
from pathlib import Path
import argparse
import re
import shutil
import sys
import zipfile

from build import BUILD

# WinUI ships Microsoft.ui.xaml.dll.mui for ~90 cultures; the app's UI is Chinese, en-US is the fallback.
LANGUAGES = {"zh-cn", "zh-tw", "en-us"}
CULTURE = re.compile(r"^[a-z]{2,3}(-[a-z0-9]{2,8})+$", re.IGNORECASE)
DEV_EXTENSIONS = {".winmd", ".lib", ".exp", ".ilk", ".iobj", ".ipdb", ".map"}
SYMBOLS = {".pdb"}
# File names kept even though their extension is dropped above (lower case).
KEEP: set[str] = set()
# The app does not start, or loses a feature, without these.
REQUIRED = ["MusicUnlock.exe", "MusicUnlock.pri", "Microsoft.ui.xaml.dll", "Microsoft.WindowsAppRuntime.dll",
            "kgm.mask", "LICENSE", "THIRD_PARTY_NOTICES.txt"]


def classify(relative: Path) -> str:
    """'app', 'symbols' or the reason the file is left out."""
    if relative.name.lower() in KEEP:
        return "app"
    if len(relative.parts) > 1 and CULTURE.match(relative.parts[0]) and relative.parts[0].lower() not in LANGUAGES:
        return "language"
    suffix = relative.suffix.lower()
    if suffix in SYMBOLS:
        return "symbols"
    if suffix in DEV_EXTENSIONS:
        return "development"
    return "app"


def size(path: Path) -> int:
    return path.stat().st_size if path.is_file() else sum(f.stat().st_size for f in path.rglob("*") if f.is_file())


def mb(value: int) -> str:
    return f"{value / 1e6:.1f} MB"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--config", default="Release", choices=["Release", "Debug"])
    parser.add_argument("--zip", action="store_true", help="also write build/dist/MusicUnlock.zip")
    args = parser.parse_args()

    source = BUILD / f"app-{args.config.lower()}"
    if not (source / "MusicUnlock.exe").exists():
        sys.exit(f"未找到 {source / 'MusicUnlock.exe'}，请先运行 scripts/build_app.py")
    dist = BUILD / "dist"
    target, symbols = dist / "MusicUnlock", dist / "symbols"
    for folder in (target, symbols):
        shutil.rmtree(folder, ignore_errors=True)
    (dist / "MusicUnlock.zip").unlink(missing_ok=True)

    dropped: dict[str, int] = {}
    for file in sorted(source.rglob("*")):
        if not file.is_file():
            continue
        relative = file.relative_to(source)
        kind = classify(relative)
        if kind in ("app", "symbols"):
            destination = (target if kind == "app" else symbols) / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(file, destination)
        if kind != "app":
            dropped[kind] = dropped.get(kind, 0) + file.stat().st_size

    missing = [name for name in REQUIRED if not (target / name).exists()]
    if missing:
        sys.exit(f"分发目录缺少必需文件：{', '.join(missing)}")
    print(f"构建输出 {mb(size(source))}，分发目录 {mb(size(target))}：{target}")
    for kind, total in sorted(dropped.items()):
        print(f"  未复制 {kind}: {mb(total)}")
    if symbols.exists():
        print(f"符号文件 {mb(size(symbols))}：{symbols}")
    if args.zip:
        archive = dist / "MusicUnlock.zip"
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as output:
            for file in sorted(target.rglob("*")):
                if file.is_file():
                    output.write(file, Path("MusicUnlock") / file.relative_to(target))
        print(f"zip 压缩后 {mb(size(archive))}：{archive}")


if __name__ == "__main__":
    main()
