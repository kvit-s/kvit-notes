// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef TEXTFILEVIEWMODEL_H
#define TEXTFILEVIEWMODEL_H

#include <QObject>
#include <QString>
#include <QVector>

// Read-only source-file loading for the shell's text surface. The model reads
// no file beyond MaxFileBytes and accepts only valid UTF-8 without NUL bytes;
// refused files remain available to the desktop through the surface's action.
class TextFileViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QString language READ language NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString message READ message NOTIFY changed)
    Q_PROPERTY(int lineCount READ lineCount NOTIFY changed)
    Q_PROPERTY(int requestedLine READ requestedLine NOTIFY changed)

public:
    static constexpr qint64 MaxFileBytes = 16LL * 1024 * 1024;

    explicit TextFileViewModel(QObject *parent = nullptr);

    QString path() const { return m_path; }
    QString text() const { return m_text; }
    QString language() const { return m_language; }
    QString state() const { return m_state; }
    QString message() const { return m_message; }
    int lineCount() const { return m_lineStarts.size(); }
    int requestedLine() const { return m_requestedLine; }

    Q_INVOKABLE bool open(const QString &path, int line = 1);
    Q_INVOKABLE void close();
    Q_INVOKABLE int positionForLine(int line) const;

    static QString languageForPath(const QString &path);

signals:
    void changed();

private:
    void refuse(const QString &path, const QString &state,
                const QString &message);
    void buildLineStarts();

    QString m_path;
    QString m_text;
    QString m_language;
    QString m_state = QStringLiteral("empty");
    QString m_message;
    QVector<int> m_lineStarts;
    int m_requestedLine = 1;
};

#endif // TEXTFILEVIEWMODEL_H
