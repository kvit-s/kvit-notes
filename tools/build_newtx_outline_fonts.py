#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Build local generated OTF outlines from a NewTX/XCharter outline plan.

This is an experiment helper for the ignored generated workspace created by
`tools/generate_newtx_microtex.py`. It copies glyph outlines from local MiKTeX
Type1 fonts into generated OTF/CFF fonts, applies the virtual-font source
scale, and bakes in each recorded VF offset. It does not vendor any font assets
into the repository.
"""

from __future__ import annotations

import argparse
import json
import math
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Any, Iterable


UNITS_PER_EM = 1000

# Cmap alias base for TeX slots below 33; must match the draw-boundary remap
# in src/mathrenderer.cpp and third_party/microtex platform/qt/graphic_qt.cpp.
LOW_SLOT_ALIAS_BASE = 0xE000

# Per-source-outline attribution, keyed by filename stem, embedded into each
# generated OTF's name table (vendoring checklist item 2 in
# tex-mathdesign-progress.md). Stems not listed fall back to the newtx-bundle
# attribution.
_TXFONTS_GPL_STEMS = {
    "txsys", "txbsys", "txexs", "txbexs", "txexas", "txbexas",
    "txmiaX", "txbmiaX",
}
_STIX_STEMS = {"stxscr", "txmiaSTbb", "txbmiaSTbb"}

_NOTICE_CHARTER = (
    "Portions Copyright 1990 as an unpublished work by Bitstream Inc. "
    "All rights reserved. Portions copyright Andrey V. Panov and "
    "Michael Sharpe (XCharter)."
)
_NOTICE_TXFONTS = (
    "Portions derived from txfonts-lineage outlines by Young Ryu and "
    "Michael Sharpe (newtx); embedded source notice: GPL."
)
_NOTICE_NEWTXMI = (
    "Portions derived from TX fonts and GNU FreeFont (GNU GPL 3.0) via "
    "NewTXMI by Michael Sharpe."
)
_NOTICE_STIX = (
    "Portions Copyright (c) 2001-2011 by the STI Pub Companies (STIX "
    "fonts, SIL Open Font License 1.1)."
)
_NOTICE_NEWTX = (
    "Portions from the newtx bundle by Michael Sharpe (CTAN, LaTeX "
    "Project Public License 1.3)."
)

_LICENSE_DESCRIPTION = (
    "Generated for the Kvit editor from the CTAN newtx and xcharter "
    "bundles (LPPL 1.3). Portions derive from txfonts-lineage outlines "
    "whose embedded headers state GPL, from STIX glyphs (SIL OFL 1.1), "
    "and from Bitstream Charter (Bitstream grant). See the LICENSES "
    "directory and NOTICE.md distributed alongside this font."
)
_LICENSE_URL = "https://ctan.org/pkg/newtx"


def _source_notice(stem: str) -> str:
    if stem.startswith("XCharter"):
        return _NOTICE_CHARTER
    if stem in _TXFONTS_GPL_STEMS:
        return _NOTICE_TXFONTS
    if stem.startswith("NewTX"):
        return _NOTICE_NEWTXMI
    if stem in _STIX_STEMS:
        return _NOTICE_STIX
    return _NOTICE_NEWTX


@dataclass
class SourceFont:
    path: Path
    font: Any
    glyph_set: dict[str, Any]
    encoding: list[str] | None


@dataclass
class BuiltFont:
    tex_name: str
    microtex_name: str
    path: Path
    slots: int
    shifted_slots: int
    source_fonts: int


class OutlineBuildError(RuntimeError):
    pass


def _load_fonttools() -> dict[str, Any]:
    try:
        from fontTools import t1Lib
        from fontTools.fontBuilder import FontBuilder
        from fontTools.pens.boundsPen import BoundsPen
        from fontTools.pens.t2CharStringPen import T2CharStringPen
        from fontTools.pens.transformPen import TransformPen
        from fontTools.ttLib import TTFont
    except ImportError as exc:
        raise OutlineBuildError(
            "tools/build_newtx_outline_fonts.py requires fontTools. "
            "One local setup is:\n"
            "  python3 -m venv build/fonttools-venv\n"
            "  build/fonttools-venv/bin/python -m pip install fonttools\n"
            "  build/fonttools-venv/bin/python tools/build_newtx_outline_fonts.py "
            "--plan build/generated/newtx-charter-microtex/outline_plan.json"
        ) from exc

    return {
        "BoundsPen": BoundsPen,
        "FontBuilder": FontBuilder,
        "T2CharStringPen": T2CharStringPen,
        "TransformPen": TransformPen,
        "TTFont": TTFont,
        "t1Lib": t1Lib,
    }


def _slot_width(slot: dict[str, Any]) -> int:
    return max(0, round(float(slot["metrics"]["width"]) * UNITS_PER_EM))


def _glyph_name_for(slot: dict[str, Any], source: SourceFont) -> str:
    glyph_name = slot.get("glyph_name")
    if glyph_name:
        return str(glyph_name)

    code = int(slot["physical_code"])
    if source.encoding is not None and 0 <= code < len(source.encoding):
        return source.encoding[code]

    raise OutlineBuildError(
        f"{source.path} has no glyph name for physical code {code}"
    )


def _load_source_font(
    path: Path,
    cache: dict[Path, SourceFont],
    t1_lib: Any,
) -> SourceFont:
    path = path.resolve()
    cached = cache.get(path)
    if cached is not None:
        return cached

    if path.suffix.lower() not in {".pfb", ".pfa"}:
        raise OutlineBuildError(
            f"{path} is not a Type1 source font; only .pfb/.pfa inputs "
            "are supported for this generated NewTX/XCharter corpus"
        )
    if not path.exists():
        raise OutlineBuildError(f"source outline font does not exist: {path}")

    font = t1_lib.T1Font(str(path))
    font.parse()
    encoding = font.font.get("Encoding")
    if not isinstance(encoding, list):
        encoding = None

    loaded = SourceFont(
        path=path,
        font=font,
        glyph_set=font.getGlyphSet(),
        encoding=encoding,
    )
    cache[path] = loaded
    return loaded


def _font_transform(slot: dict[str, Any]) -> tuple[float, float, float, float, float, float]:
    scale = float(slot["source_scale"])
    x_offset = float(slot["x_offset_em"]) * UNITS_PER_EM
    # DVI y offsets increase downward. Font outlines increase upward.
    y_offset = -float(slot["y_offset_em"]) * UNITS_PER_EM
    return (scale, 0.0, 0.0, scale, x_offset, y_offset)


def _draw_charstring(
    source_glyph: Any,
    slot: dict[str, Any],
    fonttools: dict[str, Any],
) -> tuple[Any, tuple[float, float, float, float] | None]:
    BoundsPen = fonttools["BoundsPen"]
    T2CharStringPen = fonttools["T2CharStringPen"]
    TransformPen = fonttools["TransformPen"]

    transform = _font_transform(slot)
    bounds_pen = BoundsPen(None)
    source_glyph.draw(TransformPen(bounds_pen, transform))

    charstring_pen = T2CharStringPen(_slot_width(slot), None)
    source_glyph.draw(TransformPen(charstring_pen, transform))
    return charstring_pen.getCharString(), bounds_pen.bounds


def _empty_charstring(fonttools: dict[str, Any], width: int = 500) -> Any:
    T2CharStringPen = fonttools["T2CharStringPen"]
    return T2CharStringPen(width, None).getCharString()


def _validate_saved_font(path: Path, expected_codes: set[int], fonttools: dict[str, Any]) -> None:
    TTFont = fonttools["TTFont"]
    font = TTFont(path)
    actual_codes: set[int] = set()
    for table in font["cmap"].tables:
        if table.isUnicode():
            actual_codes.update(table.cmap)

    missing = sorted(expected_codes - actual_codes)
    extra = sorted(actual_codes - expected_codes)
    if missing or extra:
        details = []
        if missing:
            details.append(f"missing codes {missing[:10]}")
        if extra:
            details.append(f"extra codes {extra[:10]}")
        raise OutlineBuildError(f"{path} cmap validation failed: {'; '.join(details)}")
    if "CFF " not in font:
        raise OutlineBuildError(f"{path} is missing its CFF table")


def _ps_name(value: str) -> str:
    clean = "".join(ch for ch in value if ch.isalnum())
    return clean[:63] or "KvitNewTX"


def _build_font(
    font_plan: dict[str, Any],
    source_cache: dict[Path, SourceFont],
    fonttools: dict[str, Any],
    overwrite: bool,
) -> BuiltFont:
    FontBuilder = fonttools["FontBuilder"]
    t1_lib = fonttools["t1Lib"]

    tex_name = str(font_plan["tex_name"])
    microtex_name = str(font_plan["microtex_name"])
    target_path = Path(str(font_plan["target_outline_path"]))
    slots = sorted(font_plan["slots"], key=lambda item: int(item["target_code"]))
    if not slots:
        raise OutlineBuildError(f"{tex_name} has no slots to build")
    if font_plan.get("blocked"):
        blocked = "; ".join(str(item) for item in font_plan["blocked"][:3])
        raise OutlineBuildError(f"{tex_name} is blocked in outline_plan.json: {blocked}")
    if target_path.exists() and not overwrite:
        raise OutlineBuildError(f"{target_path} already exists; pass --force to overwrite")

    glyph_order = [".notdef"]
    charstrings = {".notdef": _empty_charstring(fonttools)}
    hmetrics = {".notdef": (500, 0)}
    cmap: dict[int, str] = {}
    all_bounds: list[tuple[float, float, float, float]] = []
    source_paths: set[Path] = set()

    for slot in slots:
        target_code = int(slot["target_code"])
        glyph_name = f"slot{target_code:03d}"
        outline_path = slot.get("outline_path")
        if outline_path is None:
            raise OutlineBuildError(f"{tex_name} slot {target_code} has no outline path")

        source = _load_source_font(Path(str(outline_path)), source_cache, t1_lib)
        source_paths.add(source.path)
        source_glyph_name = _glyph_name_for(slot, source)
        source_glyph = source.glyph_set.get(source_glyph_name)
        if source_glyph is None:
            raise OutlineBuildError(
                f"{tex_name} slot {target_code} maps to missing glyph "
                f"{source_glyph_name!r} in {source.path}"
            )

        charstring, bounds = _draw_charstring(source_glyph, slot, fonttools)
        glyph_order.append(glyph_name)
        charstrings[glyph_name] = charstring
        hmetrics[glyph_name] = (_slot_width(slot), round(bounds[0]) if bounds else 0)
        cmap[target_code] = glyph_name
        if bounds is not None:
            all_bounds.append(bounds)

    # Qt's text pipeline cannot address several sub-33 codepoints: the wstring
    # conversion truncates at NUL, and 9/10/12/13/32 shape as whitespace with
    # no ink. MicroTeX's vendored fonts avoid codes below 33 entirely; the
    # generated fonts keep the raw TeX slots for the metric tables and alias
    # each low slot at U+E000+slot so the Qt draw boundary can reach the glyph.
    for code in [c for c in cmap if c < 33]:
        cmap[LOW_SLOT_ALIAS_BASE + code] = cmap[code]

    if all_bounds:
        x_min = math.floor(min(bounds[0] for bounds in all_bounds))
        y_min = math.floor(min(bounds[1] for bounds in all_bounds))
        x_max = math.ceil(max(bounds[2] for bounds in all_bounds))
        y_max = math.ceil(max(bounds[3] for bounds in all_bounds))
    else:
        x_min, y_min, x_max, y_max = 0, -250, UNITS_PER_EM, 750

    metric_ascent = max(float(slot["metrics"]["height"]) for slot in slots) * UNITS_PER_EM
    metric_descent = max(float(slot["metrics"]["depth"]) for slot in slots) * UNITS_PER_EM
    ascent = max(1, math.ceil(max(y_max, metric_ascent)))
    descent = min(-1, math.floor(min(y_min, -metric_descent)))

    target_path.parent.mkdir(parents=True, exist_ok=True)
    ps_name = _ps_name(f"KvitNewTX{microtex_name}")
    family_name = f"Kvit Generated {microtex_name}"

    builder = FontBuilder(UNITS_PER_EM, isTTF=False)
    builder.setupGlyphOrder(glyph_order)
    builder.setupCharacterMap(cmap)
    builder.setupHorizontalMetrics(hmetrics)
    builder.setupHorizontalHeader(ascent=ascent, descent=descent)
    source_names = sorted(path.name for path in source_paths)
    notices = sorted({_source_notice(path.stem) for path in source_paths})
    builder.setupNameTable(
        {
            "copyright": " ".join(notices),
            "familyName": family_name,
            "styleName": "Regular",
            "uniqueFontIdentifier": ps_name,
            "fullName": family_name,
            "psName": ps_name,
            "version": "Version 0.001",
            "description": (
                f"Generated for the Kvit editor from TeX font {tex_name} "
                "(newtx/XCharter). Glyph outlines derived from: "
                + ", ".join(source_names) + "."
            ),
            "licenseDescription": _LICENSE_DESCRIPTION,
            "licenseInfoURL": _LICENSE_URL,
        }
    )
    builder.setupOS2(
        sTypoAscender=ascent,
        sTypoDescender=descent,
        usWinAscent=max(0, ascent),
        usWinDescent=max(0, -descent),
    )
    builder.setupPost()
    builder.setupCFF(
        ps_name,
        {"FontBBox": [x_min, y_min, x_max, y_max]},
        charstrings,
        {},
    )
    builder.setupMaxp()
    builder.save(target_path)
    _validate_saved_font(target_path, set(cmap), fonttools)

    return BuiltFont(
        tex_name=tex_name,
        microtex_name=microtex_name,
        path=target_path,
        slots=len(slots),
        shifted_slots=sum(
            1 for slot in slots
            if int(slot["x_offset_raw"]) != 0 or int(slot["y_offset_raw"]) != 0
        ),
        source_fonts=len(source_paths),
    )


def _selected_fonts(
    plan: dict[str, Any],
    requested: set[str],
) -> list[dict[str, Any]]:
    fonts = list(plan["fonts"])
    if not requested:
        return fonts

    selected = [
        font for font in fonts
        if str(font["tex_name"]) in requested or str(font["microtex_name"]) in requested
    ]
    found = {str(font["tex_name"]) for font in selected}
    found.update(str(font["microtex_name"]) for font in selected)
    missing = sorted(requested - found)
    if missing:
        raise OutlineBuildError(f"font(s) not present in plan: {', '.join(missing)}")
    return selected


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--plan",
        type=Path,
        default=Path("build/generated/newtx-charter-microtex/outline_plan.json"),
        help="outline_plan.json produced by tools/generate_newtx_microtex.py.",
    )
    parser.add_argument(
        "--font",
        action="append",
        dest="fonts",
        help="TeX or MicroTeX font name to build. May be repeated. Defaults to all fonts.",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Overwrite existing generated OTF files.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    try:
        fonttools = _load_fonttools()
        plan_path = args.plan.resolve()
        if not plan_path.exists():
            raise OutlineBuildError(f"outline plan does not exist: {plan_path}")

        plan = json.loads(plan_path.read_text(encoding="utf-8"))
        fonts = _selected_fonts(plan, set(args.fonts or []))
        source_cache: dict[Path, SourceFont] = {}
        built = [
            _build_font(font, source_cache, fonttools, overwrite=args.force)
            for font in fonts
        ]
    except OutlineBuildError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2

    for font in built:
        print(
            "Built {} from {} source font(s): {} slots, {} shifted -> {}".format(
                font.microtex_name,
                font.source_fonts,
                font.slots,
                font.shifted_slots,
                font.path,
            )
        )
    print(f"Built {len(built)} generated outline font(s).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
