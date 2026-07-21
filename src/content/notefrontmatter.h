// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef NOTEFRONTMATTER_H
#define NOTEFRONTMATTER_H

#include <QString>
#include <QStringList>
#include <QDateTime>
#include <QMap>

// Pure front-matter handling for note files.
// A front-matter block is a "---" fence line at byte 0, mapping
// lines, and a closing "---" fence line; everything after is the note body.
//
// split() is byte-preserving: block + body == the input whenever a block is
// recognized. The parser consumes only what it fully understands (tags,
// created, pinned, favorite); every other line — foreign keys, comments,
// unparseable values — is preserved verbatim in Metadata::unknownLines so a
// rewrite cannot destroy metadata written by other tools.
//
// A Kvit document whose FIRST BLOCK IS A DIVIDER also starts with a "---"
// line, so recognition is strict: a closing fence must exist, every interior
// line must be mapping-shaped (key line, list item, indented continuation,
// comment, or blank), and at least one key line must be present. Divider-led
// documents fail the shape test and read as pure body.
class NoteFrontMatter
{
public:
    struct Metadata {
        QStringList tags;
        QDateTime created;        // invalid = unset
        bool pinned = false;
        bool favorite = false;
        int goal = 0;             // per-note writing goal (words); 0 = unset
        QStringList unknownLines; // preserved verbatim, in order, no '\n'
        // Every "key: value" line as raw scalar text, known keys included,
        // first level only (a block list stores ""); last occurrence wins.
        // READ-ONLY DERIVED DATA for the query block: serialize() never
        // emits from it, so the byte-preserving contract is untouched.
        QMap<QString, QString> fields;

        // Typed accessors for query evaluation — pure, no locale
        // surprises. fieldString strips one matching outer quote pair;
        // fieldDate accepts ISO date-times and YYYY-MM-DD; fieldNumber
        // parses with the C locale; fieldList splits a YAML inline list
        // or a comma-separated scalar.
        QString fieldString(const QString &key) const;
        QDateTime fieldDate(const QString &key) const;
        double fieldNumber(const QString &key, bool *ok = nullptr) const;
        QStringList fieldList(const QString &key) const;

        // fields is deliberately EXCLUDED: it is derived data, and a
        // hand-built Metadata (empty fields) must equal its
        // parse(serialize(...)) round trip (whose fields are populated).
        bool operator==(const Metadata &o) const
        {
            return tags == o.tags && created == o.created
                && pinned == o.pinned && favorite == o.favorite
                && goal == o.goal && unknownLines == o.unknownLines;
        }
        bool operator!=(const Metadata &o) const { return !(*this == o); }
    };

    struct Split {
        QString block; // empty when absent; includes both fence lines
        QString body;
        bool present = false;
    };

    // Byte-preserving split: present => block + body == fileText.
    static Split split(const QString &fileText);

    // Parse a block as returned by split() (fences included; empty string
    // yields defaults). A known key whose value does not parse is kept as
    // an unknown line rather than half-understood. For duplicate known
    // keys the last occurrence wins.
    static Metadata parse(const QString &block);

    // Canonical block: known keys that differ from their defaults (in the
    // order tags, created, pinned, favorite), then the unknown lines
    // verbatim. Empty string when there is nothing to write — a note
    // without metadata gets no front-matter at all.
    static QString serialize(const Metadata &meta);

private:
    static bool parseTagsValue(const QString &value, QStringList *tags);
    static QString serializeTag(const QString &tag);
};

#endif // NOTEFRONTMATTER_H
