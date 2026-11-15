"""Pack a published MonoPod Desktop directory without storing binaries in Git."""
from __future__ import annotations

import argparse
from pathlib import Path
import zipfile


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--publish-dir", type=Path, default=root / "publish" / "win-x64")
    parser.add_argument("--output", type=Path, default=root / "outputs" / "MonoPod_Desktop_win-x64.zip")
    args = parser.parse_args()
    publish = args.publish_dir.resolve()
    executable = publish / "MonoPod.Desktop.exe"
    if not executable.is_file():
        parser.error(f"Publication not found: {executable}. Run dotnet publish first.")
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    paths = sorted(p for p in publish.rglob("*") if p.is_file())
    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path in paths:
            archive.write(path, (Path("MonoPod_Desktop") / path.relative_to(publish)).as_posix())
    print(f"Created {output} ({len(paths)} files, {output.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
