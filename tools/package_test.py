# SPDX-License-Identifier: GPL-3.0-or-later
"""Package a release ZIP (e.g. 0.1-Alpha): source, the pinned lifter and setup tools, no game data.

--platform macOS or Linux packages the same source as a .tar.gz, keeping the scripts executable."""
import argparse
import io
import os
from pathlib import Path
import subprocess
import tarfile
import tempfile
import time
import zipfile

ROOT = Path(__file__).resolve().parents[1]
# Agent notes and dev-only submodule metadata stay out of the tester package.
SUBMODULES = ("tools/xboxrecomp", "third_party/smaa")
EXCLUDE = {"AGENTS.md", ".gitmodules", *SUBMODULES}
# The build reads only SMAA.hlsl and Textures; the demo is 48 MB of media.
SKIP = "third_party/smaa/Demo/"
# extract-xiso.exe and its LICENSE.TXT, copied from the DOAXBV alpha package.
ARTIFACTS = ROOT / "private" / "release-assets"
# The C# compiler every Windows ships builds both exes from tools/launcher.cs.
CSC = Path(os.environ.get("WINDIR", r"C:\Windows")) / "Microsoft.NET" / "Framework64" / "v4.0.30319" / "csc.exe"
LAUNCHERS = {"Launcher.exe": [], "DOA3.exe": ["/define:PLAY_ONLY"]}


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd)


def archive(cwd, prefix):
    with tarfile.open(fileobj=io.BytesIO(git("archive", "--format=tar", "HEAD", cwd=cwd))) as tar:
        for member in tar.getmembers():
            if member.isfile() and member.name not in EXCLUDE:
                yield prefix + member.name, tar.extractfile(member).read(), member.mode


def build_launchers(out_dir):
    """Launcher.exe (build, settings, play) and DOA3.exe (play) into out_dir."""
    if not CSC.is_file():
        raise SystemExit(f"The launchers need Windows: {CSC} is missing")
    out_dir.mkdir(parents=True, exist_ok=True)
    for exe, flags in LAUNCHERS.items():
        subprocess.run([str(CSC), "/nologo", "/warnaserror", "/target:winexe", "/r:System.Web.Extensions.dll",
                        f"/resource:{ROOT / 'tools' / 'launcher-background.jpg'},launcher-background.jpg",
                        *flags, f"/out:{out_dir / exe}", str(ROOT / "tools" / "launcher.cs")], check=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("version", nargs="?")
    parser.add_argument("--platform", choices=("Windows", "macOS", "Linux"), default="Windows")
    parser.add_argument("--launchers", type=Path, metavar="DIR",
                        help="only build Launcher.exe and DOA3.exe into DIR (the repository root for a checkout)")
    args = parser.parse_args()
    if args.launchers:
        build_launchers(args.launchers)
        return
    if not args.version:
        parser.error("give a version, or --launchers DIR")
    version = args.version
    unix = args.platform != "Windows"
    if git("status", "--porcelain", "--untracked-files=no").strip():
        raise SystemExit("Commit first: the package is built from HEAD")
    for sub in SUBMODULES:
        pinned = git("ls-tree", "HEAD", sub).split()[2].decode()
        if git("rev-parse", "HEAD", cwd=ROOT / sub).decode().strip() != pinned or \
                git("status", "--porcelain", cwd=ROOT / sub).strip():
            raise SystemExit(f"{sub} must be a clean checkout of {pinned}")

    name = f"DOA3-{version}"
    out = ROOT / "private" / "release" / (f"{name}-{args.platform}.tar.gz" if unix else f"{name}.zip")
    if out.exists():
        raise SystemExit(f"{out} already exists")
    # macOS and Linux build extract-xiso themselves (docs/building.md).
    artifacts = () if unix else ("extract-xiso.exe", "LICENSE.TXT")
    missing = [a for a in artifacts if not (ARTIFACTS / a).is_file()]
    if missing:
        raise SystemExit(f"Missing from {ARTIFACTS}: {', '.join(missing)}")
    files = list(archive(ROOT, f"{name}/"))
    for sub in SUBMODULES:
        files += [f for f in archive(ROOT / sub, f"{name}/{sub}/") if not f[0].startswith(f"{name}/{SKIP}")]
    files += [(f"{name}/tools/artifacts/{a}", (ARTIFACTS / a).read_bytes(), 0o644) for a in artifacts]
    if not unix:
        with tempfile.TemporaryDirectory() as launchers:
            build_launchers(Path(launchers))
            files += [(f"{name}/{exe}", (Path(launchers) / exe).read_bytes(), 0o755) for exe in LAUNCHERS]
    readme = ROOT / "docs" / ("building.md" if unix else "test-release.md")
    files.append((f"{name}/README.txt", readme.read_bytes(), 0o644))
    out.parent.mkdir(parents=True, exist_ok=True)
    partial = out.with_name(out.name + ".tmp")
    if unix:
        with tarfile.open(partial, "w:gz") as package:
            for path, data, mode in files:
                info = tarfile.TarInfo(path)
                info.size, info.mode, info.mtime = len(data), mode, int(time.time())
                package.addfile(info, io.BytesIO(data))
    else:
        with zipfile.ZipFile(partial, "w", zipfile.ZIP_DEFLATED) as package:
            for path, data, _ in files:
                package.writestr(path, data)
    partial.rename(out)
    print(out)


if __name__ == "__main__":
    main()
