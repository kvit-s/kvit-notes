#!/usr/bin/env python3
"""Assemble kvit.ico and kvit.icns from pre-rendered PNGs.

Both container formats accept PNG-compressed images directly (ICO since
Windows Vista, ICNS for every size type used here), so this needs no
imaging library - it only writes headers around the PNG bytes produced by
generate.sh. Usage: pack_icons.py <png-dir> <out-dir>, where <png-dir>
holds kvit-<size>.png files.
"""

import struct
import sys
from pathlib import Path

ICO_SIZES = [16, 24, 32, 48, 64, 128, 256]
# ICNS type codes for PNG payloads, by pixel size.
ICNS_TYPES = {16: b"icp4", 32: b"icp5", 64: b"icp6", 128: b"ic07",
              256: b"ic08", 512: b"ic09", 1024: b"ic10"}
ICNS_SIZES = [16, 32, 64, 128, 256, 512, 1024]


def png(png_dir: Path, size: int) -> bytes:
    return (png_dir / f"kvit-{size}.png").read_bytes()


def write_ico(png_dir: Path, out: Path) -> None:
    entries = []
    offset = 6 + 16 * len(ICO_SIZES)
    blobs = []
    for size in ICO_SIZES:
        data = png(png_dir, size)
        # Width/height bytes: 0 encodes 256.
        dim = 0 if size >= 256 else size
        entries.append(struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32,
                                   len(data), offset))
        blobs.append(data)
        offset += len(data)
    out.write_bytes(struct.pack("<HHH", 0, 1, len(ICO_SIZES))
                    + b"".join(entries) + b"".join(blobs))


def write_icns(png_dir: Path, out: Path) -> None:
    chunks = b""
    for size in ICNS_SIZES:
        data = png(png_dir, size)
        chunks += ICNS_TYPES[size] + struct.pack(">I", 8 + len(data)) + data
    out.write_bytes(b"icns" + struct.pack(">I", 8 + len(chunks)) + chunks)


def main() -> int:
    png_dir, out_dir = Path(sys.argv[1]), Path(sys.argv[2])
    write_ico(png_dir, out_dir / "kvit.ico")
    write_icns(png_dir, out_dir / "kvit.icns")
    print(f"wrote {out_dir / 'kvit.ico'} and {out_dir / 'kvit.icns'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
