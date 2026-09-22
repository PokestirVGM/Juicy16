"""Create deterministic portable and corresponding-source archives; verify extraction."""
import argparse
import hashlib
import json
import pathlib
import shutil
import subprocess
import zipfile


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def archive_tree(root, output):
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for path in sorted(root.rglob("*")):
            if path.is_file():
                info = zipfile.ZipInfo(root.name + "/" + path.relative_to(root).as_posix(),
                                       (2026, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                info.external_attr = 0o100644 << 16
                archive.writestr(info, path.read_bytes())
    output.with_suffix(output.suffix + ".sha256").write_text(
        sha(output) + "  " + output.name + "\n", encoding="utf-8")
    with zipfile.ZipFile(output) as archive:
        if archive.testzip():
            raise RuntimeError("Corrupt archive")
        for info in archive.infolist():
            relative = pathlib.PurePosixPath(info.filename)
            if relative.is_absolute() or ".." in relative.parts:
                raise RuntimeError("Unsafe archive path")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=pathlib.Path, required=True)
    parser.add_argument("--artifacts", type=pathlib.Path, required=True)
    parser.add_argument("--dependency-sources", type=pathlib.Path, required=True)
    parser.add_argument("--juce-source", type=pathlib.Path, required=True)
    parser.add_argument("--name", required=True)
    args = parser.parse_args()
    root = args.root.resolve()
    output = root / "distribute/out"
    stage = output / (args.name + "-Portable")
    source = output / (args.name + "-Source")
    for directory in (stage, source):
        if directory.exists():
            raise RuntimeError(f"Refusing to overwrite existing stage: {directory}")
        directory.mkdir(parents=True)
    for fmt in ("VST3", "Standalone"):
        shutil.copytree(args.artifacts / fmt, stage / fmt,
                        ignore=shutil.ignore_patterns("*.pdb", "*.ilk", "*.lib", "*.exp"))
    for name in ("LICENSE.txt", "NOTICE.md", "README.md", "CHANGELOG.md", "PRIVACY.txt",
                 "ROADMAP.md", "building.win32.md", "building.macos.md"):
        shutil.copy2(root / name, stage / name)
    for directory in ("docs", "licenses_of_dependencies", "vendor"):
        # Only versioned/public inputs; private notes and corpora never enter the package.
        names = subprocess.check_output(["git", "ls-files", "--", directory], cwd=root,
                                        text=True).splitlines()
        if directory == "docs":
            names += ["docs/WINDOWS_RELEASE.md"]
        for name in sorted(set(names)):
            path = root / name
            if directory == "vendor" and path.suffix not in (".md", ".patch", ".cmake"):
                continue
            destination = stage / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, destination)
    shutil.copy2(root / "distribute/INSTALL-WINDOWS.txt", stage / "INSTALL-WINDOWS.txt")
    commit = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    status = subprocess.check_output(["git", "status", "--porcelain"], cwd=root, text=True)
    info = {"product": "Juicy16", "candidate": args.name, "base_commit": commit,
            "modified_source": bool(status.strip()), "source_archive": source.name + ".zip",
            "signature": "unsigned", "architecture": "x86_64",
            "vst3_sha256": sha(stage / "VST3/Juicy16.vst3/Contents/x86_64-win/Juicy16.vst3"),
            "standalone_sha256": sha(stage / "Standalone/Juicy16.exe")}
    (stage / "BUILD_INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    files = subprocess.check_output(["git", "ls-files", "--cached", "--others", "--exclude-standard"],
                                    cwd=root, text=True).splitlines()
    for name in sorted(set(files)):
        path = root / name
        if not path.is_file():
            continue
        destination = source / name
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)
    upstream = source / "upstream"
    upstream.mkdir()
    for path in sorted(args.dependency_sources.glob("*.tar.gz")):
        shutil.copy2(path, upstream / path.name)
    subprocess.run(["git", "-C", str(args.juce_source), "archive", "--format=tar.gz",
                    "--prefix=JUCE-8.0.14/", "-o", str(upstream / "JUCE-8.0.14.tar.gz"),
                    "2cdfca8feb300fb424002ba2c2751569e5bacb64"], check=True)
    (source / "SOURCE_INFO.json").write_text(json.dumps(info, indent=2) + "\n", encoding="utf-8")
    for directory in (source, stage):
        manifest = "".join(sha(p) + "  " + p.relative_to(directory).as_posix() + "\n"
                           for p in sorted(directory.rglob("*")) if p.is_file())
        (directory / "SHA256SUMS").write_text(manifest, encoding="utf-8")
        archive_tree(directory, output / (directory.name + ".zip"))
    print(stage)


if __name__ == "__main__":
    main()
