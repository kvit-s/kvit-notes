// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef BLOCKATTRIBUTES_H
#define BLOCKATTRIBUTES_H

#include <QObject>
#include <QString>
#include <QMap>

// Per-block presentation storage (phase12-plan.md decision 1, Option A).
//
// A block's presentation attributes live in a trailing HTML-comment tag
// appended to its own markdown, e.g.
//
//     Some text.  <!--kvit align=center-->
//     ---  <!--kvit style=dashed width=50%-->
//     ![alt|420](x.png)  <!--kvit align=center rounded shadow-->
//
// This is one pure parse/serialize pair used by DocumentSerializer for every
// block type: the tag is split off before a block's content parses and
// re-attached on serialize, so a block with no attributes serializes
// byte-identically to before Phase 12. A foreign markdown editor treats the tag
// as an ordinary HTML comment and renders it invisibly (spike (d) — the one
// exception is a *styled* divider, which stops being a thematic break; a bare
// divider is unaffected).
//
// The stored form of a block's attributes (the "payload") is the canonical
// inside of the tag: space-separated tokens, each either `key=value` or a bare
// `flag`, in a stable (sorted) key order. Unknown keys pass through untouched,
// exactly as the note front-matter preserves foreign keys.
//
// The class is also a QML helper (registered as the `blockAttributes` context
// property): its Q_INVOKABLE methods let delegates read typed values off a
// payload and let attribute editors compute a new payload to hand to the one
// undoable model setter.
class BlockAttributes : public QObject
{
    Q_OBJECT
public:
    explicit BlockAttributes(QObject *parent = nullptr) : QObject(parent) {}

    // ---- Pure serializer core (used by DocumentSerializer) ----

    // Strip a trailing "  <!--kvit PAYLOAD-->" tag from one line. Returns the
    // line without the tag (and without the whitespace that separated it), and
    // sets *payload to the canonical payload. A line with no kvit tag is
    // returned UNCHANGED and *payload is left empty — so tag-free content is
    // byte-identical.
    static QString stripTag(const QString &line, QString *payload);

    // Append "  <!--kvit PAYLOAD-->" to content when payload is non-empty;
    // otherwise return content unchanged (the byte-identical no-attribute case).
    static QString attachTag(const QString &content, const QString &payload);

    // Parse a payload into an ordered map ({} for empty). Bare flags map to an
    // empty value. Serialize writes it back with a stable key order: a bare key
    // for an empty value, `key=value` otherwise.
    static QMap<QString, QString> parseMap(const QString &payload);
    static QString serializeMap(const QMap<QString, QString> &map);

    // Re-serialize a payload through the map, normalizing token order/spacing.
    static QString canonical(const QString &payload);

    // ---- QML-invokable typed reads over a payload ----

    // True when key is present (as a flag or a key=value).
    Q_INVOKABLE bool has(const QString &payload, const QString &key) const;
    // Value of key, or def when absent. A bare flag yields an empty string.
    Q_INVOKABLE QString str(const QString &payload, const QString &key,
                            const QString &def = QString()) const;
    // Integer value of key, or def when absent/unparseable.
    Q_INVOKABLE int num(const QString &payload, const QString &key,
                        int def = 0) const;

    // ---- QML-invokable editing helpers (return a NEW payload) ----

    // payload with key set to value (value "" writes a bare flag).
    Q_INVOKABLE QString withValue(const QString &payload, const QString &key,
                                  const QString &value) const;
    // payload with key present (bare flag) when on, removed when off.
    Q_INVOKABLE QString withFlag(const QString &payload, const QString &key,
                                 bool on) const;
    // payload with key removed.
    Q_INVOKABLE QString without(const QString &payload, const QString &key) const;
};

#endif // BLOCKATTRIBUTES_H
