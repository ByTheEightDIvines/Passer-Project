#!/usr/bin/env python3
"""Add the local (non-GitHub) assets to the CI-built all-platform dusk.

The code lives on GitHub and CI builds the combined, all-platform bundle without any game-derived
files. The assets (tent archive, passer archives, HD textures) are kept locally and only go into the
final dusk that gets uploaded. This copies every file from the CI bundle and adds the local folders.

Usage:
  add_assets.py CI_BUNDLE.dusk ASSET_DIR -o OUTPUT.dusk

ASSET_DIR may contain any of:
  overlay/   game file overlays, e.g. overlay/res/Object/PsrTent.arc
  textures/  HD texture replacements (Dolphin-style tex1_*.dds / .png)
  res/       bundled resources
"""
import argparse
import sys
import zipfile
from pathlib import Path

ASSET_FOLDERS = ("overlay", "textures", "res")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    p.add_argument("bundle", type=Path, help="CI-built .dusk (from the dist branch)")
    p.add_argument("assets", type=Path, help="local asset folder")
    p.add_argument("-o", "--output", type=Path, required=True)
    a = p.parse_args()
    if not a.bundle.is_file():
        sys.exit(f"error: no such bundle: {a.bundle}")
    if not a.assets.is_dir():
        sys.exit(f"error: no such asset folder: {a.assets}")

    added = []
    with zipfile.ZipFile(a.bundle) as src, zipfile.ZipFile(a.output, "w", zipfile.ZIP_DEFLATED) as out:
        existing = set()
        for info in src.infolist():
            out.writestr(info, src.read(info.filename))
            existing.add(info.filename)
        for folder in ASSET_FOLDERS:
            root = a.assets / folder
            if not root.is_dir():
                continue
            for f in sorted(root.rglob("*")):
                if not f.is_file() or f.name.startswith("."):
                    continue
                name = f.relative_to(a.assets).as_posix()
                if name in existing:
                    sys.exit(f"error: {name} is already in the CI bundle")
                out.write(f, name)
                added.append(name)
    print(f"wrote {a.output} with {len(added)} asset file(s):")
    for n in added:
        print("  " + n)


if __name__ == "__main__":
    main()
