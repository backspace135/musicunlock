#!/usr/bin/env python3
"""Bundle every platform's newest release into one GitHub release marked Latest.

GitHub marks only one release as Latest, and the repository page shows only that one. Each platform is still
released under its own tag (v* for macOS, windows-v*, android-v*); after publishing one of those, run this script.
It takes the newest published, non-prerelease release of each platform, downloads the assets into
build/release/<tag>/, checks them against the digests GitHub recorded (and Android's .sha256), and writes the
release notes. With --publish it creates the combined release and marks it Latest.

    python3 scripts/release.py                       # dry run: download, verify and write notes.md
    python3 scripts/release.py --publish             # tag defaults to today's date in JST, e.g. 2026.11.07
    python3 scripts/release.py 2026.11.07.1 --publish
"""

import argparse
import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Suffixed tags such as android-v0.1.0-rc1 do not match, so test builds are never bundled.
PLATFORMS = [
    {
        "name": "macOS",
        "tag": re.compile(r"v(\d+)\.(\d+)\.(\d+)"),
        "requires": "macOS 26+，Apple Silicon",
        "note": "本地 ad-hoc 签名，没有经过 Apple 公证。第一次打开时在 Finder 里右键 App 选“打开”，"
        "或者解压后执行 `xattr -dr com.apple.quarantine 音匣.app`。",
    },
    {
        "name": "Windows",
        "tag": re.compile(r"windows-v(\d+)\.(\d+)\.(\d+)"),
        "requires": "Windows 10 1809+ / Windows 11，仅 x64",
        "note": "推荐单文件的 `MusicUnlock-Windows-x64.exe`，自包含，不需要另装 .NET 或 Windows App Runtime。"
        "程序没有代码签名，SmartScreen 提示时点“更多信息 → 仍要运行”。",
    },
    {
        "name": "Android",
        "tag": re.compile(r"android-v(\d+)\.(\d+)\.(\d+)"),
        "requires": "Android 8.0（API 26）+，仅 arm64-v8a",
        "note": "第一次安装时只给用来安装的浏览器或文件管理器打开「安装未知应用」；升级直接覆盖安装，不要先卸载。",
    },
]

COMBINED_TAG = re.compile(r"\d{4}\.\d{2}\.\d{2}(\.\d+)?")


def gh(*args):
    return subprocess.run(["gh", *args], check=True, capture_output=True, text=True, cwd=ROOT).stdout


def gh_json(*args):
    return json.loads(gh(*args))


def newest_release(platform, releases):
    candidates = []
    for release in releases:
        match = platform["tag"].fullmatch(release["tagName"])
        if match and not release["isDraft"] and not release["isPrerelease"]:
            candidates.append((tuple(int(part) for part in match.groups()), release["tagName"]))
    if not candidates:
        sys.exit(f"{platform['name']} 没有已发布的正式版")
    version, tag = max(candidates)
    return ".".join(map(str, version)), tag


def changes(body):
    """The "### 更新" section, or the introduction of a first release that has none."""
    lines = body.splitlines()
    for start, line in enumerate(lines):
        if line.strip() == "### 更新":
            section = []
            for line in lines[start + 1 :]:
                if line.startswith("## ") or line.startswith("### "):
                    break
                section.append(line)
            return "\n".join(section).strip()
    intro = []
    for line in lines:
        if line.startswith("### "):
            break
        if not line.startswith("## "):
            intro.append(line)
    return "\n".join(intro).strip()


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as file:
        for block in iter(lambda: file.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def download(release, out):
    names = [asset["name"] for asset in release["assets"]]
    taken = [name for name in names if (out / name).exists()]
    if taken:
        sys.exit(f"{release['tagName']} 的文件和其他平台重名：{', '.join(taken)}")
    gh("release", "download", release["tagName"], "--dir", str(out))
    for asset in release["assets"]:
        path = out / asset["name"]
        actual = sha256(path)
        expected = (asset.get("digest") or "").removeprefix("sha256:")
        if not expected:
            sys.exit(f"{asset['name']} 没有 GitHub 记录的 SHA-256")
        if actual != expected:
            sys.exit(f"{asset['name']} 的 SHA-256 是 {actual}，GitHub 记录的是 {expected}")
    for asset in release["assets"]:
        if asset["name"].endswith(".sha256"):
            expected, name = (out / asset["name"]).read_text().split()
            if sha256(out / name.lstrip("*")) != expected:
                sys.exit(f"{name} 和 {asset['name']} 对不上")
    return [name for name in names if not name.endswith(".sha256")]


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    today = datetime.datetime.now(datetime.timezone(datetime.timedelta(hours=9))).strftime("%Y.%m.%d")
    parser.add_argument("tag", nargs="?", default=today, help=f"combined release tag (default: {today})")
    parser.add_argument("--publish", action="store_true", help="create the release and mark it Latest")
    args = parser.parse_args()

    if not COMBINED_TAG.fullmatch(args.tag):
        sys.exit(f"tag 要写成 YYYY.MM.DD 或 YYYY.MM.DD.N：{args.tag}")
    releases = gh_json("release", "list", "--limit", "500", "--json", "tagName,isDraft,isPrerelease")
    if any(release["tagName"] == args.tag for release in releases):
        sys.exit(f"{args.tag} 已经有 Release，换一个 tag，例如 {args.tag}.1")

    out = ROOT / "build" / "release" / args.tag
    shutil.rmtree(out, ignore_errors=True)
    out.mkdir(parents=True)

    bundled = []
    for platform in PLATFORMS:
        version, tag = newest_release(platform, releases)
        release = gh_json("release", "view", tag, "--json", "tagName,url,body,assets")
        files = download(release, out)
        bundled.append((platform, version, release, files))
        print(f"{platform['name']:8} {version:8} {tag:18} {', '.join(files)}")

    installers = [name for _, _, _, files in bundled for name in files]
    sums = "".join(f"{sha256(out / name)}  {name}\n" for name in installers)
    (out / "SHA256SUMS.txt").write_text(sums)

    notes = [f"## 音匣 MusicUnlock {args.tag}", "", "三个平台的最新版本，各版本的完整说明见版本号链接。", ""]
    notes += ["| 平台 | 版本 | 文件 | 系统要求 |", "| --- | --- | --- | --- |"]
    for platform, version, release, files in bundled:
        listed = "<br>".join(f"`{name}`" for name in files)
        notes.append(f"| {platform['name']} | [{version}]({release['url']}) | {listed} | {platform['requires']} |")
    for platform, version, release, _ in bundled:
        notes += ["", f"### {platform['name']} {version}", "", changes(release["body"]), "", platform["note"]]
    notes += ["", "### SHA-256", "", "```", sums.rstrip(), "```", "", "校验：`sha256sum -c SHA256SUMS.txt --ignore-missing`"]
    (out / "notes.md").write_text("\n".join(notes) + "\n")
    print(f"\n文件和说明在 {out.relative_to(ROOT)}/")

    if not args.publish:
        print("没有发布；确认 notes.md 后加 --publish")
        return
    assets = [str(out / name) for name in sorted(path.name for path in out.iterdir() if path.name != "notes.md")]
    gh(
        "release", "create", args.tag, *assets,
        "--title", f"音匣 MusicUnlock {args.tag}",
        "--notes-file", str(out / "notes.md"),
        "--target", "main",
        "--latest",
    )
    if gh("api", "repos/{owner}/{repo}/releases/latest", "--jq", ".tag_name").strip() != args.tag:
        sys.exit(f"{args.tag} 已创建，但没有被标成 Latest")
    print(gh_json("release", "view", args.tag, "--json", "url")["url"])


if __name__ == "__main__":
    main()
