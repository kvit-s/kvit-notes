// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TESTSETUP_H
#define TESTSETUP_H

#include <QQmlEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QDir>
#include <QFile>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "blockmodel.h"
#include "embedmetadata.h"
#include "extensionregistry.h"
#include "undostack.h"

// A hermetic embed fetcher for the Qt Quick tests: returns canned OpenGraph
// HTML synchronously, so embed cards render without touching the network.
class FakeEmbedFetcher : public EmbedFetcher
{
public:
    void fetch(const QString &url,
               std::function<void(bool, const QString &)> done) override
    {
        const QString html =
            "<html><head>"
            "<meta property=\"og:title\" content=\"Example Page Title\">"
            "<meta property=\"og:description\" content=\"A short description of "
            "the linked page for the preview card.\">"
            "<meta property=\"og:image\" content=\"https://example.com/thumb.png\">"
            "</head><body>x</body></html>";
        done(true, html);
    }
};
#include "theme.h"
#include "typography.h"

#include <QTemporaryDir>
#include <QImage>
#include <QPainter>

// A file-writing seam for the Qt Quick tests: lets a test act as "another
// program" editing a note on disk, so the FileWatcher → refreshPaths live
// paths (backlinks panel, query block) can be exercised end to end.
class TestFileHelper : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE bool writeFile(const QString &path, const QString &text)
    {
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
            return false;
        file.write(text.toUtf8());
        return true;
    }
    Q_INVOKABLE QString readFile(const QString &path)
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
            return QString();
        return QString::fromUtf8(file.readAll());
    }
};

// Shared qmlEngineAvailable setup for the Qt Quick Test binaries
// (test_integration and test_visual).
//
// This composes the REAL AppContext — the same graph, the same wiring, the
// same context properties the shipped application runs on — and then layers
// the handful of things only a test needs on top: sample content in the
// model, fixture media on disk, and a few helper properties.
//
// It used to rebuild that graph by hand, and had drifted: startupController
// was never published, the search index was never constructed (so global
// search ran against an unindexed collection), and three of the four
// FileWatcher connections were missing, which left the own-write guard
// inactive in every Qt Quick test. Composing the production root is what
// stops that class of divergence from recurring, because there is no second
// copy to fall behind.
class Setup : public QObject
{
    Q_OBJECT

public:
    Setup() {}

    // The composed graph, for a test that needs to reach a service directly
    // rather than through a context property. Valid after
    // qmlEngineAvailable(); owned by the engine.
    AppContext *context() const { return m_context; }

public slots:
    void qmlEngineAvailable(QQmlEngine *engine)
    {
        QQuickStyle::setStyle("Fusion");
        AppContext::registerQmlTypes();

        // The two places production reaches out to the desktop session.
        AppContext::Options options;
        options.showSystemTray = false;
        options.configureLoggingFromSettings = false;

        m_context = new AppContext(options, engine);
        // Hermetic embeds: canned OpenGraph HTML instead of the network.
        m_context->setEmbedFetcher(std::make_unique<FakeEmbedFetcher>());
        m_context->openSettings(
            m_collectionDir.filePath(QStringLiteral("app-settings.json")));
        m_context->installContextProperties(engine);

        // Sample content, so the shell opens on a populated document. The
        // undo stack is reset afterwards: loading the sample is not an edit
        // the user can undo, and the document starts clean.
        m_context->blockModel()->initializeWithSampleData();
        m_context->undoStack()->clear();
        m_context->undoStack()->setClean();

        // The collection stays UNOPENED here: the shell renders single-file
        // geometry, so the pre-collection tests run unchanged. Collection
        // tests open a fresh subdirectory of testCollectionDir in init().
        if (!m_collectionDir.isValid())
            qWarning("testsetup: temporary collection dir is invalid");
        QQmlContext *context = engine->rootContext();
        context->setContextProperty("testCollectionDir", m_collectionDir.path());

        // A sample image on disk for the image-block storyboard/integration.
        const QString samplePath = m_collectionDir.filePath("sample.png");
        {
            QImage sample(240, 150, QImage::Format_ARGB32);
            sample.fill(QColor("#4a90d9"));
            QPainter p(&sample);
            p.setBrush(QColor("#ffd166"));
            p.setPen(Qt::NoPen);
            p.drawEllipse(60, 30, 120, 90);
            p.end();
            sample.save(samplePath, "PNG");
        }
        context->setContextProperty("sampleImagePath", samplePath);

        // Sample audio/video on disk for the media-block storyboard.
        // Copied from the committed fixtures into the collection so they
        // resolve like any note asset.
#ifdef KVIT_TEST_FIXTURES
        {
            const QString fx = QStringLiteral(KVIT_TEST_FIXTURES);
            const QString audio = m_collectionDir.filePath("sample.wav");
            const QString video = m_collectionDir.filePath("sample.mp4");
            QFile::remove(audio);
            QFile::copy(fx + QStringLiteral("/sample.wav"), audio);
            QFile::remove(video);
            QFile::copy(fx + QStringLiteral("/sample.mp4"), video);
            context->setContextProperty("sampleAudioPath", audio);
            context->setContextProperty("sampleVideoPath", video);

            // A sample image for the image-effects storyboard: copied into
            // the collection so a relative ![](sample.png) resolves.
            const QString image = m_collectionDir.filePath("sample.png");
            QFile::remove(image);
            QFile::copy(fx + QStringLiteral("/sample.png"), image);
            context->setContextProperty("sampleImagePath", image);
        }
#endif

        // Lets a test act as "another program" editing a note on disk, so
        // the FileWatcher paths run end to end.
        context->setContextProperty("testFiles", new TestFileHelper(engine));

        // Screenshot directory for the saveScreenshot helper. build.sh wipes
        // and exports KVIT_SHOT_DIR; standalone runs fall back to <cwd>.
        QString shotDir = qEnvironmentVariable("KVIT_SHOT_DIR");
        if (shotDir.isEmpty())
            shotDir = QDir::currentPath() + "/screenshots";
        QDir().mkpath(shotDir);
        context->setContextProperty("screenshotDir", shotDir);
    }

private:
    QTemporaryDir m_collectionDir;
    // Parented to the engine, which owns it; declared here so the tests can
    // reach the composed graph if they ever need to.
    AppContext *m_context = nullptr;
};

#endif // TESTSETUP_H
