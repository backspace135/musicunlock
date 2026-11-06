#!/usr/bin/env python3
"""Smoke-test the single-exe launcher end to end with a stand-in app (Windows only).

    python scripts/smoke_launcher.py build/windows-release

Packs a small folder whose MusicUnlock.exe is launcher_probe.exe (it records its path, working folder, the
launcher path it was given and its arguments), copies only the packed exe into an empty folder and runs it: first
start, warm start, arguments and the launcher path passed on, repair after tampering, concurrent starts, removal
of old versions unless in use, and the error messages. MUSICUNLOCK_RUNTIME_DIR keeps every extraction under
build/tmp; nothing goes to %LOCALAPPDATA%.
"""
from pathlib import Path
import os
import re
import shutil
import subprocess
import sys
import time

ROOT = Path(__file__).resolve().parent.parent
WORK = ROOT / "build" / "tmp" / "launcher-smoke"
PROBE_EXIT = 7


def check(condition: bool, message: str, detail: str = "") -> None:
    if not condition:
        sys.exit(f"launcher smoke test failed: {message}\n{detail}")


class Packed:
    """A packed single exe, alone in its own folder."""

    def __init__(self, tools: Path, name: str, files: dict[str, bytes]):
        self.files = files
        source = WORK / f"{name}-app"
        shutil.rmtree(source, ignore_errors=True)
        for relative, data in files.items():
            (source / relative).parent.mkdir(parents=True, exist_ok=True)
            (source / relative).write_bytes(data)
        shutil.copy2(tools / "launcher_probe.exe", source / "MusicUnlock.exe")
        self.files["MusicUnlock.exe"] = (source / "MusicUnlock.exe").read_bytes()
        folder = WORK / f"{name}-clean"
        shutil.rmtree(folder, ignore_errors=True)
        folder.mkdir(parents=True)
        self.exe = folder / "MusicUnlock.exe"
        subprocess.run([str(tools / "launcher_pack.exe"), str(source), str(tools / "launcher.exe"), str(self.exe),
                        "--algorithm", "lzms", "--chunk-mb", "1"], check=True, capture_output=True)
        check(sorted(p.name for p in folder.iterdir()) == ["MusicUnlock.exe"], "the exe is not alone")


class Run:
    def __init__(self, exe: Path, root: Path, tag: str, args: list[str] = (), hold_ms: int = 0, wait: bool = True):
        self.log = WORK / f"{tag}.log"
        self.probe = WORK / f"{tag}.probe"
        for path in (self.log, self.probe):
            path.unlink(missing_ok=True)
        # A stale MUSICUNLOCK_LAUNCHER_PATH from the environment must be replaced by the launcher's own path.
        env = dict(os.environ, MUSICUNLOCK_RUNTIME_DIR=str(root), MUSICUNLOCK_LAUNCHER_LOG=str(self.log),
                   MUSICUNLOCK_PROBE_OUT=str(self.probe), MUSICUNLOCK_PROBE_HOLD_MS=str(hold_ms),
                   MUSICUNLOCK_LAUNCHER_PATH=r"C:\stale\MusicUnlock.exe")
        self.process = subprocess.Popen([str(exe), *args], env=env, cwd=WORK)
        if wait:
            self.wait()

    def wait(self) -> "Run":
        self.code = self.process.wait(timeout=120)
        self.text = self.log.read_text(encoding="utf-8-sig") if self.log.exists() else ""
        return self

    def started(self) -> None:
        deadline = time.time() + 30
        while not self.probe.exists():
            check(time.time() < deadline and self.process.poll() is None, "the app did not start")
            time.sleep(0.05)

    def lines(self) -> list[str]:
        return self.probe.read_text(encoding="utf-8-sig").splitlines()

    def expect(self, action: str, changed: int | None = None) -> None:
        check(self.code == PROBE_EXIT, f"exit code {self.code}", self.text)
        match = re.search(r"prepare (\w+) .*?: (\d+) files changed", self.text)
        check(bool(match) and match.group(1) == action, f"expected {action}", self.text)
        if changed is not None:
            check(int(match.group(2)) == changed, f"expected {changed} changed files", self.text)

    def launch_ms(self) -> float:
        return float(re.search(r"\[\s*([\d.]+) ms\] started", self.text).group(1))


def same_files(folder: Path, files: dict[str, bytes]) -> bool:
    found = {p.relative_to(folder).as_posix(): p.read_bytes() for p in folder.rglob("*")
             if p.is_file() and p.name != ".musicunlock-stamp"}
    return found == files


def main() -> None:
    if os.name != "nt" or len(sys.argv) != 2:
        sys.exit(__doc__)
    tools = Path(sys.argv[1]).resolve()
    shutil.rmtree(WORK, ignore_errors=True)
    WORK.mkdir(parents=True)
    files = {"Microsoft.UI.Xaml.dll": os.urandom(1 << 20) + bytes(3 << 20), "zh-CN/资源 文件.mui": b"mui" * 1000,
             "empty.txt": b"", "kgm.mask": bytes(range(256)) * 512}
    v1 = Packed(tools, "v1", dict(files))
    root = WORK / "runtime"

    # First start extracts, a second start reuses; arguments arrive unchanged.
    args = [r"D:\音乐\歌曲 1.ncm", "-flag", 'quote "inside"', "trailing\\", "", r"C:\a b\c\\"]
    first = Run(v1.exe, root, "first", args)
    first.expect("extracted")
    lines = first.lines()
    versions = [p for p in root.iterdir()]
    check(len(versions) == 1 and re.fullmatch(r"[0-9a-f]{16}", versions[0].name), "one version folder",
          str(versions))
    folder = versions[0]
    check(Path(lines[0]) == folder / "MusicUnlock.exe", "started the extracted app", lines[0])
    check(Path(lines[1]) == WORK, "kept the working folder", lines[1])
    # The app pins and relaunches the single exe, not the extracted copy (app/Taskbar.cpp).
    check(Path(lines[2]) == v1.exe, "told the app the single exe's path", lines[2])
    check(lines[3:] == args, "arguments passed through", repr(lines[3:]))
    check(same_files(folder, v1.files), "extracted files match the app folder")
    warm = Run(v1.exe, root, "warm")
    warm.expect("reused")
    check(warm.lines()[3:] == [], "no arguments")
    print(f"first start {first.launch_ms():.0f} ms, warm start {warm.launch_ms():.0f} ms (stand-in app)")

    # Tampered, deleted and planted files are repaired on the next start.
    dll = folder / "Microsoft.UI.Xaml.dll"
    data = bytearray(dll.read_bytes())
    data[100] ^= 0xFF
    dll.write_bytes(data)
    (folder / "zh-CN" / "资源 文件.mui").unlink()
    (folder / "version.dll").write_bytes(b"planted")
    Run(v1.exe, root, "repair").expect("repaired", 3)
    check(same_files(folder, v1.files), "repaired files match the app folder")
    Run(v1.exe, root, "after-repair").expect("reused")

    # Concurrent first starts: one extracts, the others wait and reuse it.
    crowd_root = WORK / "crowd"
    crowd = [Run(v1.exe, crowd_root, f"crowd{i}", wait=False) for i in range(6)]
    actions = sorted(re.search(r"prepare (\w+)", run.wait().text).group(1) for run in crowd)
    check(all(run.code == PROBE_EXIT for run in crowd), "all concurrent starts ran the app")
    check(actions == ["extracted"] + ["reused"] * 5, "exactly one extraction", repr(actions))
    check([p.name for p in crowd_root.iterdir()] == [folder.name], "no temporary folders left")

    # A new version removes old folders, except one a running copy is using; other folders are left alone.
    (root / "notes").mkdir()
    (root / ".tmp-0123456789abcdef-1").mkdir()
    v2 = Packed(tools, "v2", dict(files, **{"kgm.mask": b"v2"}))
    running = Run(v1.exe, root, "running", hold_ms=4000, wait=False)
    running.started()
    newer = Run(v2.exe, root, "v2-first")
    newer.expect("extracted")
    check("cleanup: 0 removed, 1 in use" in newer.text, "kept the folder in use", newer.text)
    # Repairing the folder a running copy uses works as long as the file itself is not loaded.
    v2_folder = next(p for p in root.iterdir() if p.name not in (folder.name, "notes"))
    hold = Run(v2.exe, root, "v2-hold", hold_ms=3000, wait=False)
    hold.started()
    (v2_folder / "kgm.mask").write_bytes(b"yy")
    Run(v2.exe, root, "v2-repair-running").expect("repaired", 1)
    hold.wait()
    running.wait()
    check(running.code == PROBE_EXIT, "the old copy kept running", running.text)
    later = Run(v2.exe, root, "v2-later")
    later.expect("reused")
    check("cleanup: 1 removed, 0 in use" in later.text, "removed the old folder once closed", later.text)
    check(sorted(p.name for p in root.iterdir()) == sorted([v2_folder.name, "notes"]), "only v2 and notes left",
          str(list(root.iterdir())))
    check(same_files(v2_folder, v2.files), "v2 files match")

    # Errors: an unwritable runtime folder and a damaged exe exit with a message instead of starting.
    blocker = WORK / "blocker"
    blocker.write_bytes(b"x")
    blocked = Run(v1.exe, blocker / "runtime", "blocked")
    check(blocked.code == 1 and "无法写入" in blocked.text and not blocked.probe.exists(), "unwritable folder",
          blocked.text)
    damaged = WORK / "damaged" / "MusicUnlock.exe"
    damaged.parent.mkdir()
    image = bytearray(v1.exe.read_bytes())
    header = image.rfind(b"MUPAYLD\x01")  # the format constant also sits in .rdata
    chunks = int.from_bytes(image[header + 12:header + 16], "little")
    image[header + 64 + 8 * chunks + 20] ^= 1  # a hash byte in the manifest
    damaged.write_bytes(image)
    broken = Run(damaged, WORK / "damaged-runtime", "damaged")
    check(broken.code == 1 and "已损坏" in broken.text and not broken.probe.exists(), "damaged exe", broken.text)
    print("launcher smoke test passed")


if __name__ == "__main__":
    main()
