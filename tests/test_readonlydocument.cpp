// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QGuiApplication>
#include <QKeyEvent>
#include <QQuickWindow>
#include <QDirIterator>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQuickItem>
#include <QQuickTextDocument>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextDocument>
#include <QTextLayout>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentselection.h"
#include "documentserializer.h"
#include "notecollection.h"
#include "qmlservices.h"
#include "theme.h"
#include "undostack.h"

namespace {

// Warnings the shell and the surfaces emit while this suite runs, captured
// the way tests/test_shell.cpp captures them and for the same reason: QML
// reports a binding it could not resolve as a warning and then carries on
// with an undefined value, so a surface wired to nothing would otherwise pass
// every assertion below about counts that are zero.
QStringList g_warnings;
QtMessageHandler g_previousHandler = nullptr;

void capturingHandler(QtMsgType type, const QMessageLogContext &context,
                      const QString &message)
{
    if (type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg) {
        // The runner's problems rather than the shell's: see test_shell.cpp.
        if (!message.contains(QLatin1String("pipewire"))
            && !message.contains(QLatin1String("font family aliases")))
            g_warnings << message;
    }
    if (g_previousHandler)
        g_previousHandler(type, context, message);
}

// The document every case draws: a heading, two paragraphs, a list item and a
// code fence — five blocks, with the three things a surface has to render
// that a plain Text cannot. The first paragraph carries inline markers and a
// wiki-link; the second carries an inline equation; the fence carries markup
// that must stay literal.
QString sampleMarkdown()
{
    return QStringLiteral(
        "# Release notes\n"
        "\n"
        "The first paragraph has **bold** text and a [[Target]] link.\n"
        "\n"
        "A second paragraph with $E=mc^2$ set into it.\n"
        "\n"
        "- one list item\n"
        "\n"
        "```txt\n"
        "verbatim **not bold**\n"
        "```\n");
}

constexpr int kHeading = 0;
constexpr int kFirstParagraph = 1;
constexpr int kSecondParagraph = 2;
constexpr int kListItem = 3;
constexpr int kCodeFence = 4;

// Every note in a vault with its size and last-modified time: the cheapest
// thing that changes when one is written.
//
// The notes rather than the whole tree, because the editor's own housekeeping
// writes under `.kvit/` on its own schedule — the crash journal is debounced,
// so it can land in the middle of any case — and that is the editor's
// business rather than the surface's. What the surface must never do is write
// a note, including the stored version it is drawing.
QStringList noteStamp(const QString &root)
{
    QStringList out;
    QDirIterator it(root, QStringList{QStringLiteral("*.md")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        const QFileInfo info = it.fileInfo();
        if (info.absoluteFilePath().contains(QLatin1String("/.kvit/")))
            continue;
        out << info.absoluteFilePath() + QLatin1Char('|')
                   + QString::number(info.size()) + QLatin1Char('|')
                   + info.lastModified().toString(Qt::ISODateWithMs);
    }
    out.sort();
    return out;
}

} // namespace

// A markdown document drawn somewhere other than the editor pane
// (qml/ReadOnlyDocument.qml, selection.md "A document drawn read-only").
//
// The surface is a second BlockModel with a DocumentSelection of its own, and
// the properties worth defending are what that buys: the blocks render as the
// editor renders them, a sweep across them produces the same range the editor
// produces and copies as markdown, several surfaces in one window keep
// separate selections, and none of it reaches the open note or the disk.
//
// Driven through the shipped shell rather than through a mirror of it, so the
// surfaces are created in the same composition the application runs, and so
// the one consumer rebuilt on the surface — the backup dialog's preview of a
// stored version — is exercised where it lives.
class TestReadOnlyDocument : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));

        // A vault with the wiki-link's target in it, so the link the sample
        // document carries resolves and renders in the link colour rather
        // than in the muted "no such note" one.
        m_vaultRoot = m_dir.filePath(QStringLiteral("vault"));
        QVERIFY(QDir().mkpath(m_vaultRoot));
        writeNote(QStringLiteral("Target.md"),
                  QStringLiteral("# Target\n\nThe note the link points at.\n"));
        writeNote(QStringLiteral("Notes.md"),
                  QStringLiteral("A note that has been edited.\n"));
        QVERIFY(m_context->openVaultRoot(m_vaultRoot));

        m_context->installContextProperties(&m_engine);

        g_warnings.clear();
        g_previousHandler = qInstallMessageHandler(capturingHandler);
        m_engine.load(QUrl(QStringLiteral("qrc:/qml/main.qml")));
        QCoreApplication::processEvents();
        QVERIFY(!m_engine.rootObjects().isEmpty());
        m_warningsAfterLoad = g_warnings.size();
    }

    void cleanupTestCase()
    {
        m_surfaces.clear();
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void init()
    {
        // Each case starts from a two-paragraph note in the editor and no
        // surfaces, so the "the note is untouched" assertions have something
        // to be untouched.
        BlockModel *model = m_context->blockModel();
        while (model->count() > 0)
            model->removeBlock(model->count() - 1);
        model->insertBlock(0, Block::Paragraph, QStringLiteral("note first"));
        model->insertBlock(1, Block::Paragraph, QStringLiteral("note second"));
        noteSelection()->clear();
        m_context->undoStack()->clear();
        m_surfaces.clear();
        QCoreApplication::processEvents();
    }

    void theShellLoadsWithNoWarnings()
    {
        if (m_warningsAfterLoad > 0) {
            QFAIL(qPrintable(QStringLiteral("Loading the shell produced "
                                            "warnings:\n  ")
                             + g_warnings.mid(0, m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

    // The rendering half: five blocks, inline markers hidden, a wiki-link in
    // the link colour, an equation with a box reserved for it, and a fence
    // whose markup stayed literal.
    void aSurfaceDrawsMarkdownAsRenderedBlocks()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);

        // Markers hidden: the paragraph's asterisks and the wiki-link's
        // brackets are gone from what is drawn, and the words are not.
        const QString drawn = rowText(surface, kFirstParagraph);
        QCOMPARE(drawn,
                 QStringLiteral("The first paragraph has bold text and a "
                                "Target link."));
        // The markdown behind it is untouched, which is what a copy uses.
        QCOMPARE(blockMarkdown(surface, kFirstParagraph),
                 QStringLiteral("The first paragraph has **bold** text and a "
                                "[[Target]] link."));

        // The heading kept its text and lost its `#`, and the list item lost
        // its `-`: both are the parser's doing rather than the surface's, and
        // both are what the reader sees.
        QCOMPARE(rowText(surface, kHeading), QStringLiteral("Release notes"));
        QCOMPARE(rowText(surface, kListItem), QStringLiteral("one list item"));

        // A fence is verbatim: its content IS its text, asterisks included.
        QCOMPARE(rowText(surface, kCodeFence),
                 QStringLiteral("verbatim **not bold**"));

        // And a point resolves through the panel's padding rather than past
        // it: the first character of the fence is offset zero, not the
        // character eight pixels of inset further along.
        QCOMPARE(markdownPositionAt(surface, kCodeFence,
                                    scenePoint(surface, kCodeFence, 10, 0.3)),
                 0);
    }

    void aWikiLinkIsStyledAndAnEquationIsBoxed()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);

        // The wiki-link's text, at its offset in the DISPLAY text: the
        // brackets are hidden, so this is several characters left of where
        // the same word sits in the markdown.
        const int wikiPos =
            rowText(surface, kFirstParagraph).indexOf(QStringLiteral("Target"));
        QVERIFY(wikiPos > 0);
        const QTextCharFormat format =
            formatAt(surface, kFirstParagraph, wikiPos);
        QVERIFY2(format.fontUnderline(), "the wiki-link is not drawn as a link");
        // The target exists in the vault, so the resolved colour rather than
        // the muted one.
        QCOMPARE(format.foreground().color(), m_context->theme()->link());

        // The equation: the engine hides the `$…$` markers, reserves a box
        // for the rendered formula and the overlay paints one image into it.
        QQuickItem *row = rowItem(surface, kSecondParagraph);
        QVERIFY(row);
        QTRY_COMPARE(row->property("inlineMathBoxes").toList().size(), 1);
        QVERIFY(!rowText(surface, kSecondParagraph).contains(QLatin1Char('$')));
        QTRY_VERIFY(childItem(row, QStringLiteral("inlineMathImage")));
    }

    // A sweep from the first paragraph into the list selects every block
    // between them, and each one paints its own share.
    void aSweepSelectsEveryBlockBetweenAndPaintsEachShare()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);

        sweep(surface, nearStartOf(surface, kFirstParagraph),
              nearEndOf(surface, kListItem));

        QVERIFY(surface->property("hasSelection").toBool());
        const QVariantMap range = invoke(surface, "selectedRange").toMap();
        QCOMPARE(range.value(QStringLiteral("startIndex")).toInt(),
                 kFirstParagraph);
        QCOMPARE(range.value(QStringLiteral("endIndex")).toInt(), kListItem);

        // Every block the range covers shows a painted portion, and the ones
        // outside it show none.
        for (int i = kFirstParagraph; i <= kListItem; ++i) {
            QVERIFY2(hasPaintedSelection(surface, i),
                     qPrintable(QStringLiteral("block %1 painted nothing")
                                    .arg(i)));
        }
        QVERIFY(!hasPaintedSelection(surface, kHeading));
        QVERIFY(!hasPaintedSelection(surface, kCodeFence));
    }

    // What comes out is markdown: the blocks the range covers whole are
    // serialized with their prefixes, and each partially covered end
    // contributes an inline fragment. The comparison is against the editor's
    // own answer for the same span, which is the definition being met.
    void theCopyIsMarkdownAndMatchesTheEditorsAnswer()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);

        sweep(surface, partWayInto(surface, kFirstParagraph, 0.1),
              nearEndOf(surface, kListItem));
        const QVariantMap range = invoke(surface, "selectedRange").toMap();
        const QString copied = invoke(surface, "selectedMarkdown").toString();

        // The middle blocks came out whole, with the list prefix the
        // serializer writes.
        QVERIFY2(copied.contains(QStringLiteral("A second paragraph with "
                                                "$E=mc^2$ set into it.")),
                 qPrintable(copied));
        QVERIFY2(copied.contains(QStringLiteral("- one list item")),
                 qPrintable(copied));
        // The end the sweep started inside contributes a fragment rather than
        // the whole paragraph, and the fragment is that paragraph's markdown
        // from the offset the sweep began at — markers and all.
        QVERIFY2(range.value(QStringLiteral("startPos")).toInt() > 0,
                 "the sweep did not begin inside the first paragraph");
        const QString firstBlock = blockMarkdown(surface, kFirstParagraph);
        const QString fragment =
            copied.left(copied.indexOf(QStringLiteral("\n\n")));
        QVERIFY2(fragment.length() < firstBlock.length(), qPrintable(fragment));
        QVERIFY2(firstBlock.endsWith(fragment), qPrintable(fragment));
        QVERIFY2(fragment.contains(QStringLiteral("**bold**")),
                 qPrintable(fragment));

        // The same span, applied to the open note holding the same document:
        // the two answers have to agree, because they are the same code
        // pointed at two models.
        DocumentSerializer serializer;
        serializer.loadIntoModel(m_context->blockModel(), sampleMarkdown());
        DocumentSelection *note = noteSelection();
        note->beginTextSelection(
            range.value(QStringLiteral("startIndex")).toInt(),
            range.value(QStringLiteral("startPos")).toInt(), 0);
        note->updateTextSelectionHead(
            range.value(QStringLiteral("endIndex")).toInt(),
            range.value(QStringLiteral("endPos")).toInt());
        QCOMPARE(copied, note->rangeMarkdown());
    }

    void aDoubleClickTakesAWordAndATripleClickTheBlock()
    {
        QQuickItem *surface =
            makeSurface(QStringLiteral("alpha beta gamma\n"));
        QTRY_COMPARE(surface->property("blockCount").toInt(), 1);

        const QPointF at = insideFirstWordOf(surface, 0);
        press(surface, at);
        release(surface);
        press(surface, at);   // the second press of a double click
        release(surface);
        QCOMPARE(invoke(surface, "selectedMarkdown").toString(),
                 QStringLiteral("alpha"));

        press(surface, at);   // and the third
        release(surface);
        QCOMPARE(invoke(surface, "selectedMarkdown").toString(),
                 QStringLiteral("alpha beta gamma"));
    }

    // The surface's selection and the note's are mutually exclusive, which is
    // the rule selection.md already states for the mechanisms that exist.
    void theSurfacesSelectionAndTheNotesExcludeEachOther()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);
        DocumentSelection *note = noteSelection();

        note->beginTextSelection(0, 0, 0);
        note->updateTextSelectionHead(1, 4);
        QVERIFY(note->hasTextSelection());

        sweep(surface, nearStartOf(surface, kFirstParagraph),
              nearEndOf(surface, kSecondParagraph));
        QVERIFY(surface->property("hasSelection").toBool());
        QVERIFY(!note->hasTextSelection());

        // And the other way round.
        note->beginTextSelection(0, 0, 0);
        note->updateTextSelectionHead(1, 4);
        QVERIFY(note->hasTextSelection());
        QVERIFY(!surface->property("hasSelection").toBool());
    }

    void twoSurfacesInOneWindowSelectIndependently()
    {
        QQuickItem *first = makeSurface(sampleMarkdown());
        QQuickItem *second = makeSurface(sampleMarkdown());
        QTRY_COMPARE(first->property("blockCount").toInt(), 5);
        QTRY_COMPARE(second->property("blockCount").toInt(), 5);

        sweep(first, nearStartOf(first, kFirstParagraph),
              nearEndOf(first, kFirstParagraph));
        sweep(second, nearStartOf(second, kListItem),
              nearEndOf(second, kListItem));

        QVERIFY(first->property("hasSelection").toBool());
        QVERIFY(second->property("hasSelection").toBool());
        QCOMPARE(invoke(first, "selectedRange").toMap()
                     .value(QStringLiteral("startIndex")).toInt(),
                 kFirstParagraph);
        QCOMPARE(invoke(second, "selectedRange").toMap()
                     .value(QStringLiteral("startIndex")).toInt(),
                 kListItem);
        QVERIFY(invoke(first, "selectedMarkdown").toString()
                != invoke(second, "selectedMarkdown").toString());

        // Clearing one leaves the other alone.
        invoke(first, "clearSelection");
        QVERIFY(!first->property("hasSelection").toBool());
        QVERIFY(second->property("hasSelection").toBool());
    }

    // Select-all takes the surface first and falls through the second time,
    // which is the two stages Ctrl+A already has inside a paragraph.
    void selectAllTakesTheWholeSurfaceAndThenFallsThrough()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);

        QVERIFY(invoke(surface, "selectAll").toBool());
        QVERIFY(invoke(surface, "everythingSelected").toBool());
        const QString all = invoke(surface, "selectedMarkdown").toString();
        QVERIFY2(all.startsWith(QStringLiteral("# Release notes")),
                 qPrintable(all));
        QVERIFY2(all.contains(QStringLiteral("```txt")), qPrintable(all));

        invoke(surface, "clearSelection");
        QVERIFY(!surface->property("hasSelection").toBool());
    }

    // A divider has no characters to highlight, so it joins a range that
    // crosses it as a whole block and the row itself is tinted.
    void aDividerJoinsARangeAsAWholeBlock()
    {
        QQuickItem *surface = makeSurface(
            QStringLiteral("above the rule\n\n---\n\nbelow the rule\n"));
        QTRY_COMPARE(surface->property("blockCount").toInt(), 3);

        QQuickItem *rule = childItem(rowItem(surface, 1),
                                     QStringLiteral("readOnlyDividerRule"));
        QVERIFY(rule);
        QVERIFY(rule->isVisible());

        sweep(surface, nearStartOf(surface, 0), nearEndOf(surface, 2));
        QVERIFY(surface->property("hasSelection").toBool());
        QQuickItem *band = childItem(rowItem(surface, 1),
                                     QStringLiteral("readOnlySelectionBand"));
        QVERIFY(band);
        QVERIFY(band->isVisible());
        QVERIFY2(invoke(surface, "selectedMarkdown").toString()
                     .contains(QStringLiteral("---")),
                 "the divider is missing from the copy");
    }

    // The keyboard reaches the surface: Ctrl+A takes all of it, Ctrl+C puts
    // the markdown on the clipboard, and Escape drops the selection.
    void theKeyboardSelectsCopiesAndDrops()
    {
        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);
        surface->forceActiveFocus();
        QVERIFY(surface->hasActiveFocus());

        sendKey(Qt::Key_A, Qt::ControlModifier);
        QVERIFY(invoke(surface, "everythingSelected").toBool());

        QGuiApplication::clipboard()->clear();
        sendKey(Qt::Key_C, Qt::ControlModifier);
        const QString onClipboard = QGuiApplication::clipboard()->text();
        QCOMPARE(onClipboard, invoke(surface, "selectedMarkdown").toString());
        QVERIFY2(onClipboard.startsWith(QStringLiteral("# Release notes")),
                 qPrintable(onClipboard));

        sendKey(Qt::Key_Escape, Qt::NoModifier);
        QVERIFY(!surface->property("hasSelection").toBool());
    }

    // Nothing the surface does reaches the open note or the vault.
    void theOpenNoteAndTheVaultAreUntouched()
    {
        BlockModel *note = m_context->blockModel();
        const int countBefore = note->count();
        QStringList idsBefore;
        for (int i = 0; i < countBefore; ++i)
            idsBefore << note->data(note->index(i, 0), BlockModel::BlockIdRole).toString();
        const int undoBefore = m_context->undoStack()->count();
        const QStringList notesBefore = noteStamp(m_vaultRoot);

        QQuickItem *surface = makeSurface(sampleMarkdown());
        QTRY_COMPARE(surface->property("blockCount").toInt(), 5);
        sweep(surface, nearStartOf(surface, kHeading),
              nearEndOf(surface, kCodeFence));
        QVERIFY(surface->property("hasSelection").toBool());
        invoke(surface, "selectedMarkdown");
        // Reloading the surface with a different document is the other thing
        // a consumer does, and it must not reach the note either.
        surface->setProperty("markdown",
                             QStringLiteral("A different document.\n"));
        QTRY_COMPARE(surface->property("blockCount").toInt(), 1);
        QCoreApplication::processEvents();

        QCOMPARE(note->count(), countBefore);
        QStringList idsAfter;
        for (int i = 0; i < note->count(); ++i)
            idsAfter << note->data(note->index(i, 0), BlockModel::BlockIdRole).toString();
        QCOMPARE(idsAfter, idsBefore);
        QCOMPARE(m_context->undoStack()->count(), undoBefore);
        QCOMPARE(noteStamp(m_vaultRoot), notesBefore);
    }

    // The consumer: the backup dialog draws the version under the cursor, so
    // the surface ships with a user rather than only with tests.
    void theBackupDialogDrawsTheVersionUnderTheCursor()
    {
        const QString relPath = QStringLiteral("Notes.md");
        const QString absPath = m_vaultRoot + QLatin1Char('/') + relPath;
        writeNote(relPath, QStringLiteral("A **first** version.\n\n"
                                          "- with a list\n"));
        m_context->noteCollection()->backupBeforeOverwrite(absPath);
        QTRY_VERIFY(!m_context->noteCollection()->backupsFor(relPath).isEmpty());

        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(window, "openNoteByPath",
                                          Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, relPath)));
        QTRY_COMPARE(window->property("currentNoteRelPath").toString(), relPath);

        QObject *dialog = window->findChild<QObject *>(
            QStringLiteral("backupDialog"));
        QVERIFY(dialog);
        // Cost when unused: a dialog's content item is built with the shell
        // whether the dialog is ever opened or not, so the surface sits
        // behind a Loader and nothing exists until somebody asks to restore.
        QVERIFY2(!window->findChild<QQuickItem *>(
                     QStringLiteral("backupPreviewDocument")),
                 "a surface was built before the dialog was opened");
        QVERIFY(QMetaObject::invokeMethod(dialog, "openForCurrentNote"));

        QQuickItem *preview = window->findChild<QQuickItem *>(
            QStringLiteral("backupPreviewDocument"));
        QVERIFY(preview);
        // Two blocks, rendered: the paragraph with its markers hidden and the
        // list item beside its bullet.
        QTRY_COMPARE(preview->property("blockCount").toInt(), 2);
        QCOMPARE(rowText(preview, 0), QStringLiteral("A first version."));
        QCOMPARE(rowText(preview, 1), QStringLiteral("with a list"));

        // And it is selectable, which is the point of drawing it here rather
        // than in a Text.
        sweep(preview, nearStartOf(preview, 0), nearEndOf(preview, 1));
        QVERIFY(preview->property("hasSelection").toBool());
        QVERIFY2(invoke(preview, "selectedMarkdown").toString()
                     .contains(QStringLiteral("**first**")),
                 "the copy is not markdown");

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QTRY_VERIFY(!window->findChild<QQuickItem *>(
            QStringLiteral("backupPreviewDocument")));
    }

    void theSuiteProducedNoWarnings()
    {
        if (g_warnings.size() > m_warningsAfterLoad) {
            QFAIL(qPrintable(QStringLiteral("The surfaces produced "
                                            "warnings:\n  ")
                             + g_warnings.mid(m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

private:
    // The window's own selection, reached the way anything outside
    // AppContext reaches a service: through the table the composition
    // publishes to its QML engine.
    DocumentSelection *noteSelection()
    {
        return qobject_cast<DocumentSelection *>(
            m_context->services()->lookup(&DocumentSelection::staticMetaObject));
    }

    void writeNote(const QString &relPath, const QString &text)
    {
        QFile file(m_vaultRoot + QLatin1Char('/') + relPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text.toUtf8());
        file.close();
    }

    // A surface, created in the shell's own engine and parented into its
    // window, exactly as a consumer's QML would create one.
    QQuickItem *makeSurface(const QString &markdown)
    {
        QObject *window = m_engine.rootObjects().value(0);
        auto *content = window ? window->property("contentItem")
                                     .value<QQuickItem *>() : nullptr;
        if (!content)
            return nullptr;
        QQmlComponent component(
            &m_engine, QUrl(QStringLiteral("qrc:/qml/ReadOnlyDocument.qml")));
        if (component.isError())
            qWarning() << component.errorString();
        auto *surface = qobject_cast<QQuickItem *>(component.create());
        if (!surface)
            return nullptr;
        m_surfaces.push_back(std::unique_ptr<QQuickItem>(surface));
        surface->setParentItem(content);
        surface->setWidth(600);
        surface->setProperty("markdown", markdown);
        // A Repeater builds its items on a clean stack and a Column places
        // them on the next polish, so a surface is not measurable in the turn
        // it was given its document. Every case that asks where a block is
        // starts here rather than assuming.
        for (int attempt = 0; attempt < 200; ++attempt) {
            QCoreApplication::processEvents();
            const int count = surface->property("blockCount").toInt();
            if (count > 0 && rowItem(surface, count - 1))
                break;
            QTest::qWait(5);
        }
        QMetaObject::invokeMethod(surface, "forceLayout");
        return surface;
    }

    QVariant invoke(QQuickItem *surface, const char *name)
    {
        QVariant result;
        QMetaObject::invokeMethod(surface, name, Q_RETURN_ARG(QVariant, result));
        return result;
    }

    QQuickItem *rowItem(QQuickItem *surface, int index)
    {
        QVariant result;
        if (!QMetaObject::invokeMethod(surface, "blockItem",
                                       Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, index)))
            return nullptr;
        return qvariant_cast<QQuickItem *>(result);
    }

    QRectF rowRect(QQuickItem *surface, int index)
    {
        QVariant result;
        if (!QMetaObject::invokeMethod(surface, "blockRect",
                                       Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, index)))
            return QRectF();
        return result.toRectF();
    }

    // What one row answers for a scene point, which is the one question the
    // sweep asks it.
    int markdownPositionAt(QQuickItem *surface, int index, const QPointF &at)
    {
        QVariant result;
        if (!QMetaObject::invokeMethod(rowItem(surface, index),
                                       "markdownPositionAt",
                                       Q_RETURN_ARG(QVariant, result),
                                       Q_ARG(QVariant, at.x()),
                                       Q_ARG(QVariant, at.y())))
            return -1;
        return result.toInt();
    }

    QString blockMarkdown(QQuickItem *surface, int index)
    {
        QVariant result;
        QMetaObject::invokeMethod(surface, "blockMarkdown",
                                  Q_RETURN_ARG(QVariant, result),
                                  Q_ARG(QVariant, index));
        return result.toString();
    }

    // One item of a row's VISUAL tree.
    //
    // Not findChild: an item a Repeater created is owned by the delegate
    // model rather than by the item it is drawn inside, so a row's equation
    // images are visual children of it and QObject children of nothing under
    // it.
    static void collectItems(QQuickItem *parent, const QString &objectName,
                             QList<QQuickItem *> &found)
    {
        if (!parent)
            return;
        const QList<QQuickItem *> children = parent->childItems();
        for (QQuickItem *child : children) {
            if (child->objectName() == objectName)
                found.append(child);
            collectItems(child, objectName, found);
        }
    }
    static QQuickItem *childItem(QQuickItem *parent, const QString &objectName)
    {
        QList<QQuickItem *> found;
        collectItems(parent, objectName, found);
        return found.value(0);
    }

    // The editor one row draws its text in, and the text it laid out.
    QQuickItem *rowEditor(QQuickItem *surface, int index)
    {
        return childItem(rowItem(surface, index),
                         QStringLiteral("readOnlyBlockText"));
    }

    QTextDocument *rowDocument(QQuickItem *surface, int index)
    {
        QQuickItem *editor = rowEditor(surface, index);
        if (!editor)
            return nullptr;
        auto *handle = qvariant_cast<QQuickTextDocument *>(
            editor->property("textDocument"));
        return handle ? handle->textDocument() : nullptr;
    }

    QString rowText(QQuickItem *surface, int index)
    {
        QTextDocument *doc = rowDocument(surface, index);
        return doc ? doc->toPlainText() : QString();
    }

    // The character format the layout actually renders at a display position.
    QTextCharFormat formatAt(QQuickItem *surface, int index, int pos)
    {
        QTextDocument *doc = rowDocument(surface, index);
        if (!doc || !doc->firstBlock().layout())
            return QTextCharFormat();
        const auto formats = doc->firstBlock().layout()->formats();
        for (const auto &range : formats) {
            if (pos >= range.start && pos < range.start + range.length)
                return range.format;
        }
        return QTextCharFormat();
    }

    bool hasPaintedSelection(QQuickItem *surface, int index)
    {
        QQuickItem *editor = rowEditor(surface, index);
        if (!editor)
            return false;
        return editor->property("selectionEnd").toInt()
             > editor->property("selectionStart").toInt();
    }

    // ---- driving the pointer ----
    //
    // The three calls the surface's MouseArea makes, with scene coordinates
    // worked out from a row's rectangle. The MouseArea forwards its events
    // and does nothing else, so this drives the gesture the reader drives.

    QPointF scenePoint(QQuickItem *surface, int index, qreal dx, qreal fy)
    {
        const QRectF rect = rowRect(surface, index);
        return surface->mapToScene(
            QPointF(rect.x() + dx, rect.y() + rect.height() * fy));
    }

    QPointF nearStartOf(QQuickItem *surface, int index)
    {
        return scenePoint(surface, index, 1, 0.5);
    }
    QPointF insideFirstWordOf(QQuickItem *surface, int index)
    {
        return scenePoint(surface, index, 4, 0.5);
    }
    // A fraction of the way along a row, for a sweep that has to begin part
    // way through a paragraph rather than at its edge.
    QPointF partWayInto(QQuickItem *surface, int index, qreal fraction)
    {
        const QRectF rect = rowRect(surface, index);
        return scenePoint(surface, index, rect.width() * fraction, 0.5);
    }
    QPointF nearEndOf(QQuickItem *surface, int index)
    {
        const QRectF rect = rowRect(surface, index);
        return surface->mapToScene(
            QPointF(rect.right() - 1, rect.y() + rect.height() * 0.5));
    }

    void press(QQuickItem *surface, const QPointF &at)
    {
        QMetaObject::invokeMethod(surface, "beginSweepAt",
                                  Q_ARG(QVariant, at.x()),
                                  Q_ARG(QVariant, at.y()));
    }
    void moveTo(QQuickItem *surface, const QPointF &at)
    {
        QMetaObject::invokeMethod(surface, "updateSweepAt",
                                  Q_ARG(QVariant, at.x()),
                                  Q_ARG(QVariant, at.y()));
    }
    void release(QQuickItem *surface)
    {
        QMetaObject::invokeMethod(surface, "endSweep");
    }

    // A key, delivered to the window rather than posted through the platform:
    // an offscreen window is never activated, and an inactive window is not
    // the focus window the platform layer would route a synthesised key to.
    void sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
    {
        auto *window = qobject_cast<QQuickWindow *>(
            m_engine.rootObjects().value(0));
        if (!window)
            return;
        QKeyEvent press(QEvent::KeyPress, key, modifiers);
        QCoreApplication::sendEvent(window, &press);
        QKeyEvent release(QEvent::KeyRelease, key, modifiers);
        QCoreApplication::sendEvent(window, &release);
        QCoreApplication::processEvents();
    }

    void sweep(QQuickItem *surface, const QPointF &from, const QPointF &to)
    {
        press(surface, from);
        moveTo(surface, to);
        release(surface);
        QCoreApplication::processEvents();
    }

    QTemporaryDir m_dir;
    QString m_vaultRoot;
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    std::vector<std::unique_ptr<QQuickItem>> m_surfaces;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestReadOnlyDocument)
#include "test_readonlydocument.moc"
