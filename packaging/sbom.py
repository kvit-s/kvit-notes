#!/usr/bin/env python3
"""Everything the packaging does with packaging/sbom.yaml.

    packaging/sbom.py notices [--check]
        Write packaging/THIRD-PARTY-NOTICES.md from sbom.yaml, or with --check
        exit 1 when the file differs from what sbom.yaml gives.

    packaging/sbom.py check <os>/<arch>=<executable>...
        Check executables against sbom.yaml: the Go toolchain and every Go
        module compiled in (as `go version -m` reports them) must have an entry
        with the same version, and every file embedded with //go:embed in the
        packages the program is built from (as `go list -deps` reports them
        for that os/arch) must match an entry's `embedded` pattern. Anything
        missing fails, so a new dependency cannot ship without its notice.

    packaging/sbom.py licenses <dir> <artifact> [--math] <executable>...
        Copy into <dir> the project's LICENSE, THIRD-PARTY-NOTICES.md and the
        licence texts of every component that ships in <artifact> (windows,
        macos, linux or appimage). Texts of Go modules are copied only for
        modules compiled into one of the executables, and texts of the math
        library's components only with --math. A missing or empty source text
        fails.

Run from anywhere; paths in sbom.yaml are relative to the repository root.
Needs Python 3 with PyYAML, and the go command (the module cache holds the
modules' licence texts).
"""

import fnmatch
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import yaml

ROOT = Path(__file__).resolve().parent.parent
SBOM = ROOT / "packaging" / "sbom.yaml"
NOTICES = ROOT / "packaging" / "THIRD-PARTY-NOTICES.md"
MAIN_PACKAGE = "./cmd/kvit-notes"
MODULE_TEXT = re.compile(r"^(LICEN[CS]E|COPYING|NOTICE|PATENTS)", re.IGNORECASE)

HEADER = """\
# Third-party notices

Kvit Notes is licensed under the Mozilla Public License 2.0 (see LICENSE).
Its packages contain the third-party components below. Each package has
these notices and the licence texts in its licenses folder: `licenses\\` beside
kvit-notes.exe on Windows, `Contents/Resources/licenses` in the macOS app, and
`share/licenses/kvit-notes` on Linux. The math fonts' licences are beside the
fonts in `math-res`.

The source code of every Go module listed here is available from its origin
at the version given, and from the Go module mirror at
`https://proxy.golang.org/<module path>/@v/<version>.zip`.

This file is generated from `packaging/sbom.yaml` by `packaging/sbom.py
notices`; edit the manifest, not this file.

"""


def load():
    return yaml.safe_load(SBOM.read_text())["components"]


def fail(msg):
    print(f"sbom: {msg}", file=sys.stderr)
    sys.exit(1)


# ── notices ──────────────────────────────────────────────────────────────


def render(components):
    out = [HEADER]
    for c in components:
        shipped = ", ".join(c.get("ships_in") or []) or "not shipped"
        out.append(f"## {c['name']}\n\n")
        if c.get("go_module"):
            out.append(f"- **Go module:** {c['go_module']}\n")
        out.append(f"- **Version:** {c['version']}\n")
        out.append(f"- **License:** {c['spdx']}\n")
        out.append(f"- **Origin:** {c['origin']}\n")
        out.append(f"- **Files:** {c['files']}\n")
        out.append(f"- **License text:** {c['license_file']}\n")
        out.append(f"- **Ships in:** {shipped}\n")
        if c.get("obligations"):
            out.append(f"- **Obligations:** {c['obligations']}\n")
        out.append("\n")
    return "".join(out).rstrip() + "\n"


def cmd_notices(args):
    content = render(load())
    if "--check" in args:
        if not NOTICES.exists() or NOTICES.read_text() != content:
            fail("packaging/THIRD-PARTY-NOTICES.md is stale; run packaging/sbom.py notices")
        print("packaging/THIRD-PARTY-NOTICES.md matches sbom.yaml")
        return
    NOTICES.write_text(content)
    print(f"wrote {NOTICES.relative_to(ROOT)}")


# ── what an executable contains ──────────────────────────────────────────


def go(*args, env=None):
    """The output of a go command run in the repository; its error ends the run."""
    r = subprocess.run(["go", *args], cwd=ROOT, env=env, capture_output=True, text=True)
    if r.returncode != 0:
        fail(f"go {' '.join(args)} failed:\n{r.stderr.strip()}")
    return r.stdout


def build_info(exe):
    """The Go version and the modules compiled into exe: {path: (version, replaced)}."""
    text = go("version", "-m", str(exe))
    lines = text.splitlines()
    go_version = lines[0].rsplit(" ", 1)[-1]
    modules = {}
    last = None
    for line in lines[1:]:
        f = line.strip("\n").split("\t")
        if len(f) < 3:
            continue
        if f[1] == "dep":
            last = f[2]
            modules[last] = (f[3] if len(f) > 3 else "", False)
        elif f[1] == "=>" and last:
            modules[last] = (modules[last][0], True)
    return go_version, modules


def embedded_files(goos, goarch):
    fmt = '{{range .EmbedFiles}}{{$.ImportPath}}:{{.}}{{"\\n"}}{{end}}'
    env = {**os.environ, "GOOS": goos, "GOARCH": goarch, "CGO_ENABLED": "0"}
    text = go("list", "-deps", "-f", fmt, MAIN_PACKAGE, env=env)
    return [line for line in text.splitlines() if line]


def cmd_check(args):
    components = load()
    by_module = {c["go_module"]: c for c in components if c.get("go_module")}
    toolchain = next((c for c in components if c.get("go_toolchain")), None)
    patterns = [p for c in components for p in c.get("embedded") or []]
    problems = []
    for arg in args:
        target, _, exe = arg.partition("=")
        goos, _, goarch = target.partition("/")
        if not exe or not goarch:
            fail(f"expected <os>/<arch>=<executable>, not {arg!r}")
        go_version, modules = build_info(exe)
        if toolchain is None or toolchain["version"] != go_version:
            problems.append(f"{exe}: built with {go_version}, but sbom.yaml's Go entry says "
                            f"{toolchain and toolchain['version']}")
        for path, (version, replaced) in sorted(modules.items()):
            c = by_module.get(path)
            if c is None:
                problems.append(f"{exe}: Go module {path} {version} has no entry in sbom.yaml")
            elif replaced:
                if not str(c["version"]).startswith("local replacement"):
                    problems.append(f"{exe}: {path} is replaced by a local directory, "
                                    f"but sbom.yaml gives version {c['version']}")
            elif str(c["version"]) != version:
                problems.append(f"{exe}: {path} is {version}, but sbom.yaml says {c['version']}")
        for f in embedded_files(goos, goarch):
            if not any(fnmatch.fnmatchcase(f, p) for p in patterns):
                problems.append(f"{target}: embedded file {f} matches no `embedded` pattern in sbom.yaml")
        print(f"  {target}: {go_version}, {len(modules)} modules")
    if problems:
        for p in problems:
            print(f"  PROBLEM {p}", file=sys.stderr)
        fail("the executables contain what sbom.yaml does not describe; update "
             "packaging/sbom.yaml, then run packaging/sbom.py notices")
    print("Every module and embedded file is in sbom.yaml.")


# ── licence texts into an artifact ───────────────────────────────────────


_module_dirs = {}


def module_dir(path):
    if path not in _module_dirs:
        out = go("list", "-m", "-f", "{{if .Replace}}{{.Replace.Dir}}{{else}}{{.Dir}}{{end}}", path).strip()
        if not out:
            fail(f"the go command does not know where module {path} is")
        _module_dirs[path] = Path(out)
    return _module_dirs[path]


def goroot():
    return Path(go("env", "GOROOT").strip())


def resolve(src):
    m = re.match(r"^\{module:([^}]+)\}/(.*)$", src)
    if m:
        return module_dir(m.group(1)) / m.group(2)
    if src.startswith("{goroot}/"):
        return goroot() / src[len("{goroot}/"):]
    return ROOT / src


def copy_text(src, dest):
    if not src.is_file() or src.stat().st_size == 0:
        fail(f"licence text {src} is missing or empty")
    dest.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(src, dest)
    dest.chmod(0o644)


def cmd_licenses(args):
    math = "--math" in args
    args = [a for a in args if a != "--math"]
    if len(args) < 3:
        fail("usage: sbom.py licenses <dir> <artifact> [--math] <executable>...")
    dest, artifact, exes = Path(args[0]), args[1], args[2:]
    wanted = {"appimage": {"appimage", "linux"}}.get(artifact, {artifact})
    compiled = set()
    for exe in exes:
        compiled |= set(build_info(exe)[1])
    dest.mkdir(parents=True, exist_ok=True)
    copy_text(ROOT / "LICENSE", dest / "LICENSE")
    copy_text(NOTICES, dest / "THIRD-PARTY-NOTICES.md")
    count = 2
    for c in load():
        if not wanted & set(c.get("ships_in") or []):
            continue
        if c.get("with_math") and not math:
            continue
        module = c.get("go_module")
        if module and module not in compiled:
            continue
        texts = c.get("license_texts")
        if texts is None and module:
            mdir = module_dir(module)
            found = sorted(p for p in mdir.iterdir() if p.is_file() and MODULE_TEXT.match(p.name))
            if not found:
                fail(f"module {module} has no LICENSE, COPYING, NOTICE or PATENTS file in {mdir}")
            texts = [{"from": str(p), "to": f"go/{module}/{p.name}"} for p in found]
        for t in texts or []:
            src = Path(t["from"]) if Path(t["from"]).is_absolute() else resolve(t["from"])
            copy_text(src, dest / t["to"])
            count += 1
    print(f"  {count} licence and notice files in {dest}")


def main():
    if len(sys.argv) < 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    cmd, args = sys.argv[1], sys.argv[2:]
    {"notices": cmd_notices, "check": cmd_check, "licenses": cmd_licenses}.get(
        cmd, lambda _: fail(f"unknown command {cmd}"))(args)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
