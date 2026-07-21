#!/usr/bin/env python3
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this
# file, You can obtain one at https://mozilla.org/MPL/2.0/.
"""Check the module boundary the tree is built on.

src/ is seven modules (code-plan.md §6.1, docs/adr/0008-module-boundary.md).
CMake enforces the include direction already — each target publishes only its
own directory and inherits the modules it links, so an upward include fails to
compile. This script exists for the two things that cannot express:

  * A wrong `target_link_libraries` line would silently make an upward include
    legal. The map below is the intended graph, checked against the sources
    rather than against CMake, so the two have to agree.

  * Two ownership rules are about what a module *does*, not what it includes.
    Only kvit-platform may reach Qt's networking. Only files that are meant to
    change the filesystem may call the primitives that change it — and each
    one is listed here with the reason it is allowed, so adding a write is a
    decision someone makes rather than something that happens.

Run it directly, or as the LayeringTests ctest entry:

    python3 tools/check-layering.py

Exits non-zero and prints every violation with its file and line.
"""

import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")

# ── The intended graph.
#
# A module may include headers from itself and from anything listed here,
# transitively. Nothing else.
DEPENDS = {
    "content": set(),
    "domain": {"content"},
    "search": {"domain", "content"},
    "platform": {"domain", "content"},
    "repository": {"domain", "search", "content"},
    "application": {"repository", "platform", "search", "domain", "content"},
    "qml": {"application", "repository", "platform", "search", "domain", "content"},
}

# src/main.cpp is the executable's entry point, not a module; it composes.
UNMODULED = {"main.cpp"}

# ── Qt networking.
#
# Every remote access the app makes goes through EgressFetcher, which applies
# EgressPolicy: scheme, credentials, address, redirects, size and time. A
# module that could open its own connection would be a way around that, so
# only kvit-platform is allowed to name the classes that can.
NETWORK_TYPES = re.compile(
    r"\bQ(?:NetworkAccessManager|NetworkReply|NetworkRequest|NetworkProxy"
    r"|NetworkCookie\w*|NetworkDatagram|NetworkInterface|HostAddress|HostInfo"
    r"|TcpSocket|TcpServer|UdpSocket|SslSocket|SslConfiguration|LocalSocket"
    r"|DnsLookup|AbstractSocket)\b"
)
NETWORK_MODULES = {"platform"}

# ── Filesystem mutation.
#
# The primitives that create, replace, move or delete a file or directory.
# Reading is unrestricted; changing what is on disk is not.
MUTATION = re.compile(
    r"\bQSaveFile\b"
    r"|\bQFile::(?:rename|remove|copy|link)\b"
    r"|\bmkpath\s*\("
    r"|\bmkdir\s*\("
    r"|\bremoveRecursively\s*\("
    r"|\brmdir\s*\("
    r"|\bQIODevice::(?:WriteOnly|Append|ReadWrite|Truncate)\b"
    r"|\.save\s*\("
)

# Every file allowed to change the filesystem, and why. A module name on the
# left means the whole module; anything else is one file.
#
# Keep the reasons here rather than in a comment beside each call: this list is
# the answer to "what can write, and on whose authority", and it is only useful
# if it is complete.
MAY_MUTATE = {
    "repository":
        "the vault — notes, trash, backups, the recovery journal, templates, "
        "imports, the index file and the lock",
    "application/documentmanager.cpp":
        "the open note. Its in-memory body is authoritative, so the session "
        "is its exclusive writer; also its crash-recovery journal",
    "application/documentexporter.cpp":
        "export output, at a path the user chose, outside the vault",
    "application/embedmetadata.cpp":
        "the embed preview cache under .kvit/cache, rebuilt on miss",
    "platform/settingsstore.cpp":
        "per-user application settings, outside any vault",
    "platform/remotemediacache.cpp":
        "the fetched-media cache, rebuilt on miss",
    "search/searchindexdb.cpp":
        "the FTS5 database, rebuilt on miss",
    "search/collectionsearchindex.cpp":
        "the FTS5 database's directory",
    "content/diagrams/diagramcanvas.cpp":
        "a rendered diagram PNG, at a path the exporter chose, outside the "
        "vault",
    "domain/perflog.cpp":
        "the performance log and its rotation, in the user's cache directory",
}


def strip_comments(text):
    """Blank out comments and string literals, keeping line numbering.

    A rule about what the code does must not fire on a sentence describing it,
    and several of these files describe exactly what they are not allowed to
    do.
    """
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == "/" and i + 1 < n and text[i + 1] == "/":
            while i < n and text[i] != "\n":
                out.append(" ")
                i += 1
        elif c == "/" and i + 1 < n and text[i + 1] == "*":
            while i < n and not (text[i] == "*" and i + 1 < n and text[i + 1] == "/"):
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            out.append("  ")
            i += 2
        elif c == '"':
            out.append(" ")
            i += 1
            while i < n and text[i] != '"':
                if text[i] == "\\":
                    out.append(" ")
                    i += 1
                out.append("\n" if text[i] == "\n" else " ")
                i += 1
            i += 1
            out.append(" ")
        else:
            out.append(c)
            i += 1
    return "".join(out)


def sources():
    for dirpath, _, filenames in os.walk(SRC):
        for name in sorted(filenames):
            if not name.endswith((".h", ".cpp")):
                continue
            path = os.path.join(dirpath, name)
            rel = os.path.relpath(path, SRC)
            if rel in UNMODULED:
                continue
            yield rel, path


def module_of(rel):
    return rel.split(os.sep)[0]


def header_owners():
    """Which module each header belongs to, by basename and by module-relative
    path — the two spellings the tree uses."""
    owners = {}
    for rel, _ in sources():
        if not rel.endswith(".h"):
            continue
        parts = rel.split(os.sep)
        mod, inside = parts[0], os.sep.join(parts[1:])
        owners[os.path.basename(rel)] = mod
        owners[inside] = mod
    return owners


def main():
    owners = header_owners()
    problems = []

    for rel, path in sources():
        mod = module_of(rel)
        if mod not in DEPENDS:
            problems.append((rel, 0, "sits in src/%s, which is not a module" % mod))
            continue
        text = open(path, encoding="utf-8", errors="replace").read()
        code = strip_comments(text)
        allowed = DEPENDS[mod] | {mod}

        for lineno, line in enumerate(code.split("\n"), 1):
            # The include direction. Quoted includes only: angle-bracket ones
            # are Qt and third-party.
            match = re.match(r'\s*#\s*include\s+[<"]([^">]+)[">]', text.split("\n")[lineno - 1])
            if match:
                owner = owners.get(match.group(1)) or owners.get(
                    os.path.basename(match.group(1)))
                if owner and owner not in allowed:
                    problems.append((rel, lineno,
                                     'includes "%s" from kvit-%s, which kvit-%s '
                                     "may not depend on" % (match.group(1), owner, mod)))

            found = NETWORK_TYPES.search(line)
            if found and mod not in NETWORK_MODULES:
                problems.append((rel, lineno,
                                 "names %s. Qt networking belongs to kvit-platform, "
                                 "behind EgressFetcher's policy" % found.group(0)))

            found = MUTATION.search(line)
            if found and not (mod in MAY_MUTATE or rel.replace(os.sep, "/") in MAY_MUTATE):
                problems.append((rel, lineno,
                                 "changes the filesystem (%s). Add it to MAY_MUTATE in "
                                 "tools/check-layering.py with the reason, or move the "
                                 "write into kvit-repository" % found.group(0).strip()))

    # A stale allowance is its own kind of wrong: it reads as a decision
    # someone made about code that is no longer there.
    for entry in sorted(MAY_MUTATE):
        if entry in DEPENDS:
            continue
        if not os.path.exists(os.path.join(SRC, entry)):
            problems.append((entry, 0,
                             "is listed in MAY_MUTATE but does not exist"))

    if problems:
        print("Module boundary violations:\n", file=sys.stderr)
        for rel, lineno, why in problems:
            where = "src/%s:%d" % (rel, lineno) if lineno else "src/%s" % rel
            print("  %s: %s" % (where, why), file=sys.stderr)
        print("\n%d violation(s). See docs/adr/0008-module-boundary.md."
              % len(problems), file=sys.stderr)
        return 1

    modules = ", ".join("kvit-" + m for m in DEPENDS)
    print("module boundary: %d sources across %s — include direction, network "
          "ownership and filesystem ownership all hold"
          % (sum(1 for _ in sources()), modules))
    return 0


if __name__ == "__main__":
    sys.exit(main())
