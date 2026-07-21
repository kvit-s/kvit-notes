// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef OPERATIONJOURNAL_H
#define OPERATIONJOURNAL_H

#include "notefileio.h"
#include "vaultpaths.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUuid>

// What was half done when the process stopped.
//
// Individual writes in this module are atomic: every note is committed by
// renaming a fully written temporary over it, so no file is ever left half
// written. That says nothing about an operation that spans several files.
// Renaming a tag rewrites the front matter of every note carrying it, and
// renaming a note rewrites every note linking to it; a crash in the middle of
// either leaves a vault where half the notes say one thing and half say
// another, with nothing on disk recording that a single operation was meant
// to cover both halves.
//
// The same gap makes an equal-length metadata edit invisible. A metadata
// rewrite deliberately restores the file's modification time so that tagging
// a note does not reorder "recently modified", and the performance index
// decides freshness from size and modification time. Change `books` to
// `draft` and both are unchanged, so if the index sidecar is not written
// afterwards — a denied write, a crash — the next session trusts the stale
// cached metadata forever, and may later write it back over the correct file.
//
// So a plan is recorded before the first file is touched and removed only
// once the operation and its index write are complete. What is on disk while
// it runs is enough to answer two questions at the next start: which files
// might not match the index (all of them named here, whatever the sidecar
// claims), and what remained to be done (the files not yet marked complete).
//
// The plan lives in <root>/.kvit/operations/<id>.json, with completions
// appended one line at a time to <id>.done. Completions are appended rather
// than rewritten because a tag rename over a thousand notes would otherwise
// rewrite a thousand-entry file a thousand times.
class OperationJournal
{
public:
    struct Plan {
        QString id;
        QString kind;
        QJsonObject payload;   // kind-specific arguments, e.g. the tag names
        QStringList files;     // relPaths the operation intends to rewrite
        QSet<QString> done;    // relPaths already committed
        // The files still owed, in the planned order.
        QStringList remaining() const
        {
            QStringList out;
            for (const QString &file : files) {
                if (!done.contains(file))
                    out.append(file);
            }
            return out;
        }
    };

    void setRootPath(const QString &rootPath) { m_rootPath = rootPath; }

    // Record a plan and return its id, or "" when it could not be recorded.
    // A caller that gets "" carries on: an operation nobody can journal is
    // still better performed than refused, and the failure mode is the one
    // that existed before there was a journal at all.
    QString begin(const QString &kind, const QJsonObject &payload,
                  const QStringList &files)
    {
        const QString dir = directory(true);
        if (dir.isEmpty() || files.isEmpty())
            return QString();
        const QString id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        QJsonObject root;
        root.insert(QStringLiteral("kind"), kind);
        root.insert(QStringLiteral("payload"), payload);
        root.insert(QStringLiteral("files"),
                    QJsonArray::fromStringList(files));
        if (!NoteFileIo::writeFileBytesAtomic(
                dir + QLatin1Char('/') + id + QStringLiteral(".json"),
                QJsonDocument(root).toJson(QJsonDocument::Compact))) {
            return QString();
        }
        return id;
    }

    void markDone(const QString &id, const QString &relPath)
    {
        const QString dir = directory(false);
        if (id.isEmpty() || dir.isEmpty())
            return;
        QFile file(dir + QLatin1Char('/') + id + QStringLiteral(".done"));
        if (!file.open(QIODevice::Append | QIODevice::WriteOnly))
            return;
        file.write(relPath.toUtf8());
        file.write("\n");
        // The plan is only useful if what it says has reached the disk before
        // the next file is touched.
        file.flush();
    }

    void finish(const QString &id)
    {
        const QString dir = directory(false);
        if (id.isEmpty() || dir.isEmpty())
            return;
        QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".done"));
        QFile::remove(dir + QLatin1Char('/') + id + QStringLiteral(".json"));
    }

    // Every plan still on disk. Called when a vault opens, so what it finds
    // is evidence of an operation that did not complete.
    QList<Plan> pending() const
    {
        QList<Plan> plans;
        const QString dir = directory(false);
        if (dir.isEmpty())
            return plans;
        const QStringList names = QDir(dir).entryList(
            {QStringLiteral("*.json")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            bool ok = false;
            const QByteArray bytes = NoteFileIo::readFileBytes(
                dir + QLatin1Char('/') + name, &ok);
            if (!ok)
                continue;
            const QJsonObject root = QJsonDocument::fromJson(bytes).object();
            Plan plan;
            plan.id = name.left(name.size() - 5); // drop ".json"
            plan.kind = root.value(QStringLiteral("kind")).toString();
            plan.payload = root.value(QStringLiteral("payload")).toObject();
            const QJsonArray files = root.value(QStringLiteral("files")).toArray();
            for (const QJsonValue &value : files) {
                const QString relPath = value.toString();
                // Journal contents are vault content like any other: a
                // crafted plan must not name a path outside the vault.
                if (VaultPaths::isPlainRelativePath(relPath))
                    plan.files.append(relPath);
            }
            bool doneOk = false;
            const QString doneText = NoteFileIo::readTextFile(
                dir + QLatin1Char('/') + plan.id + QStringLiteral(".done"),
                &doneOk);
            if (doneOk) {
                const QStringList lines = doneText.split(QLatin1Char('\n'),
                                                         Qt::SkipEmptyParts);
                for (const QString &line : lines)
                    plan.done.insert(line);
            }
            if (!plan.files.isEmpty())
                plans.append(plan);
        }
        return plans;
    }

private:
    QString directory(bool create) const
    {
        const QString rel = QStringLiteral(".kvit/operations");
        return create ? VaultPaths::ensureOwnedDir(m_rootPath, rel)
                      : VaultPaths::ownedDir(m_rootPath, rel);
    }

    QString m_rootPath;
};

#endif // OPERATIONJOURNAL_H
