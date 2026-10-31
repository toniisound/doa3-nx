# SPDX-License-Identifier: GPL-3.0-or-later
"""Build the DOA3 runner from a user-owned ISO or extracted disc folder."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import uuid

from extract_iso import extract, run_logged, sha256

ROOT = Path(__file__).resolve().parents[1]
LIFTER = ROOT / "tools" / "xboxrecomp"
CAPSTONE = "5.0.7"
# The discs the build accepts, by default.xbe SHA-256: Japan 3.1 is the main
# build, USA 3.0 is still accepted. "program" is the generated program the
# runtime was validated against; changing the lifter pin, the recipe's JSON or
# its forwards file changes it, so regenerate locally and update it with them.
# Each version keeps its own extraction and play folder (saves), by suffix.
VERSIONS = {
    "469cb086048020d04fbc45b05fd5c470b479f06013d77d736c0ea11df5de85df": dict(
        name="Japan 3.1", recipe="doa3-jp", forwards="program_forwards_jp.c", jp=True, suffix="-jp",
        program="e351bc456acef3ee5678849e7c7926d6659472bea9e6b006b91479c8607f60a5", ebp=0),
    "e523dd15d0765899a43a1824c023e0219d129e9a8b41e8213e4e645755d2bfa3": dict(
        name="USA 3.0", recipe="doa3", forwards="program_forwards.c", jp=False, suffix="",
        program="bcdbc15c611015b6fc0c50529a7e453cddf621290af4a5c73624e0f37cc5e9b9", ebp=0),
}
# A movie cut to its stream and audio header sectors holds no picture, so the
# game's player ends it at once. A missing movie file stalls the game instead.
MOVIE_STUB = 0x2000


def use_windows_newlines(generated):
    """PROGRAM_SHA256 authenticates the lifter's Windows output, which Python
    writes with CRLF line endings; other hosts write LF, so match those bytes."""
    if sys.platform == "win32":
        return
    for path in generated.iterdir():
        if path.is_file():
            data = path.read_bytes()
            path.write_bytes(data.replace(b"\r\n", b"\n").replace(b"\n", b"\r\n"))


def program_manifest(generated):
    chunks = sorted(generated.glob("recomp_[0-9][0-9][0-9][0-9].c"))
    members = chunks + [generated / "recomp_dispatch.c", generated / "recomp_funcs.h"]
    manifest = "".join(f"{p.name}\t{p.stat().st_size}\t{sha256(p)}\n" for p in members)
    ebp = sum(p.read_text(encoding="utf-8").count("\n    uint32_t ebp;\n")
              for p in members if p.suffix == ".c")
    return hashlib.sha256(manifest.encode("utf-8")).hexdigest(), ebp


def movies_cut(disc):
    return any(movie.stat().st_size == MOVIE_STUB for movie in disc.glob("*.sfd"))


def disc_version(disc):
    """The disc's default.xbe (any case) and its entry in VERSIONS."""
    xbe = next((p for p in disc.iterdir() if p.name.casefold() == "default.xbe"), None)
    version = VERSIONS.get(sha256(xbe)) if xbe else None
    if version is None:
        raise ValueError("This build supports Dead or Alive 3 (Xbox) Japan 3.1 and USA 3.0 only: "
                         "default.xbe is not one of them")
    return xbe, version


def extractions():
    """The build's own extractions, one per version, Japan first."""
    return [ROOT / "private" / ("imported-disc" + v["suffix"]) for v in VERSIONS.values()]


def rebuild_disc():
    """Without a source: the disc the last build used, else an extraction."""
    path_file = ROOT / "private" / "disc-path.txt"
    if path_file.is_file():
        last = Path(path_file.read_text(encoding="utf-8").splitlines()[0].strip())
        if last.is_dir():
            return last
    return next((i / "disc" for i in extractions() if (i / "receipt.json").is_file()), None)


def find_disc(source, movies=True, extractor=None):
    if source is not None and not source.is_file():
        return source.resolve(strict=True)
    if source is None:
        disc = rebuild_disc()
        if disc is None:
            raise ValueError("Choose your Dead or Alive 3 ISO or extracted disc folder to build from")
        if movies and movies_cut(disc):
            raise ValueError("This build removed the movies; build again from your ISO to restore them")
        print(f"Using the disc in {disc}", flush=True)
        return disc
    iso_sha256 = sha256(source)
    for imported in extractions():
        receipt = imported / "receipt.json"
        if (receipt.is_file() and json.loads(receipt.read_text(encoding="utf-8")).get("iso_sha256") == iso_sha256
                and not (movies and movies_cut(imported / "disc"))):
            print(f"Using the disc already extracted to {imported / 'disc'}", flush=True)
            return imported / "disc"
    # Extract beside the others, then take the version's place, so another
    # version's extraction (and its play folder's saves) is never touched.
    staging = ROOT / "private" / "imported-disc.new"
    if staging.exists():
        shutil.rmtree(staging)
    bundled = ROOT / "tools" / "artifacts" / "extract-xiso.exe"
    extract(source, staging, extractor or (bundled if bundled.is_file() else "extract-xiso"))
    imported = ROOT / "private" / ("imported-disc" + disc_version(staging / "disc")[1]["suffix"])
    if imported.exists():
        # The same version from another ISO, an unfinished extraction, or one whose movies were cut.
        if (imported / "disc" / ".recomp-storage").exists():
            raise ValueError(f"{imported} holds game saves; move them out before using another ISO")
        print(f"Replacing {imported}", flush=True)
        shutil.rmtree(imported)
    staging.rename(imported)
    return imported / "disc"


def prepare_play_disc(disc, movies, name="play-disc"):
    """Hard-link the disc into private/<name>, which keeps the game's cache and
    saves (.recomp-storage) out of the source. Without movies each .sfd becomes a
    stub copied from the source, which is left whole."""
    play = ROOT / "private" / name
    # Fill a staging folder so an interrupted first fill is never taken as complete.
    folder = play if play.is_dir() else play.with_name(name + ".tmp")
    for file in sorted(disc.rglob("*")):
        relative = file.relative_to(disc)
        if not file.is_file() or relative.parts[0].casefold() == ".recomp-storage":
            continue
        size = file.stat().st_size
        cut = not movies and file.suffix.casefold() == ".sfd" and size > MOVIE_STUB
        target = folder / relative
        if target.is_file() and target.stat().st_size == (MOVIE_STUB if cut else size):
            continue
        target.parent.mkdir(parents=True, exist_ok=True)
        partial = target.with_name(target.name + ".tmp")
        partial.unlink(missing_ok=True)
        if cut:
            with file.open("rb") as movie:
                partial.write_bytes(movie.read(MOVIE_STUB))
        else:
            try:
                os.link(file, partial)
            except OSError:  # another drive
                shutil.copyfile(file, partial)
        os.replace(partial, target)
    if folder != play:
        folder.rename(play)


def cut_owned_movies(disc):
    """Free the space of movies left out of play-disc, but only in the extraction
    this build made; a user's own folder is never changed."""
    if disc.resolve() not in [(i / "disc").resolve() for i in extractions()]:
        return
    for movie in disc.glob("*.sfd"):
        if movie.stat().st_size > MOVIE_STUB:
            os.truncate(movie, MOVIE_STUB)


def ask_movies(source):
    last = rebuild_disc() if source is None else None
    if last is not None and movies_cut(last):
        print("Rebuilding without movies; build again from your ISO to restore them.", flush=True)
        return False
    if not sys.stdin.isatty():
        return True
    try:
        answer = input("Install the movies? They take 3.4 GB; without them the game skips "
                       "every movie. [Y/n] ")
    except EOFError:
        return True
    return not answer.strip().casefold().startswith("n")


def has_msvc():
    # Without it CMake falls back to NMake and fails with a confusing "-A x64" error.
    vswhere = Path(os.environ.get("ProgramFiles(x86)", r"C:\Program Files (x86)"),
                   "Microsoft Visual Studio", "Installer", "vswhere.exe")
    if not vswhere.exists():
        return False
    found = subprocess.run([vswhere, "-latest", "-products", "*", "-requires",
                            "Microsoft.VisualStudio.Component.VC.Tools.x86.x64",
                            "-property", "installationPath"], capture_output=True, text=True)
    return bool(found.stdout.strip())


def install_msvc():
    # VS 2022 Build Tools rather than 2026: every CMake from 3.20 knows its generator.
    print("Installing the Visual Studio 2022 C++ Build Tools (several GB); accept the "
          "Windows prompt and wait for the installer to finish.", flush=True)
    with tempfile.TemporaryDirectory() as folder:
        bootstrapper = Path(folder, "vs_BuildTools.exe")
        urllib.request.urlretrieve("https://aka.ms/vs/17/release/vs_BuildTools.exe", bootstrapper)
        code = subprocess.run([bootstrapper, "--passive", "--wait", "--norestart",
                               "--add", "Microsoft.VisualStudio.Workload.VCTools",
                               "--includeRecommended"]).returncode
    # 3010: installed, a restart is recommended but the build works without one.
    if code not in (0, 3010) or not has_msvc():
        raise ValueError('Install Visual Studio 2022 or newer, or its Build Tools, with the '
                         f'"Desktop development with C++" workload (installer exit code {code})')


def build(source, movies=True, extractor=None):
    windows = sys.platform == "win32"
    disc = find_disc(source, movies, extractor)
    xbe, version = disc_version(disc)
    play = "play-disc" + version["suffix"]
    print(f"Dead or Alive 3 {version['name']}", flush=True)
    print(f"Linking the disc into private/{play}" + ("" if movies else " without movies"), flush=True)
    prepare_play_disc(disc, movies, play)
    if shutil.which("cmake") is None:
        raise ValueError("Install CMake 3.20 or newer and add it to PATH")
    if windows and not has_msvc():
        install_msvc()
    if not windows and shutil.which("ninja") is None:
        raise ValueError("Install Ninja and add it to PATH")
    try:
        import capstone
        ready = capstone.__version__ == CAPSTONE
    except ImportError:
        ready = False
    if not ready:
        subprocess.check_call([sys.executable, "-m", "pip", "install", f"capstone=={CAPSTONE}"])

    work = ROOT / "private" / ("program-" + uuid.uuid4().hex[:8])
    work.mkdir(parents=True)
    disasm, generated = work / "disasm", work / "generated"
    recipe = ROOT / "tools" / version["recipe"]
    print(f"Lifting {xbe.name} takes several minutes.", flush=True)
    analysis = work / "default_analysis.json"
    run_logged([sys.executable, "-u", "-m", "tools.xbe_parser", xbe, "--json", analysis],
               work / "xbe_parser.log", LIFTER)
    run_logged([sys.executable, "-u", "-m", "tools.disasm", xbe, "-o", disasm,
                "--analysis-json", analysis,
                "--seed-functions", recipe / "seed_functions.json"],
               work / "disasm.log", LIFTER)
    # The missing func-id/ABI/icall paths keep the lifter to the recipe's inputs.
    run_logged([sys.executable, "-u", "-m", "tools.recomp", xbe, "--all", "--split", "1000",
                "--game-name", "Dead or Alive 3",
                "--functions", disasm / "functions.json", "--labels", disasm / "labels.json",
                "--func-id-dir", work / "none", "--abi-dir", work / "none",
                "--icall-sites", work / "none.json",
                "--manual-functions", recipe / "manual_functions.json",
                "--exclude-manual", ROOT / "recomp-runtime" / version["forwards"],
                "--gen-dir", generated, "--output-dir", work / "metadata"],
               work / "lift.log", LIFTER)
    use_windows_newlines(generated)
    manifest, ebp = program_manifest(generated)
    if (manifest, ebp) != (version["program"], version["ebp"]):
        raise ValueError(f"Generated program {manifest} differs from the validated {version['program']}")

    build_dir = ROOT / "build" / "recomp-program"
    generator = ["-A", "x64"] if windows else ["-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release"]
    if sys.platform.startswith("linux"):
        generator.append("-DRECOMP_FETCH_SDL3=ON")  # used only when the system has no SDL3
    run_logged(["cmake", "-S", ROOT / "recomp-runtime", "-B", build_dir, *generator,
                f"-DRECOMP_PROGRAM_DIR={generated.as_posix()}",
                f"-DRECOMP_PROGRAM_MANIFEST_SHA256={manifest}",
                f"-DRECOMP_PROGRAM_EBP_EXPECTED={ebp}",
                f"-DRECOMP_DOA3_JP={'ON' if version['jp'] else 'OFF'}"], work / "configure.log")
    # Linux builds the settings launcher beside the runner; SteamOS has no compiler.
    targets = ["recomp_program_runner"] + (["recomp_launcher"] if sys.platform.startswith("linux") else [])
    # Generated chunks are large; more jobs than this can exhaust memory.
    jobs = ["--parallel", str(min(4, os.cpu_count() or 1))] if not windows else []
    run_logged(["cmake", "--build", build_dir, "--config", "Release", *jobs,
                "--target", *targets], work / "build.log")
    (ROOT / "private" / "disc-path.txt").write_text(str(disc) + "\n", encoding="utf-8")
    # What is installed, for the launchers and the play scripts.
    (ROOT / "private" / "build.json").write_text(json.dumps(
        {"version": version["name"], "play_disc": play, "xbe": xbe.name}, indent=2) + "\n", encoding="utf-8")
    if not movies:
        cut_owned_movies(disc)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, nargs="?",
                        help="DOA3 ISO, or a folder holding the extracted disc's default.xbe")
    parser.add_argument("--movies", action=argparse.BooleanOptionalAction,
                        help="Install the movies (3.4 GB); asks when omitted")
    parser.add_argument("--extractor", help="extract-xiso to use instead of the one on PATH")
    args = parser.parse_args()
    try:
        build(args.source, ask_movies(args.source) if args.movies is None else args.movies,
              args.extractor)
    except (OSError, ValueError, subprocess.CalledProcessError) as error:
        print(f"Build failed: {error}", file=sys.stderr)
        return 1
    installed = json.loads((ROOT / "private" / "build.json").read_text(encoding="utf-8"))["version"]
    print(f"Build complete: Dead or Alive 3 {installed}. "
          + ("Choose START GAME in Launcher.exe, or run DOA3.exe, to play." if sys.platform == "win32"
             else "Run ./launcher.sh to play."))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
