# 0006. What an extension is trusted to do

**Status:** Accepted

Extensions are fully trusted. The seam is not a sandbox and is not intended to
become one while extensions arrive by being compiled into the binary. What the
seam does provide is legibility: an extension's contributions are namespaced, so
a collision with the core or with another module is refused and reported rather
than silently shadowing what was there.

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

## The decision, and why

**Extensions are fully trusted, and a capability layer would not change that.**
A module is C++ compiled into the same process. It already has the address
space and can call anything the process can call without going near the
extension interface at all. Constraining the seam would restrict first-party
code that has a hundred other ways in, while doing nothing about an attacker,
who was never going to arrive through it. Whoever links a module in has already
chosen what code runs.

**The maintainability argument was answered separately, and better.** The other
case for narrowing the seam was that an unbounded contract cannot be evolved:
the core could not tell which names an extension depended on, nor rename one
without risking a silent break. That is now handled by namespacing rather than
by capabilities. `KvitExtension` no longer receives a raw `QQmlContext*` to
install arbitrary names on. It declares `qmlNamespace()` and returns
`contextObjects()`, and the registry publishes exactly one context property per
module, so QML reaches `agent.session` rather than a bare global. A namespace
that is not a valid identifier, that another module has taken, or that collides
with a core name is refused with a warning, and that module publishes nothing.

A core name here means a core QML singleton, compared **case-insensitively**.
The core publishes no context properties of its own any more — every service is
a registered singleton in the `Kvit` module — so exact matching would reserve
nothing. Case-insensitive matching keeps the check doing real work and closes a
specific trap: a module free to take `theme` while the core owns `Theme` would
put two identifiers differing by one character, meaning entirely unrelated
objects, in the same QML file.

## Consequences

- The seam must not be described as a security boundary. It is not one, and
  treating it as one would be worse than having none, because it would invite
  trust that the mechanism does not earn.
- Extension authors cannot use the lowercase form of a core singleton name as a
  namespace. That is a real restriction and it is deliberate.
- Registries are instance-owned rather than process-global, so a second
  `AppContext` in one process keeps its own registrations. This matters for
  tests, which compose their own contexts.

## What would reopen this

The decision rests entirely on extensions arriving at compile time. Revisit it
**before**, not after, either of these becomes true:

- **An extension is loaded from disk at runtime.** Full trust is tenable only
  because a module cannot arrive without a rebuild.
- **An extension is written by a third party.** The argument above is that
  linking already grants everything the seam grants. That holds when the same
  people control both sides and stops holding when they do not.

## Evidence in the tree

- `src/application/extensionregistry.h`: the `KvitExtension` interface, `qmlNamespace()` and
  `contextObjects()`, the `KvitSlots` names, the namespace rule and its reason,
  and the note that the registry is a list rather than a discovery mechanism
- `src/application/extensionregistry.cpp`: namespace validation, collision refusal, dispatch
- `src/qml/qmlsingletons.h`: the `KVIT_QML_SINGLETONS` list, which generates both the
  singleton registrations and the reserved-name set from one source
- `tests/test_shell.cpp`: `aModuleCannotTakeACoreContextPropertyName` and the
  case-insensitive collision case, each with a negative case so the check cannot
  pass by refusing everything
- `qml/main.qml`: the three empty `Loader` slots
