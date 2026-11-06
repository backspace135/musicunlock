#!/usr/bin/env python3
"""Smoke-test a built musicunlock CLI: usage exit code, folder import, non-ASCII names,
no overwriting, and per-file failures. Works under build/tmp only.

    python scripts/smoke_cli.py build/windows-release/musicunlock.exe
"""
from pathlib import Path
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parent.parent


def wave() -> bytes:
    samples = bytes(160)
    fmt = struct.pack("<4sIHHIIHH", b"fmt ", 16, 1, 1, 8000, 16000, 2, 16)
    body = b"WAVE" + fmt + b"data" + struct.pack("<I", len(samples)) + samples
    return b"RIFF" + struct.pack("<I", len(body)) + body


def run(cli: Path, *args: Path) -> subprocess.CompletedProcess:
    return subprocess.run([str(cli), *map(str, args)], capture_output=True)


def check(condition: bool, message: str, result: subprocess.CompletedProcess | None = None) -> None:
    if condition:
        return
    detail = ""
    if result is not None:
        detail = f"\n  exit {result.returncode}\n  stdout: {result.stdout.decode('utf-8', 'replace')}" \
                 f"\n  stderr: {result.stderr.decode('utf-8', 'replace')}"
    sys.exit(f"CLI smoke test failed: {message}{detail}")


def main() -> None:
    cli = Path(sys.argv[1]).resolve()
    work = ROOT / "build" / "tmp" / "cli-smoke"
    shutil.rmtree(work, ignore_errors=True)
    (work / "in" / "子目录").mkdir(parents=True)
    (work / "in" / ".hidden").mkdir()
    audio = wave()
    encrypted = bytes(b ^ 0xA3 for b in audio)  # NetEase cache (.uc)
    (work / "in" / "子目录" / "中文 歌曲.uc").write_bytes(encrypted)
    (work / "in" / ".hidden" / "skip.uc").write_bytes(encrypted)
    (work / "in" / "坏文件.ncm").write_bytes(b"CTENFDAM")
    (work / "in" / "notes.txt").write_text("ignored")

    usage = run(cli)
    check(usage.returncode == 2 and "用法".encode() in usage.stdout, "usage without arguments", usage)

    out = work / "输出"
    first = run(cli, out, work / "in")
    check(first.returncode == 1, "one bad file makes the exit code 1", first)
    check((out / "中文 歌曲.wav").read_bytes() == audio, "decoded audio matches")
    check("坏文件.ncm".encode() in first.stderr, "failure names the bad file", first)
    check(not (out / "skip.wav").exists(), "hidden folders are skipped")

    second = run(cli, out, work / "in" / "子目录" / "中文 歌曲.uc")
    check(second.returncode == 0, "single file converts", second)
    check((out / "中文 歌曲 (1).wav").read_bytes() == audio, "existing output is not overwritten")
    check((out / "中文 歌曲.wav").read_bytes() == audio, "first output kept")

    missing = run(cli, out, work / "in" / "notes.txt")
    check(missing.returncode == 1, "unsupported file fails", missing)
    shutil.rmtree(work)
    print("CLI smoke test passed")


if __name__ == "__main__":
    main()
