// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include <QtTest>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QUrl>

#include <memory>

#include "appcontext.h"
#include "block.h"
#include "blockmodel.h"
#include "documentexporter.h"
#include "extensionregistry.h"
#include "notecollection.h"
#include "undostack.h"

namespace {

// A demonstration module that adds a paragraph to one note's export and
// nothing to any other, which is the whole of the seam
// (KvitExtension::exportAppendix). It draws nothing: content beside a note is
// what the decoration seam is for, and the point here is that whatever a module
// draws there is in no export of the note unless it says so through this.
const char *kContributedText = "A paragraph the module drew beside the note.";

class ExportingModule : public KvitExtension
{
public:
    QString name() const override { return QStringLiteral("export-demo"); }
    QString qmlNamespace() const override { return QStringLiteral("demo"); }

    QString exportAppendix(const QString &noteRelPath) const override
    {
        if (noteRelPath != QLatin1String("Alpha.md"))
            return QString();
        return QStringLiteral("## Beside the note\n\n")
            + QString::fromLatin1(kContributedText) + QStringLiteral("\n");
    }
    QString exportAppendixLabel() const override
    {
        return QStringLiteral("Review comments");
    }
};

// Warnings the shell emits while this suite runs, captured the way
// tests/test_shell.cpp captures them and for the same reason: QML reports a
// binding it could not resolve as a warning and then carries on with an
// undefined value, so a notice wired to nothing would otherwise pass an
// assertion about a label that is simply absent.
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

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return QString();
    return QString::fromUtf8(file.readAll());
}

} // namespace

// What a linked module adds to a note's export, driven through the shipped
// shell with a demonstration module installed.
//
// A module draws content BESIDE a note rather than inside it, so it is in the
// note's block model nowhere and was in no export of the note at all — and the
// Export command in the File menu, the one a reader will use, silently dropped
// content the note visibly had. The seam is one virtual answering markdown for
// a given note; the exporter renders that markdown the way it renders the
// note's own.
//
// The pure fan-out is tests/test_extensionregistry.cpp and the rendering is
// tests/test_documentexporter.cpp. What only the shell can show is the rest:
// that the composition wires the registry into the exporter at all, that the
// export dialog tells the reader before the destination is chosen, and that a
// note the module has nothing to add to exports exactly as it did.
class TestExportContribution : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();
        m_context = std::make_unique<AppContext>();
        m_context->openSettings(m_dir.filePath(QStringLiteral("settings.json")));

        m_vaultRoot = m_dir.filePath(QStringLiteral("vault"));
        QVERIFY(QDir().mkpath(m_vaultRoot));
        writeNote(QStringLiteral("Alpha.md"),
                  QStringLiteral("# Alpha\n\nThe note's own text.\n"));
        writeNote(QStringLiteral("Beta.md"),
                  QStringLiteral("# Beta\n\nAnother note entirely.\n"));

        // Installed before the shell loads, exactly as a module's main() would.
        m_context->extensions()->install(std::make_unique<ExportingModule>());
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
        if (g_previousHandler)
            qInstallMessageHandler(g_previousHandler);
    }

    void theShellLoadsWithTheModuleInstalled()
    {
        if (m_warningsAfterLoad > 0) {
            QFAIL(qPrintable(QStringLiteral("Loading the shell with a module "
                                            "installed produced warnings:\n  ")
                             + g_warnings.mid(0, m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

    // A reader exporting a note is deciding what leaves the application, so
    // the dialog names the contributed content before the destination picker
    // opens.
    void theExportDialogNamesWhatTheModuleAdds()
    {
        QObject *dialog = openExportDialog();
        QVERIFY(dialog);

        QQuickItem *notice = findItem(QStringLiteral("exportContributionNotice"));
        QVERIFY2(notice, "the export dialog carries no contribution notice");
        QTRY_VERIFY(notice->isVisible());
        const QString text = notice->property("text").toString();
        QVERIFY2(text.contains(QStringLiteral("Review comments")),
                 qPrintable(text));

        // Not for a block-scope export, which carries no contribution: the
        // reader picked particular blocks, and a module's contribution is
        // about the note rather than one of the blocks they picked.
        dialog->setProperty("scope", QStringLiteral("blocks"));
        QTRY_VERIFY(!notice->isVisible());

        dialog->setProperty("scope", QStringLiteral("note"));
        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCoreApplication::processEvents();
    }

    // The composition wires the registry into the exporter: the Export command
    // in the File menu carries the contribution, in every format.
    void theOpenNotesExportCarriesTheContribution()
    {
        openNote(QStringLiteral("Alpha.md"));
        QObject *dialog = openExportDialog();
        QVERIFY(dialog);
        // prepareContext() is what the dialog runs before its destination
        // picker, and it is what tells the exporter which note the live model
        // is.
        QVERIFY(QMetaObject::invokeMethod(dialog, "prepareContext"));

        QTemporaryDir out;
        for (const QString &format : {QStringLiteral("markdown"),
                                      QStringLiteral("html"),
                                      QStringLiteral("text")}) {
            const QString path = QDir(out.path()).filePath(
                QStringLiteral("alpha.") + DocumentExporter::extensionFor(format));
            QVERIFY(m_context->documentExporter()->writeModel(
                m_context->blockModel(), QStringLiteral("Alpha"), format, path));
            const QString written = readFile(path);
            QVERIFY2(written.contains(QString::fromLatin1(kContributedText)),
                     qPrintable(format + QStringLiteral(": ") + written.left(400)));
            // After the note's own text rather than instead of it.
            QVERIFY(written.contains(QStringLiteral("The note's own text")));
        }

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCoreApplication::processEvents();
    }

    // The registry is asked per note, so a note the module has nothing to add
    // to exports exactly as it would have without the module installed.
    void aNoteTheModuleIgnoresExportsUnchanged()
    {
        openNote(QStringLiteral("Beta.md"));
        QObject *dialog = openExportDialog();
        QVERIFY(dialog);
        QVERIFY(QMetaObject::invokeMethod(dialog, "prepareContext"));

        QTemporaryDir out;
        const QString path = QDir(out.path()).filePath(QStringLiteral("beta.md"));
        QVERIFY(m_context->documentExporter()->writeModel(
            m_context->blockModel(), QStringLiteral("Beta"),
            QStringLiteral("markdown"), path));
        const QString written = readFile(path);
        QVERIFY2(!written.contains(QString::fromLatin1(kContributedText)),
                 qPrintable(written));
        QCOMPARE(written, QStringLiteral("# Beta\n\nAnother note entirely.\n"));

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCoreApplication::processEvents();
    }

    // A collection export asks per note, so the contribution follows the note
    // it belongs to into a combined file and no other note picks it up.
    void aCollectionExportCarriesEachNotesOwnContribution()
    {
        QTemporaryDir out;
        const int written = m_context->documentExporter()->exportCollection(
            m_context->noteCollection(), out.path(), QStringLiteral("html"),
            true /* combined */);
        QVERIFY(written > 0);

        QDir dir(out.path());
        const QStringList files = dir.entryList(QStringList{QStringLiteral("*.html")},
                                                QDir::Files);
        QCOMPARE(files.size(), 1);
        const QString combined = readFile(dir.filePath(files.first()));
        QVERIFY2(combined.contains(QString::fromLatin1(kContributedText)),
                 qPrintable(combined.left(400)));
        // Once, in Alpha's section, and not in Beta's.
        QCOMPARE(combined.count(QString::fromLatin1(kContributedText)), 1);
        QVERIFY(combined.indexOf(QString::fromLatin1(kContributedText))
                < combined.indexOf(QStringLiteral("Another note entirely")));
    }

    // The contribution is a string the exporter renders. Exporting writes
    // nothing to the note, its model or its undo stack.
    void exportingLeavesTheNoteAndTheVaultAlone()
    {
        openNote(QStringLiteral("Alpha.md"));
        BlockModel *model = m_context->blockModel();
        const int blocksBefore = model->count();
        const int undoBefore = m_context->undoStack()->count();
        const QString onDiskBefore =
            readFile(m_vaultRoot + QStringLiteral("/Alpha.md"));

        QObject *dialog = openExportDialog();
        QVERIFY(QMetaObject::invokeMethod(dialog, "prepareContext"));
        QTemporaryDir out;
        QVERIFY(m_context->documentExporter()->writeModel(
            model, QStringLiteral("Alpha"), QStringLiteral("html"),
            QDir(out.path()).filePath(QStringLiteral("alpha.html"))));

        QCOMPARE(model->count(), blocksBefore);
        QCOMPARE(m_context->undoStack()->count(), undoBefore);
        QCOMPARE(readFile(m_vaultRoot + QStringLiteral("/Alpha.md")),
                 onDiskBefore);

        QVERIFY(QMetaObject::invokeMethod(dialog, "close"));
        QCoreApplication::processEvents();
    }

    void theSuiteProducedNoWarnings()
    {
        if (g_warnings.size() > m_warningsAfterLoad) {
            QFAIL(qPrintable(QStringLiteral("The suite produced warnings:\n  ")
                             + g_warnings.mid(m_warningsAfterLoad).join(
                                 QStringLiteral("\n  "))));
        }
    }

private:
    void writeNote(const QString &relPath, const QString &text)
    {
        QFile file(m_vaultRoot + QLatin1Char('/') + relPath);
        QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
        file.write(text.toUtf8());
        file.close();
    }

    QQuickWindow *shellWindow()
    {
        return qobject_cast<QQuickWindow *>(m_engine.rootObjects().value(0));
    }

    void openNote(const QString &relPath)
    {
        QObject *window = m_engine.rootObjects().value(0);
        QVERIFY(window);
        QVariant opened;
        QVERIFY(QMetaObject::invokeMethod(window, "openNoteByPath",
                                          Q_RETURN_ARG(QVariant, opened),
                                          Q_ARG(QVariant, relPath)));
        QTRY_COMPARE(window->property("currentNoteRelPath").toString(), relPath);
    }

    QObject *openExportDialog()
    {
        QObject *window = m_engine.rootObjects().value(0);
        if (!window)
            return nullptr;
        QObject *dialog = window->findChild<QObject *>(
            QStringLiteral("exportDialog"));
        if (!dialog)
            return nullptr;
        QMetaObject::invokeMethod(dialog, "openDialog");
        QCoreApplication::processEvents();
        return dialog;
    }

    QQuickItem *findItem(const QString &objectName)
    {
        QObject *window = m_engine.rootObjects().value(0);
        return window ? window->findChild<QQuickItem *>(objectName) : nullptr;
    }

    QTemporaryDir m_dir;
    QString m_vaultRoot;
    std::unique_ptr<AppContext> m_context;
    QQmlApplicationEngine m_engine;
    int m_warningsAfterLoad = 0;
};

QTEST_MAIN(TestExportContribution)
#include "test_exportcontribution.moc"
