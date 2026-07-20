#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Generate experimental MicroTeX font tables for NewTX/XCharter.

This is intentionally a local experiment generator. It reads MiKTeX TFM/VF/map
data, emits MicroTeX-style metric tables, and writes an outline-copy plan for a
later font-building step. It does not copy or vendor font assets.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable

from probe_newtx_vf import (
    DEFAULT_MIKTEX_ROOT,
    DEFAULT_TARGET_FONTS,
    FontFileIndex,
    MapEntry,
    PacketAnalysis,
    TfmInfo,
    TfmMetric,
    VfInfo,
    build_file_index,
    choose_file,
    glyph_name_for,
    parse_map_files,
    parse_packet,
    parse_tfm,
    parse_vf,
    required_slot_policy_name,
    required_slots_for,
)


FIX_WORD_DENOMINATOR = float(1 << 20)


@dataclass(frozen=True)
class SlotPlan:
    target_code: int
    metric: TfmMetric
    physical_font: str
    physical_code: int
    glyph_name: str | None
    outline_path: Path | None
    encoding_path: Path | None
    x_offset: int
    y_offset: int
    source_scale: float


@dataclass(frozen=True)
class GeneratedFont:
    tex_name: str
    microtex_name: str
    tfm: TfmInfo
    vf: VfInfo | None
    required_policy: str
    slots: list[SlotPlan]
    blocked: list[str]
    target_outline_path: Path
    def_path: Path


@dataclass(frozen=True)
class LargerPlan:
    code: int
    larger: int
    font_name: str


def _safe_identifier(value: str) -> str:
    clean = re.sub(r"[^0-9A-Za-z]+", "_", value).strip("_").lower()
    if not clean:
        clean = "font"
    if clean[0].isdigit():
        clean = f"font_{clean}"
    return clean


def _fmt_float(value: float) -> str:
    if abs(value) < 0.0000005:
        return "0"
    return f"{value:.9g}"


def _font_file_name(font: GeneratedFont) -> str:
    return font.target_outline_path.name


def _fontdimens(tfm: TfmInfo) -> dict[str, float]:
    # TeX fontdimens are 1-based in the TFM spec: 2=space, 5=x-height, 6=quad.
    return {
        "space": tfm.params[1] if len(tfm.params) > 1 else 0.0,
        "xheight": tfm.params[4] if len(tfm.params) > 4 else 0.0,
        "quad": tfm.params[5] if len(tfm.params) > 5 else tfm.design_size / 10.0,
    }


def _font_relations(font: GeneratedFont) -> list[str]:
    if font.microtex_name == "kvit_newtx_zchmi":
        return [
            "bold(kvit_newtx_zchbmi)",
            "roman(kvit_newtx_xcharter_roman_tlf_t1)",
            "ss(cmss10)",
            "tt(cmtt10)",
            "it(kvit_newtx_xcharter_italic_tlf_t1)",
        ]
    if font.microtex_name == "kvit_newtx_zchbmi":
        return [
            "roman(kvit_newtx_xcharter_bold_tlf_t1)",
            "ss(cmssbx10)",
            "tt(cmtt10)",
            "it(kvit_newtx_xcharter_bolditalic_tlf_t1)",
        ]
    if font.microtex_name == "kvit_newtx_zchmia":
        return ["bold(kvit_newtx_zchbmia)"]
    if font.microtex_name == "kvit_newtx_xcharter_roman_tlf_t1":
        return [
            "bold(kvit_newtx_xcharter_bold_tlf_t1)",
            "ss(cmss10)",
            "tt(cmtt10)",
            "it(kvit_newtx_xcharter_italic_tlf_t1)",
        ]
    if font.microtex_name == "kvit_newtx_xcharter_italic_tlf_t1":
        return [
            "bold(kvit_newtx_xcharter_bolditalic_tlf_t1)",
            "roman(kvit_newtx_xcharter_roman_tlf_t1)",
            "ss(cmssi10)",
            "tt(cmtt10)",
        ]
    if font.microtex_name == "kvit_newtx_xcharter_bold_tlf_t1":
        return [
            "ss(cmssbx10)",
            "tt(cmtt10)",
            "it(kvit_newtx_xcharter_bolditalic_tlf_t1)",
        ]
    if font.microtex_name == "kvit_newtx_xcharter_bolditalic_tlf_t1":
        return [
            "roman(kvit_newtx_xcharter_bold_tlf_t1)",
            "ss(cmssbx10)",
            "tt(cmtt10)",
        ]
    if font.microtex_name == "kvit_newtx_ntxsy":
        return ["bold(kvit_newtx_ntxbsy)"]
    if font.microtex_name == "kvit_newtx_ntxsy7":
        return ["bold(kvit_newtx_ntxbsy7)"]
    if font.microtex_name == "kvit_newtx_ntxsy5":
        return ["bold(kvit_newtx_ntxbsy5)"]
    if font.microtex_name == "kvit_newtx_ntxsym":
        return ["bold(kvit_newtx_ntxbsym)"]
    if font.microtex_name == "kvit_newtx_ntxsyc":
        return ["bold(kvit_newtx_ntxbsyc)"]
    if font.microtex_name == "kvit_newtx_ntxexx":
        return ["bold(kvit_newtx_ntxbexx)"]
    if font.microtex_name == "kvit_newtx_ntxexa":
        return ["bold(kvit_newtx_ntxbexa)"]
    return []


def _extra_largers(font: GeneratedFont) -> list[LargerPlan]:
    large = "kvit_newtx_ntxexx"
    large_a = "kvit_newtx_ntxexa"
    if font.microtex_name == "kvit_newtx_zchmia":
        return [
            LargerPlan(62, 8, large),  # \smlbrace
            LargerPlan(63, 9, large),  # \smrbrace
        ]
    if font.microtex_name in (
        "kvit_newtx_ntxsy",
        # The optical script masters share ntxsy's encoding, so their
        # delimiter chains jump into the same full-size extension families,
        # matching TeX's move from script-size symbol font to text-size
        # extension font.
        "kvit_newtx_ntxsy7",
        "kvit_newtx_ntxsy5",
    ):
        return [
            LargerPlan(34, 120, large),   # \uparrow
            LargerPlan(35, 121, large),   # \downarrow
            LargerPlan(42, 126, large),   # \Uparrow
            LargerPlan(43, 127, large),   # \Downarrow
            LargerPlan(98, 4, large),     # \lfloor
            LargerPlan(99, 5, large),     # \rfloor
            LargerPlan(100, 6, large),    # \lceil
            LargerPlan(101, 7, large),    # \rceil
            LargerPlan(102, 8, large),    # \lbrace
            LargerPlan(103, 9, large),    # \rbrace
            LargerPlan(104, 10, large),   # \langle
            LargerPlan(105, 11, large),   # \rangle
            LargerPlan(106, 12, large),   # \vert
            LargerPlan(107, 13, large),   # \Vert
            LargerPlan(108, 63, large),   # \updownarrow
            LargerPlan(109, 119, large),  # \Updownarrow
            LargerPlan(110, 15, large),   # \backslash
            LargerPlan(112, 112, large),  # \sqrtsign
            LargerPlan(157, 14, large),   # /
            LargerPlan(185, 0, large),    # (
            LargerPlan(186, 1, large),    # )
            LargerPlan(187, 2, large),    # [
            LargerPlan(188, 3, large),    # ]
            LargerPlan(201, 18, large_a), # \llbracket
            LargerPlan(202, 19, large_a), # \rrbracket
        ]
    if font.microtex_name == "kvit_newtx_ntxsyc":
        return [
            LargerPlan(78, 48, large_a),  # \lbag
            LargerPlan(79, 49, large_a),  # \rbag
        ]
    return []


def _required_slots(name: str, tfm: TfmInfo, vf: VfInfo | None, all_slots_required: bool) -> list[int]:
    if all_slots_required:
        if vf is not None:
            return sorted(packet.code for packet in vf.packets)
        return sorted(tfm.metrics)

    configured = required_slots_for(name)
    if configured is not None:
        if vf is not None:
            packet_codes = {packet.code for packet in vf.packets}
            return sorted(configured & packet_codes)
        return sorted(configured & set(tfm.metrics))

    if vf is not None:
        return sorted(packet.code for packet in vf.packets)
    return sorted(tfm.metrics)


def _physical_slot_plan(
    slot: int,
    metric: TfmMetric,
    tex_name: str,
    map_entry: MapEntry | None,
    enc_cache: dict[Path, list[str] | None],
) -> SlotPlan:
    return SlotPlan(
        target_code=slot,
        metric=metric,
        physical_font=tex_name,
        physical_code=slot,
        glyph_name=glyph_name_for(map_entry, slot, enc_cache),
        outline_path=map_entry.outline_path if map_entry else None,
        encoding_path=map_entry.encoding_path if map_entry else None,
        x_offset=0,
        y_offset=0,
        source_scale=1.0,
    )


def _virtual_slot_plan(
    slot: int,
    metric: TfmMetric,
    analysis: PacketAnalysis,
    vf: VfInfo,
    map_entries: dict[str, MapEntry],
    enc_cache: dict[Path, list[str] | None],
) -> SlotPlan | str:
    if not analysis.flattenable or not analysis.glyphs:
        return f"slot {slot} is not a single physical glyph"

    glyph = analysis.glyphs[0]
    font_def = vf.font_defs.get(glyph.local_font)
    if font_def is None:
        return f"slot {slot} uses missing local VF font {glyph.local_font}"

    map_entry = map_entries.get(font_def.name)
    if map_entry is None:
        return f"slot {slot} uses physical font {font_def.name} without a map entry"

    return SlotPlan(
        target_code=slot,
        metric=metric,
        physical_font=font_def.name,
        physical_code=glyph.code,
        glyph_name=glyph_name_for(map_entry, glyph.code, enc_cache),
        outline_path=map_entry.outline_path,
        encoding_path=map_entry.encoding_path,
        x_offset=glyph.x,
        y_offset=glyph.y,
        source_scale=font_def.scale / FIX_WORD_DENOMINATOR,
    )


def build_generated_font(
    name: str,
    index: FontFileIndex,
    map_entries: dict[str, MapEntry],
    enc_cache: dict[Path, list[str] | None],
    output_dir: Path,
    prefix: str,
    all_slots_required: bool,
) -> GeneratedFont:
    tfm_path = choose_file(index.tfm, f"{name}.tfm")
    vf_path = choose_file(index.vf, f"{name}.vf")
    microtex_name = prefix + _safe_identifier(name)
    target_outline_path = output_dir / "fonts" / f"{microtex_name}.otf"
    def_path = output_dir / "defs" / f"{microtex_name}.def.cpp"

    if tfm_path is None:
        return GeneratedFont(
            tex_name=name,
            microtex_name=microtex_name,
            tfm=TfmInfo(
                path=Path(),
                checksum=0,
                design_size=0,
                bc=0,
                ec=0,
                metrics={},
                kerns=[],
                ligatures=[],
                largers=[],
                extensions=[],
                params=[],
            ),
            vf=None,
            required_policy="missing TFM",
            slots=[],
            blocked=[f"{name}.tfm not found"],
            target_outline_path=target_outline_path,
            def_path=def_path,
        )

    tfm = parse_tfm(tfm_path)
    vf = parse_vf(vf_path) if vf_path is not None else None
    slots = _required_slots(name, tfm, vf, all_slots_required)
    required_policy = required_slot_policy_name(
        name, None if all_slots_required else required_slots_for(name)
    )
    blocked: list[str] = []
    planned: list[SlotPlan] = []

    if vf is None:
        map_entry = map_entries.get(name)
        if map_entry is None:
            blocked.append(f"{name} has no VF and no map entry")
        for slot in slots:
            metric = tfm.metrics.get(slot)
            if metric is None:
                blocked.append(f"slot {slot} has no TFM metric")
                continue
            planned.append(_physical_slot_plan(slot, metric, name, map_entry, enc_cache))
    else:
        packet_by_code = {packet.code: packet for packet in vf.packets}
        for slot in slots:
            metric = tfm.metrics.get(slot)
            packet = packet_by_code.get(slot)
            if metric is None:
                blocked.append(f"slot {slot} has no TFM metric")
                continue
            if packet is None:
                blocked.append(f"slot {slot} has no VF packet")
                continue
            plan = _virtual_slot_plan(
                slot,
                metric,
                parse_packet(packet.dvi),
                vf,
                map_entries,
                enc_cache,
            )
            if isinstance(plan, str):
                blocked.append(plan)
            else:
                planned.append(plan)

    return GeneratedFont(
        tex_name=name,
        microtex_name=microtex_name,
        tfm=tfm,
        vf=vf,
        required_policy=required_policy,
        slots=planned,
        blocked=blocked,
        target_outline_path=target_outline_path,
        def_path=def_path,
    )


def slot_plan_to_json(slot: SlotPlan) -> dict[str, object]:
    return {
        "target_code": slot.target_code,
        "physical_font": slot.physical_font,
        "physical_code": slot.physical_code,
        "glyph_name": slot.glyph_name,
        "outline_path": str(slot.outline_path) if slot.outline_path else None,
        "encoding_path": str(slot.encoding_path) if slot.encoding_path else None,
        "source_scale": slot.source_scale,
        "x_offset_raw": slot.x_offset,
        "y_offset_raw": slot.y_offset,
        "x_offset_em": slot.x_offset / FIX_WORD_DENOMINATOR,
        "y_offset_em": slot.y_offset / FIX_WORD_DENOMINATOR,
        "metrics": {
            "width": slot.metric.width,
            "height": slot.metric.height,
            "depth": slot.metric.depth,
            "italic": slot.metric.italic,
        },
    }


def generated_font_to_json(font: GeneratedFont) -> dict[str, object]:
    shifted = sum(1 for slot in font.slots if slot.x_offset or slot.y_offset)
    return {
        "tex_name": font.tex_name,
        "microtex_name": font.microtex_name,
        "tfm": str(font.tfm.path) if font.tfm.path else None,
        "vf": str(font.vf.path) if font.vf else None,
        "required_policy": font.required_policy,
        "target_outline_path": str(font.target_outline_path),
        "def_path": str(font.def_path),
        "slot_count": len(font.slots),
        "shifted_slot_count": shifted,
        "blocked": font.blocked,
        "physical_fonts": sorted({slot.physical_font for slot in font.slots}),
        "slots": [slot_plan_to_json(slot) for slot in font.slots],
    }


def write_def(font: GeneratedFont) -> None:
    font.def_path.parent.mkdir(parents=True, exist_ok=True)
    dims = _fontdimens(font.tfm)
    slots = sorted(font.slots, key=lambda item: item.target_code)
    slot_codes = {slot.target_code for slot in slots}
    kerns = [
        kern for kern in font.tfm.kerns
        if kern.left in slot_codes and kern.right in slot_codes
    ]
    ligatures = [
        lig for lig in font.tfm.ligatures
        if lig.left in slot_codes and lig.right in slot_codes and lig.ligature in slot_codes
    ]
    extensions = [
        ext for ext in font.tfm.extensions
        if ext.code in slot_codes
    ]
    largers = [
        LargerPlan(larger.code, larger.larger, font.microtex_name)
        for larger in font.tfm.largers
        if larger.code in slot_codes and larger.larger in slot_codes
    ]
    for larger in _extra_largers(font):
        if larger.code in slot_codes:
            largers.append(larger)

    lines = [
        '#include "res/font_def.res.h"',
        "",
        "#include <cstdlib>",
        "#include <string>",
        "",
        "// Generated by tools/generate_newtx_microtex.py.",
        "// The target outline font is intentionally not generated here;",
        "// build it from outline_plan.json before registering this definition.",
        "namespace {",
        "",
        "std::string kvitNewtxGeneratedFontPath(const char* fileName) {",
        '  const char* root = std::getenv("KVIT_NEWTX_MATH_ROOT");',
        "  if (root != nullptr && root[0] != '\\0') {",
        "    std::string path(root);",
        "    if (!path.empty() && path.back() != '/')",
        "      path += '/';",
        '    path += "fonts/";',
        "    path += fileName;",
        "    return path;",
        "  }",
        "  // Vendored default: the generated OTFs ship inside the MicroTeX",
        "  // resource tree (res/fonts/kvit-newtx), so they follow KVIT_MATH_RES",
        "  // overrides the same way the builtin fonts do.",
        '  return tex::RES_BASE + "/fonts/kvit-newtx/" + fileName;',
        "}",
        "",
        "}  // namespace",
        "",
        f"void __font_reg({font.microtex_name})() {{",
        f'  int id = tex::FontInfo::__id("{font.microtex_name}");',
        "  auto info = tex::FontInfo::__create(",
        f'    id, kvitNewtxGeneratedFontPath("{_font_file_name(font)}"));',
        "",
    ]

    if dims["xheight"]:
        lines.append(f"xHeight({_fmt_float(dims['xheight'])})")
    if dims["quad"]:
        lines.append(f"quad({_fmt_float(dims['quad'])})")
    if dims["space"]:
        lines.append(f"space({_fmt_float(dims['space'])})")
    if any(dims.values()):
        lines.append("")

    relations = _font_relations(font)
    if relations:
        lines.extend(relations)
        lines.append("")

    lines.append("METRICS_START")
    for slot in slots:
        metric = slot.metric
        lines.append(
            "{}, {}, {}, {}, {},".format(
                slot.target_code,
                _fmt_float(metric.width),
                _fmt_float(metric.height),
                _fmt_float(metric.depth),
                _fmt_float(metric.italic),
            )
        )
    lines.append("METRICS_END")

    if ligatures:
        lines.extend(["", "LIGTURES_START"])
        for lig in ligatures:
            lines.append(f"{lig.left}, {lig.right}, {lig.ligature},")
        lines.append("LIGTURES_END")

    if kerns:
        lines.extend(["", "KERNS_START"])
        for kern in kerns:
            lines.append(
                "{}, {}, {},".format(
                    kern.left,
                    kern.right,
                    _fmt_float(kern.amount),
                )
            )
        lines.append("KERNS_END")

    if extensions:
        lines.extend(["", "EXTENSIONS_START"])
        for ext in extensions:
            lines.append(
                f"{ext.code}, {ext.top}, {ext.middle}, {ext.repeat}, {ext.bottom},"
            )
        lines.append("EXTENSIONS_END")

    if largers:
        font_id_vars = {
            font_name: f"largerFont{index}"
            for index, font_name in enumerate(
                sorted({larger.font_name for larger in largers})
            )
        }
        lines.extend(["", "{"])
        for font_name, var_name in font_id_vars.items():
            lines.append(f'  const int {var_name} = tex::FontInfo::__id("{font_name}");')
        lines.append(f"  auto* larger = new int[{len(largers) * 3}] {{")
        for larger in sorted(largers, key=lambda item: (item.code, item.larger, item.font_name)):
            lines.append(
                f"      {larger.code}, {larger.larger}, {font_id_vars[larger.font_name]},"
            )
        lines.append("  };")
        lines.append(f"  info->__largers(larger, {len(largers) * 3}, true);")
        lines.append("}")

    lines.extend(["", "}"])
    font.def_path.write_text("\n".join(lines) + "\n", encoding="utf-8")


# TeX reads the math layout constants from fontdimens of the symbol font
# (family 2) and the extension font (family 3). MicroTeX keeps the same
# em-relative values in DefaultTeXFont::_parameters, so the NewTX constants
# come straight from ntxsy.tfm and ntxexx.tfm. newtxmath's setSYdimens and
# setEXdimens hooks are empty providecommands in this MiKTeX tree, so the raw
# TFM fontdimens are the values LaTeX uses.
SYMBOL_FONTDIMEN_PARAMS = {
    8: "num1",
    9: "num2",
    10: "num3",
    11: "denom1",
    12: "denom2",
    13: "sup1",
    14: "sup2",
    15: "sup3",
    16: "sub1",
    17: "sub2",
    18: "supdrop",
    19: "subdrop",
    22: "axisheight",
}

EXTENSION_FONTDIMEN_PARAMS = {
    8: "defaultrulethickness",
    9: "bigopspacing1",
    10: "bigopspacing2",
    11: "bigopspacing3",
    12: "bigopspacing4",
    13: "bigopspacing5",
}


def write_tex_params(output_dir: Path, index: FontFileIndex) -> Path | None:
    symbol_tfm_path = choose_file(index.tfm, "ntxsy.tfm")
    extension_tfm_path = choose_file(index.tfm, "ntxexx.tfm")
    if symbol_tfm_path is None or extension_tfm_path is None:
        return None

    symbol_tfm = parse_tfm(symbol_tfm_path)
    extension_tfm = parse_tfm(extension_tfm_path)

    params: list[tuple[str, float]] = []
    for dimen, name in sorted(SYMBOL_FONTDIMEN_PARAMS.items(), key=lambda kv: kv[1]):
        if len(symbol_tfm.params) >= dimen:
            params.append((name, symbol_tfm.params[dimen - 1]))
    for dimen, name in sorted(EXTENSION_FONTDIMEN_PARAMS.items(), key=lambda kv: kv[1]):
        if len(extension_tfm.params) >= dimen:
            params.append((name, extension_tfm.params[dimen - 1]))
    if not params:
        return None

    lines = [
        "// Generated by tools/generate_newtx_microtex.py.",
        "// NewTX math layout constants: sigma parameters from ntxsy.tfm",
        "// fontdimens 8-22 and xi parameters from ntxexx.tfm fontdimens 8-13,",
        "// em-relative like MicroTeX's Computer Modern defaults.",
        "#define KVIT_NEWTX_GENERATED_TEX_PARAMS_AVAILABLE 1",
        "",
        "namespace {",
        "",
        "inline void kvitNewtxApplyGeneratedTexParams(std::map<std::string, float>& params) {",
    ]
    for name, value in sorted(params):
        lines.append(f'  params["{name}"] = static_cast<float>({_fmt_float(value)});')
    lines.extend(
        [
            "}",
            "",
            "}  // namespace",
        ]
    )

    params_path = output_dir / "defs" / "kvit_newtx_tex_params.def.cpp"
    params_path.parent.mkdir(parents=True, exist_ok=True)
    params_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return params_path


def write_manifest(output_dir: Path, root: Path, fonts: list[GeneratedFont]) -> Path:
    manifest_path = output_dir / "outline_plan.json"
    manifest = {
        "generated_at": _datetime.datetime.now(_datetime.UTC).isoformat(),
        "miktex_root": str(root),
        "note": (
            "Experimental local plan only. It references source font assets but "
            "does not copy or vendor them."
        ),
        "fonts": [generated_font_to_json(font) for font in fonts],
        "totals": {
            "fonts": len(fonts),
            "blocked_fonts": sum(1 for font in fonts if font.blocked),
            "slots": sum(len(font.slots) for font in fonts),
            "shifted_slots": sum(
                1 for font in fonts for slot in font.slots
                if slot.x_offset or slot.y_offset
            ),
        },
    }
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    return manifest_path


def write_summary(output_dir: Path, fonts: list[GeneratedFont], manifest_path: Path) -> Path:
    summary_path = output_dir / "README.md"
    lines = [
        "# Generated NewTX/XCharter MicroTeX Experiment",
        "",
        "This directory is generated by `tools/generate_newtx_microtex.py`.",
        "It contains MicroTeX metric-table candidates and an outline-copy plan.",
        "The generator itself does not copy or convert NewTX/XCharter font",
        "assets; `tools/build_newtx_outline_fonts.py` can add ignored local",
        "generated OTFs to `fonts/` after this step.",
        "",
        f"- Outline plan: `{manifest_path.name}`",
        "- Generated `.def.cpp` files: `defs/`",
        "- Generated local outline font paths: `fonts/*.otf`",
        "",
        "| TeX font | MicroTeX id | Slots | Shifted | Blocked | Physical sources |",
        "| --- | --- | ---: | ---: | ---: | --- |",
    ]
    for font in fonts:
        shifted = sum(1 for slot in font.slots if slot.x_offset or slot.y_offset)
        physical = ", ".join(sorted({slot.physical_font for slot in font.slots}))
        lines.append(
            "| `{}` | `{}` | {} | {} | {} | {} |".format(
                font.tex_name,
                font.microtex_name,
                len(font.slots),
                shifted,
                len(font.blocked),
                physical,
            )
        )

    lines.extend(
        [
            "",
            "## Next Step",
            "",
            "Build the local generated outline fonts with:",
            "",
            "```sh",
            "python3 -m venv build/fonttools-venv",
            "build/fonttools-venv/bin/python -m pip install fonttools",
            "build/fonttools-venv/bin/python tools/build_newtx_outline_fonts.py \\",
            f"  --plan {manifest_path}",
            "```",
            "",
            "The generated `fonts/*.otf` files are local experiment artifacts. Keep",
            "them in this ignored workspace until the licensing gate is closed.",
            "",
        ]
    )
    summary_path.write_text("\n".join(lines), encoding="utf-8")
    return summary_path


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--miktex-root",
        type=Path,
        default=DEFAULT_MIKTEX_ROOT,
        help=f"MiKTeX root to inspect (default: {DEFAULT_MIKTEX_ROOT})",
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=Path("build/generated/newtx-charter-microtex"),
        help="Generated experiment workspace.",
    )
    parser.add_argument(
        "--font",
        action="append",
        dest="fonts",
        help="TeX font name to generate. May be repeated. Defaults to the target set.",
    )
    parser.add_argument(
        "--prefix",
        default="kvit_newtx_",
        help="Prefix for generated MicroTeX font ids.",
    )
    parser.add_argument(
        "--all-slots-required",
        action="store_true",
        help="Generate every VF packet, including non-required XCharter T1 slots.",
    )
    parser.add_argument(
        "--fail-on-blocked",
        action="store_true",
        help="Exit non-zero if any requested font cannot be fully generated.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    fonts = args.fonts if args.fonts else DEFAULT_TARGET_FONTS
    output_dir = args.output_dir.resolve()
    index = build_file_index(args.miktex_root)
    map_entries = parse_map_files(args.miktex_root, index)
    enc_cache: dict[Path, list[str] | None] = {}

    generated = [
        build_generated_font(
            name,
            index,
            map_entries,
            enc_cache,
            output_dir,
            args.prefix,
            args.all_slots_required,
        )
        for name in fonts
    ]

    (output_dir / "fonts").mkdir(parents=True, exist_ok=True)
    for font in generated:
        if not font.blocked:
            write_def(font)

    params_path = write_tex_params(output_dir, index)
    manifest_path = write_manifest(output_dir, args.miktex_root, generated)
    summary_path = write_summary(output_dir, generated, manifest_path)

    blocked = [font for font in generated if font.blocked]
    if params_path is not None:
        print(f"Wrote {params_path}")
    print(f"Wrote {manifest_path}")
    print(f"Wrote {summary_path}")
    print(f"Generated {len(generated) - len(blocked)} font definition(s); blocked {len(blocked)}.")
    if blocked:
        for font in blocked:
            print(f"- {font.tex_name}: {'; '.join(font.blocked[:3])}")

    return 1 if blocked and args.fail_on_blocked else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
