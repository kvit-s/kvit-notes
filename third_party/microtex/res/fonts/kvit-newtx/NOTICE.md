# Kvit NewTX/XCharter Math Fonts — Provenance and Licensing

The `kvit_newtx_*.otf` fonts in this directory are generated derivatives of
TeX fonts from the CTAN `newtx` and `xcharter` bundles by Michael Sharpe.
They reproduce, inside the MicroTeX engine, the math rendering of the LaTeX
preamble:

```tex
\usepackage[T1]{fontenc}
\usepackage{XCharter}
\usepackage[charter]{newtxmath}
```

Each OTF flattens virtual-font slots into single glyphs: outlines are copied
from the source Type 1 fonts, rescaled, and offset per the TeX virtual-font
mappings. Per-font provenance (the exact source outline files) is embedded in
each OTF's name table (description, name ID 10), together with copyright
notices (ID 0) and license references (IDs 13/14).

## Sources and licenses

- **newtx bundle** (https://ctan.org/pkg/newtx) — The LaTeX Project Public
  License 1.3 (`LICENSES/LPPL-1.3c.txt`). Modified/derived files are renamed
  (`kvit_newtx_*`), satisfying LPPL's identification requirement.
- **XCharter text and math outlines** (`XCharter-*.pfb`,
  `XCharterMath*.pfb`) — Bitstream Charter grant
  (`LICENSES/BITSTREAM-CHARTER.txt`): Copyright 1990/1989-1992 Bitstream
  Inc.; use, modification, sublicensing, sale, and redistribution permitted
  without restriction provided the notice stays intact. BITSTREAM CHARTER is
  a registered trademark of Bitstream Inc. Additions by Andrey V. Panov and
  Michael Sharpe.
- **txfonts-lineage symbol/extension outlines** (`txsys`, `txbsys`, `txexs`,
  `txbexs`, `txexas`, `txbexas`, `txmiaX`, `txbmiaX`, and `NewTXMI*`) —
  the newtx bundle and these files are distributed under LPPL 1.3
  (`LICENSES/LPPL-1.3c.txt`). Some source files retain historical embedded
  headers reading "GPL" (txfonts lineage by Young Ryu; `NewTXMI` states
  "Derived from TX fonts and GNUFreeFont, GNU 3.0"). The licensing review
  confirmed that those embedded headers are stale and do not change the
  LPPL 1.3 licensing of the newtx work or these generated derivatives.
- **STIX script/blackboard glyphs** (`stxscr`, `txmiaSTbb`, `txbmiaSTbb`) —
  Copyright (c) 2001-2011 by the STI Pub Companies, SIL Open Font License
  1.1 (`LICENSES/OFL-1.1.txt`). The generated fonts do not use the reserved
  font name "STIX".

## Source availability

The complete, preferred form for modification:

- Source Type 1 outlines and TFM/VF metrics: the public CTAN `newtx` and
  `xcharter` packages (https://ctan.org/pkg/newtx,
  https://ctan.org/pkg/xcharter), also present in any TeX distribution
  (these fonts were generated from MiKTeX's copies).
- Generation pipeline (in this repository):
  `tools/generate_newtx_microtex.py` (metric tables and `outline_plan.json`)
  and `tools/build_newtx_outline_fonts.py` (OTF assembly). Regeneration:

  ```sh
  python3 tools/generate_newtx_microtex.py \
    --output-dir build/generated/newtx-charter-microtex --fail-on-blocked
  python3 tools/build_newtx_outline_fonts.py \
    --plan build/generated/newtx-charter-microtex/outline_plan.json --force
  ```

The corresponding generated metric tables live at
`third_party/microtex/src/res/font/newtx-generated/` (numeric TFM-derived
metrics compiled into the application).

Recorded decision (2026-07-09, `tex-mathdesign-progress.md` Licensing
Record): the licensing review confirmed the newtx-derived corpus is
distributed under LPPL 1.3; the embedded GPL headers are stale.
