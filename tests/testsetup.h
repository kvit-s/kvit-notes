// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TESTSETUP_H
#define TESTSETUP_H

#include <QQmlEngine>
#include <QQmlContext>
#include <QSet>
#include <QUrl>
#include <QQuickStyle>
#include <QDir>
#include <QStandardPaths>
#include <QFile>
#include <QClipboard>
#include <QGuiApplication>
#include <QMimeData>

#include "appcontext.h"
#include "blockkindregistry.h"
#include "blockmodel.h"
#include "documentmanager.h"
#include "embedmetadata.h"
#include "extensionregistry.h"
#include "notecollection.h"
#include "startupcontroller.h"
#include "undostack.h"

// A hermetic embed fetcher for the Qt Quick tests: returns canned OpenGraph
// HTML synchronously, so embed cards render without touching the network.
class FakeEmbedFetcher : public EmbedFetcher
{
public:
    void fetch(const QString &url,
               std::function<void(bool, const QString &)> done) override
    {
        // A host that refuses its first request and answers the second: the
        // shape the failed card's "Try again" button exists for, and the only
        // way to reach the fallback card from a test without a network.
        if (QUrl(url).host().endsWith(QLatin1String("unreachable.test"))
            && !m_refusedOnce.contains(url)) {
            m_refusedOnce.insert(url);
            done(false, QString());
            return;
        }
        const QString html =
            "<html><head>"
            "<meta property=\"og:title\" content=\"Example Page Title\">"
            "<meta property=\"og:description\" content=\"A short description of "
            "the linked page for the preview card.\">"
            "<meta property=\"og:image\" content=\"https://example.com/thumb.png\">"
            "</head><body>x</body></html>";
        done(true, html);
    }

private:
    QSet<QString> m_refusedOnce;
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
        if (!file.open(QIODevice::WriteOnly))
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

// Puts a payload on the clipboard the way another application would: plain
// text with an HTML flavor beside it, and no trace of Kvit's private type.
// ClipboardHelper::setMarkdown always attaches that private type, which makes
// a paste take the internal arm, so it cannot stage the case where a payload
// arrives from a browser or another editor and has to be converted.
class TestClipboardHelper : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;
    Q_INVOKABLE void setExternal(const QString &text, const QString &html)
    {
        auto *mime = new QMimeData;
        mime->setText(text);
        if (!html.isEmpty())
            mime->setHtml(html);
        QGuiApplication::clipboard()->setMimeData(mime);
    }
};

// The session state a test function starts from.
//
// A Qt Quick Test file runs every one of its functions against one engine, one
// window and one application graph, and QtTest's init() is the hook for
// putting that graph back to a known state between them. Without it a case
// inherits whatever the previous case left in the model, and a suite of a few
// hundred accumulates enough drift that cases fail in the middle of a run and
// pass on their own — which is indistinguishable from flakiness, and hid two
// real defects in this tree for about a hundred commits.
class TestSessionHelper : public QObject
{
    Q_OBJECT
public:
    TestSessionHelper(AppContext *context, QObject *parent = nullptr)
        : QObject(parent), m_context(context) {}

    // Exactly what qmlEngineAvailable() leaves behind: no collection open, no
    // file associated, and the shell's fallback document loaded through the
    // production startup path so the document is clean.
    Q_INVOKABLE void resetToStartupState()
    {
        m_context->noteCollection()->closeRoot();
        m_context->documentManager()->newDocument();
        m_context->startupController()->initializeFallbackDocument();
    }

private:
    AppContext *m_context = nullptr;
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
        AppContext::applyQuickStyle();
        AppContext::registerQmlTypes();

        // The two places production reaches out to the desktop session.
        AppContext::Options options;
        options.showSystemTray = false;
        options.configureLoggingFromSettings = false;

        m_context = new AppContext(options, engine);
        // Hermetic embeds: canned OpenGraph HTML instead of the network.
        m_context->setEmbedFetcher(std::make_unique<FakeEmbedFetcher>());
        // A desktop with nothing to open a URL with. The suite clicks links
        // and embed cards, and every one of those would otherwise launch a
        // browser tab on the desk of whoever is running it. Emptied rather
        // than stubbed, so the tests also exercise what the shell does when a
        // link cannot be opened.
        m_context->urlLauncher()->setOpenersForTests({});
        m_context->openSettings(
            m_collectionDir.filePath(QStringLiteral("app-settings.json")));
        m_context->installContextProperties(engine);

        // No collection is open here, so EmbedMetadata falls back to the
        // per-user cache directory, which survives between runs. Clear it, or
        // a card another run already fetched renders from cache and the
        // inert-by-default assertions pass or fail depending on history.
        QDir(QDir(QStandardPaths::writableLocation(QStandardPaths::CacheLocation))
                 .filePath(QStringLiteral("embedcache")))
            .removeRecursively();

        // Sample content, so the shell opens on a populated document —
        // through the production entry point rather than by driving the model
        // directly. StartupController::initializeFallbackDocument() brackets
        // the load in DocumentManager's baseline-load scope, so the sample is
        // the document's starting state instead of an unsaved replacement of
        // it, and clears the undo stack afterwards. Loading the model here by
        // hand skipped that bracket, which left the shell reporting unsaved
        // changes on a document nobody had touched: four dirty-state cases
        // failed on a fresh process and passed only once an earlier case had
        // saved something.
        m_context->startupController()->initializeFallbackDocument();

        // The collection stays UNOPENED here: the shell renders single-file
        // geometry, so the pre-collection tests run unchanged. Collection
        // tests open a fresh subdirectory of testCollectionDir in init().
        if (!m_collectionDir.isValid())
            qWarning("testsetup: temporary collection dir is invalid");
        QQmlContext *context = engine->rootContext();
        context->setContextProperty("testCollectionDir", m_collectionDir.path());

        // Timing thresholds describe a quiet release build, not a shared CI
        // runner or an instrumented binary. Keep running and reporting every
        // measurement there, but defer the threshold just as timingbudget.h
        // does for the C++ suites. KVIT_ENFORCE_TIMING_BUDGETS remains the
        // explicit way to judge them anywhere.
#ifdef KVIT_SANITIZER_BUILD
        constexpr bool sanitizerBuild = true;
#else
        constexpr bool sanitizerBuild = false;
#endif
        const bool timingBudgetsEnforced =
            qEnvironmentVariableIsSet("KVIT_ENFORCE_TIMING_BUDGETS")
            || (!qEnvironmentVariableIsSet("CI") && !sanitizerBuild);
        context->setContextProperty("testTimingBudgetsEnforced",
                                    timingBudgetsEnforced);
        // File watching and indexing stay asynchronous on shared runners and
        // under sanitizers. Give CI some scheduling headroom and instrumented
        // builds more, while keeping local failures quick.
        const int asyncTimeoutMultiplier = sanitizerBuild
            ? 4
            : (qEnvironmentVariableIsSet("CI") ? 2 : 1);
        context->setContextProperty("testAsyncTimeoutMultiplier",
                                    asyncTimeoutMultiplier);

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
        context->setContextProperty("testClipboard",
                                    new TestClipboardHelper(engine));
        context->setContextProperty("testSession",
                                    new TestSessionHelper(m_context, engine));

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
