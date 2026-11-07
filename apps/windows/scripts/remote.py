#!/usr/bin/env python3
"""Run Windows builds for the current branch on g16win, in a worktree of its own (run on the Mac).

    python3 apps/windows/scripts/remote.py sync                    # send the working tree
    python3 apps/windows/scripts/remote.py run -- python scripts/build.py
    python3 apps/windows/scripts/remote.py app                     # build_app.py
    python3 apps/windows/scripts/remote.py screenshot [--convert] [--dist|--single [--shortcut plain|app-id]]
    python3 apps/windows/scripts/remote.py list | remove

    python3 apps/windows/scripts/remote.py android-setup           # setup_toolchain.py (shared by all worktrees)
    python3 apps/windows/scripts/remote.py android-build [-- <build.py options>]
    python3 apps/windows/scripts/remote.py android-install         # device.py install on the phone at g16win
    python3 apps/windows/scripts/remote.py android-screenshot <name>
    python3 apps/windows/scripts/remote.py android-device -- <device.py command>

Each branch gets D:\\project\\musicunlock\\.worktrees\\<branch>, so parallel sessions never share a
checkout, a build folder or (with screenshot.py's per-checkout names) a render desktop. Syncing commits
a snapshot of the whole working tree (tracked and untracked files, minus .gitignore) through a temporary
index, pushes it to refs/heads/wt/<branch> and checks it out there; your index and branches are left
alone. Commands run in <worktree>/apps/windows with NuGet packages in a store shared by all worktrees;
Android commands run at the worktree root and pass the branch name on, since the worktree is detached and
device.py names the installed copy after the branch.
"""
from pathlib import Path
import argparse
import os
import re
import shlex
import shutil
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[3]
WINDOWS = ROOT / "apps" / "windows"


def git(*args: str, env: dict | None = None) -> str:
    return subprocess.run(["git", "-C", str(ROOT), *args], check=True, capture_output=True, text=True,
                          env=env).stdout.strip()


def remote_location(remote: str) -> tuple[str, str]:
    """g16win:/d/project/musicunlock -> ("g16win", "/d/project/musicunlock")."""
    url = git("remote", "get-url", remote)
    host, _, path = url.partition(":")
    if not path.startswith("/"):
        sys.exit(f"不支持的远程地址：{url}（需要 host:/路径 形式）")
    return host, path.rstrip("/")


def windows_path(posix: str) -> str:
    """/d/project/x -> D:\\project\\x (Git Bash paths)."""
    match = re.match(r"^/([a-zA-Z])(/.*)?$", posix)
    if not match:
        sys.exit(f"无法转换为 Windows 路径：{posix}")
    return match.group(1).upper() + ":" + (match.group(2) or "/").replace("/", "\\")


class Remote:
    def __init__(self, remote: str, name: str, branch: str):
        self.remote = remote
        self.branch = branch
        self.host, self.repo = remote_location(remote)
        self.name = name
        self.ref = f"wt/{name}"
        self.worktree = f"{self.repo}/.worktrees/{name}"
        self.store = windows_path(f"{self.repo}/.worktrees/.packages")

    def ssh(self, script: str, check: bool = True, **kwargs) -> subprocess.CompletedProcess:
        return subprocess.run(["ssh", "-o", "ServerAliveInterval=15", self.host, script], check=check, **kwargs)

    def snapshot(self) -> str:
        """A commit of the working tree on top of HEAD, built in a throwaway index."""
        with tempfile.TemporaryDirectory() as folder:
            env = dict(os.environ, GIT_INDEX_FILE=str(Path(folder) / "index"))
            git("read-tree", "HEAD", env=env)
            git("add", "-A", env=env)
            tree = git("write-tree", env=env)
        return git("commit-tree", tree, "-p", "HEAD", "-m", f"remote.py snapshot of {self.name}")

    def sync(self) -> None:
        commit = self.snapshot()
        subprocess.run(["git", "-C", str(ROOT), "push", "-q", "-f", self.remote, f"{commit}:refs/heads/{self.ref}"],
                       check=True)
        wt = shlex.quote(self.worktree)
        self.ssh(f"""set -e
cd {shlex.quote(self.repo)}
grep -qx '.worktrees/' .git/info/exclude 2>/dev/null || echo '.worktrees/' >> .git/info/exclude
if [ -d {wt} ]; then
  git -C {wt} checkout -q --detach -f {self.ref}
  git -C {wt} clean -q -fd
else
  git worktree add -q --detach {wt} {self.ref}
fi
mkdir -p {wt}/apps/windows/build/tmp
echo "synced {commit[:10]} to {self.worktree}"
""")

    def run(self, command: list[str]) -> int:
        script = (f"cd {shlex.quote(self.worktree)}/apps/windows && "
                  f"export MUSICUNLOCK_PACKAGES_DIR={shlex.quote(self.store)} PYTHONUTF8=1 && "
                  + shlex.join(command))
        return self.ssh(script, check=False).returncode

    def run_android(self, script: str, *args: str) -> int:
        """Run apps/android/scripts/<script> with args from the worktree root."""
        command = (f"cd {shlex.quote(self.worktree)} && "
                   f"export MUSICUNLOCK_ANDROID_BRANCH={shlex.quote(self.branch)} PYTHONUTF8=1 && "
                   + shlex.join(["python", f"apps/android/scripts/{script}", *args]))
        return self.ssh(command, check=False).returncode

    def fetch(self, path: str, local: Path) -> Path:
        """Copy a file from the worktree (path relative to its root)."""
        data = self.ssh(f"cat {shlex.quote(self.worktree + '/' + path)}", capture_output=True).stdout
        local.parent.mkdir(parents=True, exist_ok=True)
        local.write_bytes(data)
        return local

    def fetch_previews(self) -> Path:
        local = WINDOWS / "build" / "previews"
        shutil.rmtree(local, ignore_errors=True)
        local.parent.mkdir(parents=True, exist_ok=True)
        archive = self.ssh(f"cd {shlex.quote(self.worktree)}/apps/windows/build && tar -cf - previews",
                           capture_output=True).stdout
        subprocess.run(["tar", "-xf", "-", "-C", str(local.parent)], input=archive, check=True)
        return local

    def remove(self) -> None:
        wt = shlex.quote(self.worktree)
        # Android builds leave paths longer than 260 characters, which git deletes only with core.longpaths.
        self.ssh(f"cd {shlex.quote(self.repo)} && git -c core.longpaths=true worktree remove --force {wt}; "
                 f"git branch -q -D {self.ref} 2>/dev/null; git worktree prune; true")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--remote", default="g16win", help="git remote of the Windows machine")
    parser.add_argument("--name", help="worktree name (default: current branch)")
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("sync", help="send the working tree")
    run = commands.add_parser("run", help="sync, then run a command in apps/windows")
    run.add_argument("argv", nargs=argparse.REMAINDER)
    commands.add_parser("build", help="sync, then scripts/build.py (core, tests, CLI smoke test)")
    commands.add_parser("app", help="sync, then scripts/build_app.py")
    shot = commands.add_parser("screenshot", help="sync, build the app, render previews and fetch them")
    shot.add_argument("--convert", action="store_true", help="also convert sample files end to end")
    shape = shot.add_mutually_exclusive_group()
    shape.add_argument("--dist", action="store_true", help="stage build/dist/MusicUnlock and render that instead")
    shape.add_argument("--single", action="store_true", help="pack build/dist/MusicUnlock.exe and render through it")
    shot.add_argument("--shortcut", choices=["plain", "app-id"],
                      help="with --single: start it from a shortcut without or with the app's AppUserModelID")
    commands.add_parser("android-setup", help="sync, then install the Android toolchain into the repository")
    android_build = commands.add_parser("android-build", help="sync, then apps/android/scripts/build.py")
    android_build.add_argument("argv", nargs=argparse.REMAINDER)
    commands.add_parser("android-install", help="sync, then install this branch's debug build on the phone")
    android_shot = commands.add_parser("android-screenshot", help="screenshot the phone into build/screenshots/")
    android_shot.add_argument("shot", metavar="name")  # not "name": that is the worktree option
    android_device = commands.add_parser("android-device", help="sync, then run a device.py command")
    android_device.add_argument("argv", nargs=argparse.REMAINDER)
    commands.add_parser("list", help="list worktrees on the Windows machine")
    commands.add_parser("remove", help="delete this branch's worktree")
    args = parser.parse_args()

    branch = args.name or git("rev-parse", "--abbrev-ref", "HEAD")
    if branch == "HEAD":
        sys.exit("当前不在分支上，请用 --name 指定工作树名称")
    remote = Remote(args.remote, re.sub(r"[^A-Za-z0-9._-]+", "-", branch).strip("-"), branch)

    if args.command == "list":
        remote.ssh(f"git -C {shlex.quote(remote.repo)} worktree list")
        return
    if args.command == "remove":
        remote.remove()
        return
    remote.sync()
    if args.command == "sync":
        return
    if args.command.startswith("android-"):
        extra = getattr(args, "argv", [])
        extra = extra[1:] if extra[:1] == ["--"] else extra
        if args.command == "android-setup":
            sys.exit(remote.run_android("setup_toolchain.py"))
        if args.command == "android-build":
            sys.exit(remote.run_android("build.py", *extra))
        if args.command == "android-install":
            sys.exit(remote.run_android("device.py", "install"))
        if args.command == "android-device":
            if not extra:
                sys.exit("请在 -- 后面给出 device.py 的命令")
            sys.exit(remote.run_android("device.py", *extra))
        name = args.shot.removesuffix(".png")
        code = remote.run_android("device.py", "screenshot", name)
        if code == 0:
            print(f"截图已取回：{remote.fetch(f'build/screenshots/{name}.png', ROOT / 'build' / 'screenshots' / f'{name}.png')}")
        sys.exit(code)
    if args.command == "run":
        command = args.argv[1:] if args.argv[:1] == ["--"] else args.argv
        if not command:
            sys.exit("请在 -- 后面给出要运行的命令")
        sys.exit(remote.run(command))
    if args.command == "build":
        sys.exit(remote.run(["python", "scripts/build.py"]))
    if args.command == "app":
        sys.exit(remote.run(["python", "scripts/build_app.py"]))
    if args.command == "screenshot":
        code = remote.run(["python", "scripts/build_app.py"])
        if code == 0 and args.dist:
            code = remote.run(["python", "scripts/stage_app.py", "--zip"])
        if code == 0 and args.single:
            code = remote.run(["python", "scripts/build.py", "--no-test"])  # the launcher
        if code == 0 and args.single:
            code = remote.run(["python", "scripts/pack.py"])
        if code == 0:
            code = remote.run(["python", "scripts/screenshot.py", *(["--convert"] if args.convert else []),
                               *(["--dist"] if args.dist else []), *(["--single"] if args.single else []),
                               *(["--shortcut", args.shortcut] if args.shortcut else [])])
        if code == 0:
            print(f"预览已取回：{remote.fetch_previews()}")
        sys.exit(code)


if __name__ == "__main__":
    main()
