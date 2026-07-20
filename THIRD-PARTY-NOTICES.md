# Third-party notices

Kvit Notes is licensed under the Mozilla Public License 2.0 (see LICENSE).
It vendors, links, or ships the third-party components below. This file is
generated from `packaging/sbom.yaml` by `tools/generate-notices.py`; edit the
manifest, not this file.

## MicroTeX (cLaTeXMath)

- **Version:** pinned commit 0e3707f6
- **License:** MIT
- **Origin:** https://github.com/NanoMichael/cLaTeXMath
- **Files:** third_party/microtex/src, third_party/microtex/res (excluding the font sub-licenses listed separately below)
- **License text:** third_party/microtex/LICENSE.clatexmath
- **Ships in:** windows, macos, linux, source
- **Obligations:** Preserve the copyright notice and license text.

## tinyxml2

- **Version:** vendored with cLaTeXMath
- **License:** Zlib
- **Origin:** https://github.com/leethomason/tinyxml2
- **Files:** third_party/microtex/tinyxml2
- **License text:** third_party/microtex/tinyxml2/LICENSE.txt; installed into every binary artifact as LICENSE.tinyxml2
- **Ships in:** windows, macos, linux, source
- **Obligations:** Keep the notice; altered sources must be marked.

## TeX math fonts vendored by cLaTeXMath (base, euler, latin, maths)

- **Version:** vendored with cLaTeXMath
- **License:** Knuth-CTAN AND OFL-1.1 AND LicenseRef-dsrom
- **Origin:** CTAN via cLaTeXMath
- **Files:** third_party/microtex/res/fonts/{base,euler,latin,maths}
- **License text:** third_party/microtex/res/fonts/licences/ (Knuth_License.txt, OFL.txt, License_for_dsrom.txt)
- **Ships in:** windows, macos, linux, source
- **Obligations:** Ship the license files beside the fonts (the installed math-res tree carries them); OFL forbids selling the fonts standalone.

## kvit-newtx math fonts (NewTX / XCharter derivatives)

- **Version:** generated 2026-07 from CTAN newtx + xcharter by Michael Sharpe
- **License:** LPPL-1.3c AND LicenseRef-Bitstream-Charter AND OFL-1.1
- **Origin:** https://ctan.org/pkg/newtx, https://ctan.org/pkg/xcharter; generation tooling tools/generate_newtx_microtex.py
- **Files:** third_party/microtex/res/fonts/kvit-newtx
- **License text:** third_party/microtex/res/fonts/kvit-newtx/LICENSES, NOTICE.md (authoritative provenance; the naming and attribution checklist was executed during the math port)
- **Ships in:** windows, macos, linux, source
- **Obligations:** LPPL renaming requirement satisfied by the kvit_newtx_* names; keep NOTICE.md and LICENSES with the fonts; Bitstream Charter notice must stay intact. The generated fonts also fold in STIX script and blackboard-bold glyphs (stxscr, txmiaSTbb, txbmiaSTbb, reaching users inside kvit_newtx_ntxmia and kvit_newtx_ntxbmia) under OFL-1.1, so LICENSES/OFL-1.1.txt must ship with them, the STI Pub Companies copyright must stay intact, and the fonts must not be sold on their own. The OFL reserved name "STIX" is not used by the generated fonts, which is what permits the kvit_newtx_* renaming.

## Greek alphabet fonts for MicroTeX

- **Version:** vendored with cLaTeXMath
- **License:** GPL-3.0-only
- **Origin:** CTAN (Greek TeX fonts) via cLaTeXMath
- **Files:** third_party/microtex/res/greek
- **License text:** third_party/microtex/res/greek/LICENSE
- **Ships in:** windows, macos, linux, source
- **Obligations:** Ship the GPL-3 text with the fonts (the installed math-res tree carries res/greek/LICENSE, verified in the Linux install and AppImage layouts). Owner ruling 2026-07-19: shipped as aggregation - plain GPL-3, no font-embedding exception, accepted because the fonts are standalone data files, the app rasterizes math in-process, and no font is ever embedded into exported documents. Serves literal Cyrillic/polytonic-Greek characters inside formulas; TeX math Greek commands come from the math fonts and never touch this pack.

## Cyrillic alphabet fonts for MicroTeX

- **Version:** vendored with cLaTeXMath
- **License:** GPL-3.0-only
- **Origin:** CTAN (Washington Cyrillic) via cLaTeXMath
- **Files:** third_party/microtex/res/cyrillic
- **License text:** third_party/microtex/res/cyrillic/LICENSE
- **Ships in:** windows, macos, linux, source
- **Obligations:** Ship the GPL-3 text with the fonts (res/cyrillic/LICENSE travels with the installed math-res tree). Same 2026-07-19 owner ruling and aggregation rationale as the Greek pack above.

## Qt

- **Version:** 6.10.1
- **License:** LGPL-3.0-only
- **Origin:** https://www.qt.io (aqtinstall binaries in CI)
- **Files:** dynamically linked; runtime libraries and plugins bundled by windeployqt / macdeployqt / linuxdeploy at packaging time
- **License text:** packaging/licenses/qt holds the canonical LGPL-3 and GPL-3 texts; packaging copies them into each artifact under share/licenses/kvit-notes/qt, alongside the Qt kit's own licensing document when the kit provides one
- **Ships in:** windows, macos, linux
- **Obligations:** See packaging/qt-lgpl-checklist.md - dynamic linking only, license texts in every artifact, source-availability statement, relink/replace ability preserved (no static Qt).

## Qt Multimedia FFmpeg backend

- **Version:** FFmpeg 7.1 (libavcodec 61.19.101) as shipped with Qt 6.10.1
- **License:** LGPL-2.1-or-later
- **Origin:** Qt-provided ffmpeg plugin (qtmultimedia)
- **Files:** libavcodec.so.61, libavformat.so.61, libavutil.so.59, libswresample.so.5, libswscale.so.8 and plugins/multimedia/ libffmpegmediaplugin.so, deployed by linuxdeploy-plugin-qt into the Linux AppImage; the equivalent set on the other platforms once they are packaged
- **License text:** covered by the Qt licenses folder in each artifact (share/licenses/kvit-notes/qt)
- **Ships in:** windows, macos, linux
- **Obligations:** LGPL dynamic linking only, license texts in the artifact, relink ability preserved. Enumeration resolved 2026-07-20 against the 1.0.0 Linux AppImage. The configuration string embedded in the deployed libavcodec and libavutil contains neither --enable-gpl nor --enable-nonfree, so this is a plain LGPL build with no GPL-only or nonfree components. It is built with --enable-openssl, but OpenSSL itself is not bundled - Qt ships stub shims (libQt6FFmpegStub-ssl.so.3, libQt6FFmpegStub-crypto.so.3, and the va, va-drm and va-x11 stubs) that resolve to the host libraries at runtime, so no OpenSSL or VA-API code is distributed in the artifact. Re-check this configuration whenever the Qt version moves.

## Contributor Covenant

- **Version:** 2.1
- **License:** CC-BY-4.0
- **Origin:** https://www.contributor-covenant.org
- **Files:** CODE_OF_CONDUCT.md
- **License text:** attribution kept in the document footer
- **Ships in:** source
- **Obligations:** Attribution (present in the file).

## Mermaid grammar definitions (reference only)

- **Version:** mermaid@11.16.0
- **License:** MIT
- **Origin:** https://github.com/mermaid-js/mermaid
- **Files:** none shipped - the native diagram parsers in src/diagrams are first-party C++ reimplementations written against the pinned jison grammar definitions; no Mermaid code or assets are vendored
- **License text:** not applicable (acknowledgment entry)
- **Ships in:** not shipped
- **Obligations:** None; entry documents derivation provenance for reviewers.

## MathJax (external CDN reference)

- **Version:** pinned URL in HTML export
- **License:** Apache-2.0
- **Origin:** https://www.mathjax.org
- **Files:** none shipped - HTML export writes a script tag loading MathJax from its CDN in the reader's browser; nothing is bundled or executed in-process
- **License text:** not applicable (external service reference)
- **Ships in:** not shipped
- **Obligations:** None for distribution; noted for network-disclosure review.

## Application icon and artwork

- **Version:** first-party, 2026-07
- **License:** MPL-2.0
- **Origin:** drawn for this project (packaging/icons/kvit.svg is the source; PNG/ICO/ICNS are generated by packaging/icons/generate.sh)
- **Files:** packaging/icons
- **License text:** LICENSE (project license)
- **Ships in:** windows, macos, linux, source
- **Obligations:** None beyond MPL-2.0.

## Test media fixtures

- **Version:** first-party, 2026-07
- **License:** MPL-2.0
- **Origin:** synthesized for the media-block test suite; not taken from any external recording or stock source
- **Files:** tests/fixtures/sample.wav, tests/fixtures/sample.mp4, tests/fixtures/sample.png
- **License text:** LICENSE (project license)
- **Ships in:** source
- **Obligations:** None beyond MPL-2.0.
