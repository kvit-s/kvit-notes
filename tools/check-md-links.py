#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Broken-link scan (export gate 2): every relative markdown link and image
in the tree must resolve to an existing file. External (http/https/mailto)
and pure-anchor links are skipped. Usage: check-md-links.py <tree>."""
import re
import sys
from pathlib import Path

LINK = re.compile(r"!?\[[^\]]*\]\(([^)\s]+)(?:\s+\"[^\"]*\")?\)")
FENCE = re.compile(r"^(```|~~~).*?^\1\s*$", re.M | re.S)
CODE_SPAN = re.compile(r"`[^`\n]*`")

root = Path(sys.argv[1]).resolve()
bad = []
for md in root.rglob("*.md"):
    if ".git" in md.parts or "third_party" in md.parts:
        continue
    text = md.read_text(encoding="utf-8", errors="replace")
    # Syntax examples inside code fences and spans are not links.
    text = CODE_SPAN.sub("", FENCE.sub("", text))
    for target in LINK.findall(text):
        if target.startswith(("http://", "https://", "mailto:", "#")):
            continue
        path = target.split("#", 1)[0]
        if not path:
            continue
        if not (md.parent / path).exists():
            bad.append(f"{md.relative_to(root)}: broken link -> {target}")
for b in bad:
    print(b)
print(f"{'FAIL' if bad else 'OK'}: {len(bad)} broken link(s)")
sys.exit(1 if bad else 0)
