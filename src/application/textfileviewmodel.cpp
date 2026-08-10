// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "textfileviewmodel.h"

#include "codelanguages.h"

#include <QFile>
#include <QFileInfo>
#include <QStringDecoder>

TextFileViewModel::TextFileViewModel(QObject *parent)
    : QObject(parent)
{
}

bool TextFileViewModel::open(const QString &path, int line)
{
    const QFileInfo info(path);
    if (!info.exists() || !info.isFile()) {
        refuse(path, QStringLiteral("error"),
               tr("The file no longer exists."));
        return false;
    }
    if (info.size() > MaxFileBytes) {
        refuse(path, QStringLiteral("tooLarge"),
               tr("This file is larger than %1 MiB. Open it with the desktop "
                  "application instead.").arg(MaxFileBytes / (1024 * 1024)));
        return false;
    }

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly)) {
        refuse(path, QStringLiteral("error"), file.errorString());
        return false;
    }
    const qint64 expected = file.size();
    QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError
        || (expected > 0 && bytes.size() != expected)) {
        refuse(path, QStringLiteral("error"),
               tr("The file could not be read completely."));
        return false;
    }
    if (bytes.contains('\0')) {
        refuse(path, QStringLiteral("binary"),
               tr("This is a binary file, not UTF-8 text."));
        return false;
    }
    if (bytes.startsWith("\xEF\xBB\xBF"))
        bytes.remove(0, 3);
    QStringDecoder decoder(QStringDecoder::Utf8);
    QString decoded = decoder.decode(bytes);
    if (decoder.hasError()) {
        refuse(path, QStringLiteral("binary"),
               tr("This file is not valid UTF-8 text."));
        return false;
    }
    // Match the editor's text-mode read: CRLF is one line break. A lone CR
    // is normalized too, so it never appears as a visible control glyph.
    decoded.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    decoded.replace(QLatin1Char('\r'), QLatin1Char('\n'));

    m_path = info.absoluteFilePath();
    m_text = decoded;
    m_language = languageForPath(m_path);
    m_state = QStringLiteral("ready");
    m_message.clear();
    buildLineStarts();
    m_requestedLine = qBound(1, line, qMax(1, lineCount()));
    emit changed();
    return true;
}

void TextFileViewModel::close()
{
    if (m_state == QLatin1String("empty") && m_path.isEmpty())
        return;
    m_path.clear();
    m_text.clear();
    m_language.clear();
    m_state = QStringLiteral("empty");
    m_message.clear();
    m_lineStarts.clear();
    m_requestedLine = 1;
    emit changed();
}

int TextFileViewModel::positionForLine(int line) const
{
    if (m_lineStarts.isEmpty())
        return 0;
    const int clamped = qBound(1, line, m_lineStarts.size());
    return m_lineStarts.at(clamped - 1);
}

QString TextFileViewModel::languageForPath(const QString &path)
{
    const QFileInfo info(path);
    const QString suffix = info.suffix().toLower();
    const QString name = info.fileName().toLower();
    if (name == QLatin1String("cmakelists.txt")
        || suffix == QLatin1String("cmake")) {
        return QString(); // no CMake table: plain is better than wrong
    }
    if (suffix == QLatin1String("qml"))
        return QStringLiteral("qml");
    return CodeLanguages::canonicalLanguage(suffix);
}

void TextFileViewModel::refuse(const QString &path, const QString &state,
                               const QString &message)
{
    m_path = QFileInfo(path).absoluteFilePath();
    m_text.clear();
    m_language.clear();
    m_state = state;
    m_message = message;
    m_lineStarts.clear();
    m_requestedLine = 1;
    emit changed();
}

void TextFileViewModel::buildLineStarts()
{
    m_lineStarts.clear();
    m_lineStarts.append(0);
    for (int i = 0; i < m_text.size(); ++i) {
        if (m_text.at(i) == QLatin1Char('\n') && i + 1 <= m_text.size())
            m_lineStarts.append(i + 1);
    }
}
