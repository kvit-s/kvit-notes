#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Generate THIRD-PARTY-NOTICES.md from packaging/sbom.yaml.

The manifest is the single source of truth (launch-plan.md A2.3); the
notices file is committed generated output. Run from anywhere:

    tools/generate-notices.py            # rewrite THIRD-PARTY-NOTICES.md
    tools/generate-notices.py --check    # exit 1 if the file is stale
"""

import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
HEADER = """\
# Third-party notices

Kvit Notes is licensed under the Mozilla Public License 2.0 (see LICENSE).
It vendors, links, or ships the third-party components below. This file is
generated from `packaging/sbom.yaml` by `tools/generate-notices.py`; edit the
manifest, not this file.

"""


def render() -> str:
    manifest = yaml.safe_load((ROOT / "packaging/sbom.yaml").read_text())
    out = [HEADER]
    for c in manifest["components"]:
        shipped = ", ".join(c.get("ships_in") or []) or "not shipped"
        out.append(f"## {c['name']}\n\n")
        out.append(f"- **Version:** {c['version']}\n")
        out.append(f"- **License:** {c['spdx']}\n")
        out.append(f"- **Origin:** {c['origin']}\n")
        out.append(f"- **Files:** {c['files']}\n")
        out.append(f"- **License text:** {c['license_file']}\n")
        out.append(f"- **Ships in:** {shipped}\n")
        if c.get("obligations"):
            out.append(f"- **Obligations:** {c['obligations']}\n")
        if c.get("flag"):
            out.append(f"- **Open flag:** {c['flag']}\n")
        out.append("\n")
    return "".join(out).rstrip() + "\n"


def main() -> int:
    target = ROOT / "THIRD-PARTY-NOTICES.md"
    content = render()
    if "--check" in sys.argv[1:]:
        if not target.exists() or target.read_text() != content:
            print("THIRD-PARTY-NOTICES.md is stale; rerun tools/generate-notices.py")
            return 1
        print("THIRD-PARTY-NOTICES.md is up to date")
        return 0
    target.write_text(content)
    print(f"wrote {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
