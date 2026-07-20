# 0006. What an extension is trusted to do

**Status:** Open

This record states a question the project has not answered. It exists so that
the question is asked deliberately when someone next touches the extension
interface, rather than being settled by accident in a patch that adds a feature.

## Context

Kvit Notes is built as an open core with an extension seam. The open editor ships
no extensions of its own; the interface exists so that a premium build can add
block kinds, QML objects and panels without that code, or any conditional
referring to it, living in the core. An extension is a module linked on top of
the core library, installed into `ExtensionRegistry` from `main()` before the
shell loads.

The registry is deliberately a plain list rather than a discovery mechanism.
Nothing scans a directory, loads a shared object at runtime, or reads a manifest.
An extension is present because it was compiled and linked into that particular
binary.

The core calls back at three points. `registerBlockKinds` claims fence languages
and their delegates, before the QML engine exists. `installContextProperties`
publishes QML context properties on the shell's root context. `qmlSlot` names the
QML file that fills one of three named UI slots: a bottom bar, a banner strip and
a side panel.

The second of those is the one that raises the question.
`installContextProperties` receives the shell's root `QQmlContext*` and may
install any name on it. That includes names the core already uses. An extension
can shadow `egressPolicy`, `documentManager` or `noteCollection` with an object of
its own, and every QML binding in the shell would then resolve to the
replacement. Nothing in the interface prevents this, detects it, or records that
it happened.

## The question

Are extensions fully trusted, or do they need narrow, versioned capabilities?

Both answers are defensible, and the choice is not obvious.

**Fully trusted.** An extension is compiled into the binary. Whoever links one
in has already chosen what code runs in the process, and could equally have
patched the core. On that reading, a raw `QQmlContext*` grants nothing that
linking does not already grant, the interface is honest about what it is, and
adding a capability layer would be security theater that costs flexibility and
buys nothing. If this is the answer, it should be written down plainly, so that
nobody later mistakes the seam for a sandbox or proposes loading extensions from
disk without revisiting this record.

**Narrow versioned capabilities.** Trust is not really the issue; the issue is
that the contract is unbounded and therefore cannot be evolved or reasoned
about. A core release cannot tell which context properties an extension depends
on, cannot rename one without risking a silent break, and cannot tell whether a
name collision was intentional. Replacing the raw context with a versioned
interface that grants specific capabilities would make the surface enumerable,
let the core detect and refuse a shadowed name, and make an extension's
dependencies explicit. This is a maintainability argument at least as much as a
security one, and it holds even if every extension is written by the same people
who write the core.

## The tradeoff

The disagreement is about what the seam is for. If extensions will only ever be
first-party modules in a build someone controls end to end, the capability layer
is overhead. If the seam is ever meant to carry third-party code, or to survive
independent release cycles on either side, an unbounded contract will not do it,
and the cost of narrowing it grows with every extension written against the
current shape.

The decision also interacts with distribution. Today an extension cannot arrive
without a rebuild, which is what makes full trust tenable. Any future that loads
extensions at runtime changes the answer, and this record should be revisited
before that happens rather than after.

## Status of related work

This question is the subject of architecture finding A6, which is in flight at
the time of writing. Nothing has been decided yet, and nothing in the tree
anticipates a particular answer. When A6 lands, this record should be replaced
by one stating the decision and its consequences, with its status changed from
Open to Accepted or Superseded.

## Evidence in the tree

- `src/extensionregistry.h`: the `KvitExtension` interface, the three callbacks, the `KvitSlots` names, and the note that the registry is a list rather than a discovery mechanism
- `src/extensionregistry.cpp`: installation order and dispatch
- `src/appcontext.cpp`: how the core publishes its own context properties, which is the same mechanism an extension would use to shadow them
- `qml/main.qml`: the three empty `Loader` slots
