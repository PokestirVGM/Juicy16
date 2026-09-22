"""Extract a checksummed upstream tarball, removing its single root directory."""
import pathlib
import sys
import tarfile

archive, destination = sys.argv[1:]
root = pathlib.Path(destination).resolve()
with tarfile.open(archive) as source:
    for member in source:
        parts = pathlib.PurePosixPath(member.name).parts
        if len(parts) < 2:
            continue
        relative = str(pathlib.PurePosixPath(*parts[1:]))
        target = (root / relative).resolve()
        if not target.is_relative_to(root):
            raise ValueError(f"Unsafe archive member: {member.name}")
        if member.isdir():
            target.mkdir(parents=True, exist_ok=True)
        elif member.isfile() or member.issym() or member.islnk():
            # Materialize archive-internal aliases, without Windows symlink privileges.
            data = source.extractfile(member)
            if data is None:
                raise ValueError(f"Unreadable archive member: {member.name}")
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data.read())
        else:
            raise ValueError(f"Unsupported archive member: {member.name}")
