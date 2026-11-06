#!/usr/bin/env python3
"""Build the single-file build/dist/MusicUnlock.exe (Windows only).

    python scripts/pack.py                   # stage build/dist/MusicUnlock, then embed it into the launcher
    python scripts/pack.py --input <folder>  # embed another app folder as is
    python scripts/pack.py --compare         # size and decompression time per algorithm and chunk size

Runs stage_app.py first, so the exe always holds the current slim folder. Needs scripts/build.py (launcher and
launcher_pack) and scripts/build_app.py. The launcher carries the app's icon and version information.
"""
from pathlib import Path
import argparse
import os
import subprocess
import sys

from build import BUILD, ROOT

OUTPUT = BUILD / "dist" / "MusicUnlock.exe"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--input", type=Path, help="app folder to embed as is (default: stage build/dist/MusicUnlock)")
    parser.add_argument("--output", type=Path, default=OUTPUT)
    # LZMS in 4 MB chunks: a quarter smaller than XPRESS Huffman, decompressed in parallel in a few hundred ms.
    parser.add_argument("--algorithm", default="lzms", choices=["lzms", "xpress_huff"])
    parser.add_argument("--chunk-mb", type=int, default=4)
    parser.add_argument("--compare", action="store_true", help="compare algorithms and chunk sizes instead")
    args = parser.parse_args()
    if os.name != "nt":
        sys.exit("只能在 Windows 上打包")

    tools = BUILD / "windows-release"
    stub, packer = tools / "launcher.exe", tools / "launcher_pack.exe"
    if not stub.exists() or not packer.exists():
        sys.exit("请先运行 scripts/build.py")
    folder = args.input
    if folder is None:
        subprocess.run([sys.executable, str(ROOT / "scripts" / "stage_app.py")], check=True)
        folder = BUILD / "dist" / "MusicUnlock"
    if not (folder / "MusicUnlock.exe").exists():
        sys.exit(f"{folder} 里没有 MusicUnlock.exe")
    if args.compare:
        subprocess.run([str(packer), "--compare", str(folder)], check=True)
        return
    subprocess.run([str(packer), str(folder), str(stub), str(args.output), "--algorithm", args.algorithm,
                    "--chunk-mb", str(args.chunk_mb)], check=True)


if __name__ == "__main__":
    main()
