#!/usr/bin/env python3
"""Add the locally built (ROM-derived) room overlay to a combined .dusk.

The public CI bundle contains only the native module for every platform. The room archive comes
from your own ROM dump, so it is added here, on your machine, and the result is never committed.

Usage: inject_overlay.py combined.dusk -o final.dusk [--overlay overlay]
"""
import argparse
import sys
import zipfile
from pathlib import Path


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("bundle", type=Path)
    p.add_argument("-o", "--output", required=True, type=Path)
    p.add_argument("--overlay", type=Path, default=Path(__file__).resolve().parents[1] / "overlay")
    a = p.parse_args()
    files = sorted(f for f in a.overlay.rglob("*") if f.is_file() and f.name != ".gitkeep")
    if not files:
        sys.exit(f"error: no overlay files in {a.overlay}; run tools/build_overlay.py first")
    with zipfile.ZipFile(a.bundle) as src:
        names = {i.filename for i in src.infolist()}
        with zipfile.ZipFile(a.output, "w", zipfile.ZIP_DEFLATED) as out:
            for info in src.infolist():
                out.writestr(info, src.read(info.filename))
            for f in files:
                name = "overlay/" + f.relative_to(a.overlay).as_posix()
                if name in names:
                    sys.exit(f"error: {name} is already in the bundle")
                info = zipfile.ZipInfo(name, date_time=(1980, 1, 1, 0, 0, 0))
                info.compress_type = zipfile.ZIP_DEFLATED
                out.writestr(info, f.read_bytes())
    print(f"wrote {a.output} (+{len(files)} overlay file(s))")


if __name__ == "__main__":
    main()
