#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Probe whether NewTX/XCharter virtual fonts fit MicroTeX's font model.

MicroTeX draws one physical glyph code from one physical font for each TeX math
slot. NewTX and XCharter use TeX virtual fonts, so this script checks whether
each virtual slot collapses to exactly one physical glyph plus optional
positioning. It intentionally does not copy or convert any font assets.
"""

from __future__ import annotations

import argparse
import datetime as _datetime
import json
import shlex
import sys
from collections import Counter, defaultdict
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


DEFAULT_MIKTEX_ROOT = Path("/mnt/c/apps/miktex")

DEFAULT_TARGET_FONTS = [
    # Fonts selected by the LyX-approved preamble in this MiKTeX install.
    "zchmi",
    "zchbmi",
    "zchmia",
    "zchbmia",
    "ntxsy",
    "ntxbsy",
    # Optical script masters: lmsntxsy.fd selects ntxsy5 below 6.3pt and
    # ntxsy7 from 6.3pt up to (not including) 8.6pt for the symbols family.
    "ntxsy7",
    "ntxsy5",
    "ntxbsy7",
    "ntxbsy5",
    "ntxsym",
    "ntxbsym",
    "ntxsyc",
    "ntxbsyc",
    "ntxexx",
    "ntxbexx",
    "ntxexa",
    "ntxbexa",
    "XCharter-Roman-tlf-t1",
    "XCharter-Italic-tlf-t1",
    "XCharter-Bold-tlf-t1",
    "XCharter-BoldItalic-tlf-t1",
    # Generic NewTX math italic remains useful as a comparison and fallback.
    "ntxmi",
    "ntxbmi",
    "ntxmi7",
    "ntxmi5",
    "ntxmia",
    "ntxbmia",
]

TEXT_CORE_SLOT_FONTS = {
    "XCharter-Roman-tlf-t1",
    "XCharter-Italic-tlf-t1",
    "XCharter-Bold-tlf-t1",
    "XCharter-BoldItalic-tlf-t1",
}

TEXT_CORE_SLOTS = set(range(32, 127))


@dataclass(frozen=True)
class FontFileIndex:
    tfm: dict[str, list[Path]]
    vf: dict[str, list[Path]]
    enc: dict[str, list[Path]]
    outline: dict[str, list[Path]]


@dataclass(frozen=True)
class TfmMetric:
    width: float
    height: float
    depth: float
    italic: float
    tag: int
    remainder: int


@dataclass(frozen=True)
class TfmKern:
    left: int
    right: int
    amount: float


@dataclass(frozen=True)
class TfmLigature:
    left: int
    right: int
    ligature: int


@dataclass(frozen=True)
class TfmLarger:
    code: int
    larger: int


@dataclass(frozen=True)
class TfmExtension:
    code: int
    top: int
    middle: int
    repeat: int
    bottom: int


@dataclass(frozen=True)
class TfmInfo:
    path: Path
    checksum: int
    design_size: float
    bc: int
    ec: int
    metrics: dict[int, TfmMetric]
    kerns: list[TfmKern]
    ligatures: list[TfmLigature]
    largers: list[TfmLarger]
    extensions: list[TfmExtension]
    params: list[float]


@dataclass(frozen=True)
class VfFontDef:
    local_id: int
    checksum: int
    scale: int
    design_size: int
    area: str
    name: str


@dataclass(frozen=True)
class VfPacket:
    code: int
    tfm_width: int
    dvi: bytes


@dataclass(frozen=True)
class VfInfo:
    path: Path
    checksum: int
    design_size: int
    font_defs: dict[int, VfFontDef]
    packets: list[VfPacket]


@dataclass(frozen=True)
class MapEntry:
    tex_name: str
    ps_name: str | None
    encoding: str | None
    outline: str | None
    map_path: Path
    encoding_path: Path | None
    outline_path: Path | None


@dataclass(frozen=True)
class GlyphEvent:
    local_font: int
    code: int
    x: int
    y: int
    op: str
    used_implicit_font: bool


@dataclass(frozen=True)
class PacketAnalysis:
    glyphs: list[GlyphEvent]
    rules: int
    specials: int
    unsupported: list[str]
    stack_errors: list[str]

    @property
    def flattenable(self) -> bool:
        return (
            len(self.glyphs) == 1
            and self.rules == 0
            and self.specials == 0
            and not self.unsupported
            and not self.stack_errors
        )

    @property
    def needs_outline_shift(self) -> bool:
        return (
            len(self.glyphs) == 1
            and (self.glyphs[0].x != 0 or self.glyphs[0].y != 0)
        )


def _u(data: bytes, offset: int, size: int) -> tuple[int, int]:
    return int.from_bytes(data[offset : offset + size], "big"), offset + size


def _s(data: bytes, offset: int, size: int) -> tuple[int, int]:
    value, offset = _u(data, offset, size)
    sign = 1 << (size * 8 - 1)
    if value & sign:
        value -= 1 << (size * 8)
    return value, offset


def _fix_word(value: int) -> float:
    if value & 0x80000000:
        value -= 0x100000000
    return value / float(1 << 20)


def build_file_index(root: Path) -> FontFileIndex:
    def collect(base: Path, suffix: str) -> dict[str, list[Path]]:
        found: dict[str, list[Path]] = defaultdict(list)
        if base.exists():
            for path in base.rglob(f"*{suffix}"):
                found[path.name].append(path)
        return dict(found)

    tfm = collect(root / "fonts" / "tfm" / "public", ".tfm")
    vf = collect(root / "fonts" / "vf" / "public", ".vf")
    enc = collect(root / "fonts" / "enc" / "dvips", ".enc")

    outline: dict[str, list[Path]] = defaultdict(list)
    for base in [
        root / "fonts" / "type1" / "public",
        root / "fonts" / "opentype" / "public",
        root / "fonts" / "truetype" / "public",
    ]:
        if base.exists():
            for suffix in ("*.pfb", "*.pfa", "*.otf", "*.ttf"):
                for path in base.rglob(suffix):
                    outline[path.name].append(path)

    return FontFileIndex(tfm=tfm, vf=vf, enc=enc, outline=dict(outline))


def choose_file(index: dict[str, list[Path]], filename: str) -> Path | None:
    paths = index.get(filename, [])
    if not paths:
        return None
    return sorted(paths, key=lambda p: (len(str(p)), str(p)))[0]


def parse_tfm(path: Path) -> TfmInfo:
    data = path.read_bytes()
    if len(data) < 24:
        raise ValueError(f"{path} is too small to be a TFM file")

    words = [int.from_bytes(data[i : i + 4], "big") for i in range(0, len(data), 4)]
    if len(words) < 6:
        raise ValueError(f"{path} has an incomplete TFM header")

    header = []
    for word in words[:6]:
        header.extend([(word >> 16) & 0xFFFF, word & 0xFFFF])

    lf, lh, bc, ec, nw, nh, nd, ni, nl, nk, ne, np = header[:12]
    if lf != len(words):
        raise ValueError(f"{path} length mismatch: header={lf} words actual={len(words)}")

    base = 6
    header_words = words[base : base + lh]
    char_info_start = base + lh
    char_count = max(0, ec - bc + 1)
    width_start = char_info_start + char_count
    height_start = width_start + nw
    depth_start = height_start + nh
    italic_start = depth_start + nd
    lig_kern_start = italic_start + ni
    kern_start = lig_kern_start + nl
    exten_start = kern_start + nk
    param_start = exten_start + ne

    checksum = header_words[0] if header_words else 0
    design_size = _fix_word(header_words[1]) if len(header_words) > 1 else 0.0

    widths = [_fix_word(word) for word in words[width_start : width_start + nw]]
    heights = [_fix_word(word) for word in words[height_start : height_start + nh]]
    depths = [_fix_word(word) for word in words[depth_start : depth_start + nd]]
    italics = [_fix_word(word) for word in words[italic_start : italic_start + ni]]
    kern_values = [_fix_word(word) for word in words[kern_start : kern_start + nk]]
    params = [_fix_word(word) for word in words[param_start : param_start + np]]

    metrics: dict[int, TfmMetric] = {}
    for idx, word in enumerate(words[char_info_start : char_info_start + char_count]):
        code = bc + idx
        width_index = (word >> 24) & 0xFF
        height_depth = (word >> 16) & 0xFF
        italic_tag = (word >> 8) & 0xFF
        remainder = word & 0xFF
        height_index = height_depth >> 4
        depth_index = height_depth & 0x0F
        italic_index = italic_tag >> 2
        tag = italic_tag & 0x03
        if (
            width_index == 0
            and height_index == 0
            and depth_index == 0
            and italic_index == 0
            and tag == 0
            and remainder == 0
        ):
            continue
        metrics[code] = TfmMetric(
            width=widths[width_index] if width_index < len(widths) else 0.0,
            height=heights[height_index] if height_index < len(heights) else 0.0,
            depth=depths[depth_index] if depth_index < len(depths) else 0.0,
            italic=italics[italic_index] if italic_index < len(italics) else 0.0,
            tag=tag,
            remainder=remainder,
        )

    lig_kern_words = words[lig_kern_start : lig_kern_start + nl]
    kerns: list[TfmKern] = []
    ligatures: list[TfmLigature] = []
    seen_kerns: set[tuple[int, int, float]] = set()
    seen_ligatures: set[tuple[int, int, int]] = set()

    def walk_lig_kern_program(left: int, start: int) -> None:
        if start < 0 or start >= len(lig_kern_words):
            return

        pos = start
        visited: set[int] = set()
        while 0 <= pos < len(lig_kern_words) and pos not in visited:
            visited.add(pos)
            word = lig_kern_words[pos]
            skip_byte = (word >> 24) & 0xFF
            next_char = (word >> 16) & 0xFF
            op_byte = (word >> 8) & 0xFF
            remainder = word & 0xFF

            if op_byte >= 128:
                kern_index = 256 * (op_byte - 128) + remainder
                if kern_index < len(kern_values):
                    item = (left, next_char, kern_values[kern_index])
                    if item not in seen_kerns:
                        seen_kerns.add(item)
                        kerns.append(TfmKern(*item))
            else:
                item = (left, next_char, remainder)
                if item not in seen_ligatures:
                    seen_ligatures.add(item)
                    ligatures.append(TfmLigature(*item))

            if skip_byte >= 128:
                break
            pos += skip_byte + 1

    for code, metric in sorted(metrics.items()):
        if metric.tag == 1:
            walk_lig_kern_program(code, metric.remainder)

    largers = [
        TfmLarger(code=code, larger=metric.remainder)
        for code, metric in sorted(metrics.items())
        if metric.tag == 2
    ]

    def part(code: int) -> int:
        return -1 if code == 0 else code

    extension_recipes: list[tuple[int, int, int, int]] = []
    for word in words[exten_start : exten_start + ne]:
        top = (word >> 24) & 0xFF
        middle = (word >> 16) & 0xFF
        bottom = (word >> 8) & 0xFF
        repeat = word & 0xFF
        extension_recipes.append((part(top), part(middle), part(repeat), part(bottom)))

    extensions = []
    for code, metric in sorted(metrics.items()):
        if metric.tag == 3 and metric.remainder < len(extension_recipes):
            top, middle, repeat, bottom = extension_recipes[metric.remainder]
            extensions.append(
                TfmExtension(
                    code=code,
                    top=top,
                    middle=middle,
                    repeat=repeat,
                    bottom=bottom,
                )
            )

    return TfmInfo(
        path=path,
        checksum=checksum,
        design_size=design_size,
        bc=bc,
        ec=ec,
        metrics=metrics,
        kerns=kerns,
        ligatures=ligatures,
        largers=largers,
        extensions=extensions,
        params=params,
    )


def parse_vf(path: Path) -> VfInfo:
    data = path.read_bytes()
    offset = 0
    op, offset = _u(data, offset, 1)
    if op != 247:
        raise ValueError(f"{path} does not start with a VF preamble")

    vf_id, offset = _u(data, offset, 1)
    if vf_id != 202:
        raise ValueError(f"{path} has unexpected VF id {vf_id}")

    comment_len, offset = _u(data, offset, 1)
    offset += comment_len
    checksum, offset = _u(data, offset, 4)
    design_size, offset = _u(data, offset, 4)

    font_defs: dict[int, VfFontDef] = {}
    packets: list[VfPacket] = []

    while offset < len(data):
        op, offset = _u(data, offset, 1)
        if op <= 241:
            packet_len = op
            code, offset = _u(data, offset, 1)
            tfm_width, offset = _u(data, offset, 3)
            packet = data[offset : offset + packet_len]
            offset += packet_len
            packets.append(VfPacket(code=code, tfm_width=tfm_width, dvi=packet))
        elif op == 242:
            packet_len, offset = _u(data, offset, 4)
            code, offset = _u(data, offset, 4)
            tfm_width, offset = _u(data, offset, 4)
            packet = data[offset : offset + packet_len]
            offset += packet_len
            packets.append(VfPacket(code=code, tfm_width=tfm_width, dvi=packet))
        elif 243 <= op <= 246:
            key_len = op - 242
            local_id, offset = _u(data, offset, key_len)
            font_checksum, offset = _u(data, offset, 4)
            scale, offset = _u(data, offset, 4)
            font_design_size, offset = _u(data, offset, 4)
            area_len, offset = _u(data, offset, 1)
            name_len, offset = _u(data, offset, 1)
            area = data[offset : offset + area_len].decode("latin1")
            offset += area_len
            name = data[offset : offset + name_len].decode("latin1")
            offset += name_len
            font_defs[local_id] = VfFontDef(
                local_id=local_id,
                checksum=font_checksum,
                scale=scale,
                design_size=font_design_size,
                area=area,
                name=name,
            )
        elif op == 248:
            break
        else:
            raise ValueError(f"{path} has unexpected top-level VF opcode {op}")

    return VfInfo(
        path=path,
        checksum=checksum,
        design_size=design_size,
        font_defs=font_defs,
        packets=packets,
    )


def parse_packet(packet: bytes) -> PacketAnalysis:
    offset = 0
    current_font = 0
    explicit_font_seen = False
    glyphs: list[GlyphEvent] = []
    rules = 0
    specials = 0
    unsupported: list[str] = []
    stack_errors: list[str] = []
    x = 0
    y = 0
    w = 0
    x_reg = 0
    y_reg = 0
    z = 0
    stack: list[tuple[int, int, int, int, int, int, int, bool]] = []

    def read_u(size: int) -> int:
        nonlocal offset
        value, offset = _u(packet, offset, size)
        return value

    def read_s(size: int) -> int:
        nonlocal offset
        value, offset = _s(packet, offset, size)
        return value

    while offset < len(packet):
        op = read_u(1)
        if op <= 127:
            glyphs.append(GlyphEvent(current_font, op, x, y, "set", not explicit_font_seen))
        elif 128 <= op <= 131:
            glyphs.append(
                GlyphEvent(
                    current_font,
                    read_u(op - 127),
                    x,
                    y,
                    "set",
                    not explicit_font_seen,
                )
            )
        elif op == 132:
            read_s(4)
            read_s(4)
            rules += 1
        elif 133 <= op <= 136:
            glyphs.append(
                GlyphEvent(
                    current_font,
                    read_u(op - 132),
                    x,
                    y,
                    "put",
                    not explicit_font_seen,
                )
            )
        elif op == 137:
            read_s(4)
            read_s(4)
            rules += 1
        elif op == 138:
            continue
        elif op == 141:
            stack.append((x, y, w, x_reg, y_reg, z, current_font, explicit_font_seen))
        elif op == 142:
            if not stack:
                stack_errors.append("pop with empty stack")
                continue
            x, y, w, x_reg, y_reg, z, current_font, explicit_font_seen = stack.pop()
        elif 143 <= op <= 146:
            x += read_s(op - 142)
        elif op == 147:
            x += w
        elif 148 <= op <= 151:
            w = read_s(op - 147)
            x += w
        elif op == 152:
            x += x_reg
        elif 153 <= op <= 156:
            x_reg = read_s(op - 152)
            x += x_reg
        elif 157 <= op <= 160:
            y += read_s(op - 156)
        elif op == 161:
            y += y_reg
        elif 162 <= op <= 165:
            y_reg = read_s(op - 161)
            y += y_reg
        elif op == 166:
            y += z
        elif 167 <= op <= 170:
            z = read_s(op - 166)
            y += z
        elif 171 <= op <= 234:
            current_font = op - 171
            explicit_font_seen = True
        elif 235 <= op <= 238:
            current_font = read_u(op - 234)
            explicit_font_seen = True
        elif 239 <= op <= 242:
            special_len = read_u(op - 238)
            offset += special_len
            specials += 1
        elif 243 <= op <= 246:
            key_len = op - 242
            read_u(key_len)
            read_u(4)
            read_u(4)
            read_u(4)
            area_len = read_u(1)
            name_len = read_u(1)
            offset += area_len + name_len
        else:
            unsupported.append(f"opcode {op}")
            break

    if stack:
        stack_errors.append(f"{len(stack)} unclosed push frame(s)")

    return PacketAnalysis(
        glyphs=glyphs,
        rules=rules,
        specials=specials,
        unsupported=unsupported,
        stack_errors=stack_errors,
    )


def parse_map_files(root: Path, index: FontFileIndex) -> dict[str, MapEntry]:
    entries: dict[str, MapEntry] = {}
    map_root = root / "fonts" / "map" / "dvips"
    if not map_root.exists():
        return entries

    for path in sorted(map_root.rglob("*.map")):
        for raw_line in path.read_text(encoding="latin1").splitlines():
            line = raw_line.strip()
            if not line or line.startswith(("%", "#")):
                continue
            try:
                tokens = shlex.split(line, comments=True, posix=True)
            except ValueError:
                continue
            if not tokens:
                continue

            tex_name = tokens[0]
            ps_name = None
            encoding = None
            outline = None
            for token in tokens[1:]:
                cleaned = token.lstrip("<[")
                if cleaned.endswith((".enc", ".ENC")):
                    encoding = Path(cleaned).name
                elif cleaned.endswith((".pfb", ".pfa", ".otf", ".ttf")):
                    outline = Path(cleaned).name
                elif ps_name is None and not token.startswith(("<", "\"")):
                    ps_name = token

            entries[tex_name] = MapEntry(
                tex_name=tex_name,
                ps_name=ps_name,
                encoding=encoding,
                outline=outline,
                map_path=path,
                encoding_path=choose_file(index.enc, encoding) if encoding else None,
                outline_path=choose_file(index.outline, outline) if outline else None,
            )

    return entries


def parse_encoding(path: Path | None) -> list[str] | None:
    if path is None:
        return None
    text = path.read_text(encoding="latin1")
    lines = []
    for line in text.splitlines():
        lines.append(line.split("%", 1)[0])
    cleaned = "\n".join(lines)
    start = cleaned.find("[")
    end = cleaned.find("]", start + 1)
    if start < 0 or end < 0:
        return None
    vector = []
    for token in cleaned[start + 1 : end].replace("\n", " ").split():
        if token.startswith("/"):
            vector.append(token[1:])
    return vector


def glyph_name_for(entry: MapEntry | None, code: int, enc_cache: dict[Path, list[str] | None]) -> str | None:
    if entry is None or entry.encoding_path is None:
        return None
    if entry.encoding_path not in enc_cache:
        enc_cache[entry.encoding_path] = parse_encoding(entry.encoding_path)
    vector = enc_cache[entry.encoding_path]
    if vector is None or code >= len(vector):
        return None
    return vector[code]


def analyze_font(
    name: str,
    index: FontFileIndex,
    map_entries: dict[str, MapEntry],
    enc_cache: dict[Path, list[str] | None],
    all_slots_required: bool,
) -> dict[str, object]:
    tfm_path = choose_file(index.tfm, f"{name}.tfm")
    vf_path = choose_file(index.vf, f"{name}.vf")

    if tfm_path is None and vf_path is None:
        return {"name": name, "status": "missing", "error": "no TFM or VF found"}

    tfm = parse_tfm(tfm_path) if tfm_path else None

    if vf_path is None:
        map_entry = map_entries.get(name)
        return {
            "name": name,
            "status": "physical",
            "tfm": str(tfm_path) if tfm_path else None,
            "tfm_slots": len(tfm.metrics) if tfm else 0,
            "map": map_summary(map_entry),
        }

    vf = parse_vf(vf_path)
    packet_results = [(packet, parse_packet(packet.dvi)) for packet in vf.packets]
    required_slots = None if all_slots_required else required_slots_for(name)
    required_packet_results = [
        item for item in packet_results
        if required_slots is None or item[0].code in required_slots
    ]
    flattenable = [item for item in packet_results if item[1].flattenable]
    unflattenable = [item for item in packet_results if not item[1].flattenable]
    required_flattenable = [
        item for item in required_packet_results if item[1].flattenable
    ]
    required_unflattenable = [
        item for item in required_packet_results if not item[1].flattenable
    ]
    extra_unflattenable = [
        item for item in unflattenable
        if item not in required_unflattenable
    ]
    offset_slots = [item for item in flattenable if item[1].needs_outline_shift]
    required_offset_slots = [
        item for item in required_flattenable if item[1].needs_outline_shift
    ]
    implicit_font_slots = [
        item for item in flattenable if item[1].glyphs[0].used_implicit_font
    ]

    local_font_use: Counter[int] = Counter()
    physical_code_max = 0
    missing_local_defs: set[int] = set()
    missing_map_fonts: set[str] = set()

    for _packet, analysis in flattenable:
        glyph = analysis.glyphs[0]
        local_font_use[glyph.local_font] += 1
        physical_code_max = max(physical_code_max, glyph.code)
        if glyph.local_font not in vf.font_defs:
            missing_local_defs.add(glyph.local_font)
            continue
        font_name = vf.font_defs[glyph.local_font].name
        if font_name not in map_entries:
            missing_map_fonts.add(font_name)

    sample_mappings = []
    sample_codes = sorted({0, 1, 2, 40, 41, 43, 48, 49, 65, 97, 120, 128, 185, 186})
    by_code = {packet.code: (packet, analysis) for packet, analysis in packet_results}
    for code in sample_codes:
        item = by_code.get(code)
        if item is None:
            continue
        packet, analysis = item
        sample_mappings.append(sample_mapping(packet, analysis, vf, map_entries, enc_cache))
        if len(sample_mappings) >= 12:
            break
    if len(sample_mappings) < 8:
        for packet, analysis in packet_results:
            if any(sample["slot"] == packet.code for sample in sample_mappings):
                continue
            sample_mappings.append(sample_mapping(packet, analysis, vf, map_entries, enc_cache))
            if len(sample_mappings) >= 12:
                break

    status = "pass" if not required_unflattenable and not missing_local_defs else "fail"
    if missing_map_fonts:
        status = "needs-map"

    return {
        "name": name,
        "status": status,
        "tfm": str(tfm_path) if tfm_path else None,
        "vf": str(vf_path),
        "tfm_slots": len(tfm.metrics) if tfm else None,
        "tfm_range": [tfm.bc, tfm.ec] if tfm else None,
        "vf_packets": len(vf.packets),
        "required_slot_policy": required_slot_policy_name(name, required_slots),
        "required_packets": len(required_packet_results),
        "required_flattenable": len(required_flattenable),
        "required_unflattenable": len(required_unflattenable),
        "flattenable": len(flattenable),
        "unflattenable": len(unflattenable),
        "extra_unflattenable": len(extra_unflattenable),
        "needs_outline_shift": len(offset_slots),
        "required_needs_outline_shift": len(required_offset_slots),
        "implicit_font0_slots": len(implicit_font_slots),
        "physical_code_max": physical_code_max,
        "local_font_use": dict(sorted(local_font_use.items())),
        "missing_local_defs": sorted(missing_local_defs),
        "missing_map_fonts": sorted(missing_map_fonts),
        "physical_fonts": [
            {
                "local_id": local_id,
                "name": font_def.name,
                "scale": font_def.scale,
                "design_size": font_def.design_size,
                "map": map_summary(map_entries.get(font_def.name)),
            }
            for local_id, font_def in sorted(vf.font_defs.items())
        ],
        "unflattenable_examples": [
            unflattenable_summary(packet, analysis, vf)
            for packet, analysis in unflattenable[:8]
        ],
        "extra_unflattenable_examples": [
            unflattenable_summary(packet, analysis, vf)
            for packet, analysis in extra_unflattenable[:8]
        ],
        "sample_mappings": sample_mappings,
    }


def required_slots_for(name: str) -> set[int] | None:
    if name in TEXT_CORE_SLOT_FONTS:
        return TEXT_CORE_SLOTS
    return None


def required_slot_policy_name(name: str, required_slots: set[int] | None) -> str:
    if required_slots is None:
        return "all VF packets"
    if name in TEXT_CORE_SLOT_FONTS:
        return "ASCII operator/upright slots 32-126"
    return f"{len(required_slots)} configured slots"


def map_summary(entry: MapEntry | None) -> dict[str, str | None] | None:
    if entry is None:
        return None
    return {
        "ps_name": entry.ps_name,
        "encoding": entry.encoding,
        "encoding_path": str(entry.encoding_path) if entry.encoding_path else None,
        "outline": entry.outline,
        "outline_path": str(entry.outline_path) if entry.outline_path else None,
        "map_path": str(entry.map_path),
    }


def sample_mapping(
    packet: VfPacket,
    analysis: PacketAnalysis,
    vf: VfInfo,
    map_entries: dict[str, MapEntry],
    enc_cache: dict[Path, list[str] | None],
) -> dict[str, object]:
    base = {"slot": packet.code, "flattenable": analysis.flattenable}
    if not analysis.flattenable or not analysis.glyphs:
        base["reason"] = unflattenable_summary(packet, analysis, vf)
        return base
    glyph = analysis.glyphs[0]
    font_def = vf.font_defs.get(glyph.local_font)
    font_name = font_def.name if font_def else None
    map_entry = map_entries.get(font_name) if font_name else None
    base.update(
        {
            "physical_font": font_name,
            "physical_code": glyph.code,
            "glyph_name": glyph_name_for(map_entry, glyph.code, enc_cache),
            "x": glyph.x,
            "y": glyph.y,
            "op": glyph.op,
            "implicit_font0": glyph.used_implicit_font,
        }
    )
    return base


def unflattenable_summary(packet: VfPacket, analysis: PacketAnalysis, vf: VfInfo) -> dict[str, object]:
    return {
        "slot": packet.code,
        "glyph_count": len(analysis.glyphs),
        "rules": analysis.rules,
        "specials": analysis.specials,
        "unsupported": analysis.unsupported,
        "stack_errors": analysis.stack_errors,
        "glyphs": [
            {
                "local_font": glyph.local_font,
                "physical_font": vf.font_defs.get(glyph.local_font).name
                if glyph.local_font in vf.font_defs
                else None,
                "code": glyph.code,
                "x": glyph.x,
                "y": glyph.y,
                "op": glyph.op,
            }
            for glyph in analysis.glyphs[:4]
        ],
    }


def analyze_all(root: Path, fonts: list[str], all_slots_required: bool) -> dict[str, object]:
    index = build_file_index(root)
    map_entries = parse_map_files(root, index)
    enc_cache: dict[Path, list[str] | None] = {}
    results = [
        analyze_font(name, index, map_entries, enc_cache, all_slots_required)
        for name in fonts
    ]
    totals = Counter()
    for result in results:
        totals["fonts"] += 1
        if result["status"] == "missing":
            totals["missing_fonts"] += 1
            continue
        totals["vf_packets"] += int(result.get("vf_packets") or 0)
        totals["required_vf_packets"] += int(result.get("required_packets") or 0)
        totals["required_flattenable"] += int(result.get("required_flattenable") or 0)
        totals["required_unflattenable"] += int(result.get("required_unflattenable") or 0)
        totals["flattenable"] += int(result.get("flattenable") or 0)
        totals["unflattenable"] += int(result.get("unflattenable") or 0)
        totals["extra_unflattenable"] += int(result.get("extra_unflattenable") or 0)
        totals["needs_outline_shift"] += int(result.get("needs_outline_shift") or 0)
        totals["required_needs_outline_shift"] += int(result.get("required_needs_outline_shift") or 0)
        totals["missing_map_fonts"] += len(result.get("missing_map_fonts") or [])

    return {
        "generated_at": _datetime.datetime.now(_datetime.UTC).isoformat(),
        "miktex_root": str(root),
        "all_slots_required": all_slots_required,
        "fonts": fonts,
        "totals": dict(totals),
        "results": results,
    }


def render_markdown(report: dict[str, object]) -> str:
    totals = report["totals"]
    result_lines = [
        "# NewTX/XCharter VF Flattening Probe",
        "",
        f"Generated: `{report['generated_at']}`",
        "",
        f"MiKTeX root: `{report['miktex_root']}`",
        "",
        "## Result",
        "",
    ]

    required_unflattenable = int(totals.get("required_unflattenable", 0))
    missing = int(totals.get("missing_fonts", 0))
    missing_maps = int(totals.get("missing_map_fonts", 0))
    if required_unflattenable == 0 and missing == 0 and missing_maps == 0:
        result_lines.append(
            "PASS: every required virtual slot collapses to exactly one physical glyph."
        )
    else:
        result_lines.append(
            "BLOCKED: at least one required font is missing, lacks a map entry, or has a non-single-glyph packet."
        )
    result_lines.extend(
        [
            "",
            f"- Fonts checked: `{totals.get('fonts', 0)}`",
            f"- Required VF packets checked: `{totals.get('required_vf_packets', 0)}`",
            f"- Required flattenable packets: `{totals.get('required_flattenable', 0)}`",
            f"- Required unflattenable packets: `{totals.get('required_unflattenable', 0)}`",
            f"- Required packets needing outline-origin shifts: `{totals.get('required_needs_outline_shift', 0)}`",
            f"- Total VF packets scanned: `{totals.get('vf_packets', 0)}`",
            f"- Total unflattenable non-required packets: `{totals.get('extra_unflattenable', 0)}`",
            f"- Missing font files: `{totals.get('missing_fonts', 0)}`",
            f"- Missing map references: `{totals.get('missing_map_fonts', 0)}`",
            "",
            "A packet that needs an outline-origin shift is still flattenable, but a generated physical font must bake that per-slot offset into the copied outline. MicroTeX has no per-glyph VF offset instruction.",
            "",
            "For XCharter text fonts, the required slot policy is limited to ASCII operator/upright slots 32-126; broader T1 text-only ligature and mark slots are reported but do not block the math-port gate.",
            "",
            "## Fonts",
            "",
            "| Font | Status | Required policy | Required | Shifted | All unflattenable | Physical fonts |",
            "| --- | --- | --- | ---: | ---: | ---: | --- |",
        ]
    )

    for result in report["results"]:
        physical = ", ".join(
            font["name"] for font in result.get("physical_fonts", [])  # type: ignore[union-attr]
        )
        result_lines.append(
            "| `{name}` | `{status}` | {policy} | {required} | {shifted} | {unflattenable} | {physical} |".format(
                name=result["name"],
                status=result["status"],
                policy=result.get("required_slot_policy", ""),
                required="{}/{}".format(
                    result.get("required_flattenable", ""),
                    result.get("required_packets", ""),
                ),
                shifted=result.get("required_needs_outline_shift", ""),
                unflattenable=result.get("unflattenable", ""),
                physical=physical or "",
            )
        )

    result_lines.extend(["", "## Map Coverage", ""])
    for result in report["results"]:
        if result["status"] == "missing":
            result_lines.extend([f"### `{result['name']}`", "", str(result.get("error", "")), ""])
            continue
        missing_map_fonts = result.get("missing_map_fonts") or []
        if missing_map_fonts:
            result_lines.extend(
                [
                    f"### `{result['name']}`",
                    "",
                    "Missing map entries: "
                    + ", ".join(f"`{font}`" for font in missing_map_fonts),
                    "",
                ]
            )

    extra_examples = [
        result for result in report["results"]
        if result.get("extra_unflattenable_examples")
    ]
    if extra_examples:
        result_lines.extend(["## Non-Required Unflattenable Slots", ""])
        for result in extra_examples:
            result_lines.extend([f"### `{result['name']}`", ""])
            for example in result.get("extra_unflattenable_examples") or []:
                result_lines.append(
                    "- Slot `{slot}`: glyphs=`{glyphs}`, rules=`{rules}`, specials=`{specials}`, unsupported=`{unsupported}`".format(
                        slot=example["slot"],
                        glyphs=example["glyph_count"],
                        rules=example["rules"],
                        specials=example["specials"],
                        unsupported=",".join(example["unsupported"]),
                    )
                )
            result_lines.append("")

    result_lines.extend(["## Sample Mappings", ""])
    for result in report["results"]:
        samples = result.get("sample_mappings") or []
        if not samples:
            continue
        result_lines.extend(
            [
                f"### `{result['name']}`",
                "",
                "| Slot | Physical font | Physical code | Glyph name | Offset |",
                "| ---: | --- | ---: | --- | --- |",
            ]
        )
        for sample in samples:  # type: ignore[assignment]
            if not sample.get("flattenable"):
                result_lines.append(
                    f"| {sample['slot']} | unflattenable |  |  |  |"
                )
                continue
            glyph_name = sample.get("glyph_name") or ""
            result_lines.append(
                "| {slot} | `{font}` | {code} | {glyph} | `{x},{y}` |".format(
                    slot=sample["slot"],
                    font=sample.get("physical_font") or "",
                    code=sample["physical_code"] if "physical_code" in sample else "",
                    glyph=glyph_name,
                    x=sample["x"] if "x" in sample else 0,
                    y=sample["y"] if "y" in sample else 0,
                )
            )
        result_lines.append("")

    result_lines.extend(
        [
            "## Interpretation",
            "",
            "- The probed VF layer is not the blocker: the selected fonts use one physical glyph per virtual math slot.",
            "- XCharter text VFs do contain a few text-only composite/rule slots outside the required math operator range; these should be ignored unless Kvit later wants full T1 text shaping through MicroTeX.",
            "- The generated font step still has to reencode outlines into the virtual TeX slots and bake in the listed offsets.",
            "- Slots backed by map entries without an external `.enc` vector report physical codes but not glyph names; a later outline copier can still select by code if the conversion preserves the source encoding.",
            "- This does not address licensing or vendoring. It only proves the mechanical VF flattening shape against the local MiKTeX install.",
            "",
        ]
    )

    return "\n".join(result_lines)


def parse_args(argv: Iterable[str]) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--miktex-root",
        type=Path,
        default=DEFAULT_MIKTEX_ROOT,
        help=f"MiKTeX root to inspect (default: {DEFAULT_MIKTEX_ROOT})",
    )
    parser.add_argument(
        "--font",
        action="append",
        dest="fonts",
        help="TeX font name to inspect. May be repeated. Defaults to the NewTX/XCharter target set.",
    )
    parser.add_argument(
        "--json",
        action="store_true",
        help="Emit JSON instead of Markdown.",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Write output to this file instead of stdout.",
    )
    parser.add_argument(
        "--fail-on-blocked",
        action="store_true",
        help="Exit non-zero if any probed VF packet is not flattenable or map coverage is missing.",
    )
    parser.add_argument(
        "--all-slots-required",
        action="store_true",
        help="Treat every VF packet as required, including text-only T1 slots in XCharter text fonts.",
    )
    return parser.parse_args(list(argv))


def main(argv: Iterable[str]) -> int:
    args = parse_args(argv)
    fonts = args.fonts if args.fonts else DEFAULT_TARGET_FONTS
    report = analyze_all(args.miktex_root, fonts, args.all_slots_required)
    output = json.dumps(report, indent=2, sort_keys=True) if args.json else render_markdown(report)

    if args.output:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(output, encoding="utf-8")
    else:
        print(output)

    totals = report["totals"]
    blocked = (
        int(totals.get("missing_fonts", 0)) > 0
        or int(totals.get("required_unflattenable", 0)) > 0
        or int(totals.get("missing_map_fonts", 0)) > 0
    )
    return 1 if args.fail_on_blocked and blocked else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
