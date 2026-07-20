// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "typography.h"

#include <QFontDatabase>
#include <QVariant>

#include "block.h"
#include "settingsstore.h"
#include "perflog.h"

namespace {
const QString kFamily = QStringLiteral("typography.fontFamily");
const QString kBaseSize = QStringLiteral("typography.fontSize");
const QString kLineHeight = QStringLiteral("typography.lineHeight");
const QString kParagraphSpacing = QStringLiteral("typography.paragraphSpacing");
const QString kMaxContentWidth = QStringLiteral("typography.maxContentWidth");
const QString kMonoFamily = QStringLiteral("typography.monoFamily");
} // namespace

Typography::Typography(QObject *parent)
    : QObject(parent)
{
}

void Typography::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    // Load through the setters so persisted values clamp exactly like
    // live ones; m_loading suppresses the write-back of what was read.
    m_loading = true;
    setFontFamily(m_settings->value(kFamily, m_fontFamily).toString());
    setBaseSize(m_settings->value(kBaseSize, m_baseSize).toInt());
    setLineHeight(m_settings->value(kLineHeight, m_lineHeight).toDouble());
    setParagraphSpacing(
        m_settings->value(kParagraphSpacing, m_paragraphSpacing).toInt());
    setMaxContentWidth(
        m_settings->value(kMaxContentWidth, m_maxContentWidth).toInt());
    setMonoFamily(m_settings->value(kMonoFamily, m_monoFamily).toString());
    m_loading = false;
    emit typographyChanged();
}

void Typography::save(const QString &key, const QVariant &value)
{
    if (m_settings && !m_loading)
        m_settings->setValue(key, value);
}

void Typography::setFontFamily(const QString &family)
{
    if (m_fontFamily == family)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"), QStringLiteral("fontFamily")}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_fontFamily = family;
    save(kFamily, m_fontFamily);
    emit typographyChanged();
}

void Typography::setBaseSize(int size)
{
    size = qBound(MinBaseSize, size, MaxBaseSize);
    if (m_baseSize == size)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"), QStringLiteral("fontSize")},
                    {QStringLiteral("value"), size}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_baseSize = size;
    save(kBaseSize, m_baseSize);
    emit typographyChanged();
}

void Typography::setLineHeight(qreal height)
{
    height = qBound(MinLineHeight, height, MaxLineHeight);
    if (qFuzzyCompare(m_lineHeight, height))
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"), QStringLiteral("lineHeight")},
                    {QStringLiteral("value"), height}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_lineHeight = height;
    save(kLineHeight, m_lineHeight);
    emit typographyChanged();
}

void Typography::setParagraphSpacing(int spacing)
{
    spacing = qBound(MinParagraphSpacing, spacing, MaxParagraphSpacing);
    if (m_paragraphSpacing == spacing)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"),
                     QStringLiteral("paragraphSpacing")},
                    {QStringLiteral("value"), spacing}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_paragraphSpacing = spacing;
    save(kParagraphSpacing, m_paragraphSpacing);
    emit typographyChanged();
}

void Typography::setMaxContentWidth(int width)
{
    if (width != 0)
        width = qMax(MinContentWidth, width);
    if (m_maxContentWidth == width)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"),
                     QStringLiteral("maxContentWidth")},
                    {QStringLiteral("value"), width}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_maxContentWidth = width;
    save(kMaxContentWidth, m_maxContentWidth);
    emit typographyChanged();
}

void Typography::setMonoFamily(const QString &family)
{
    if (m_monoFamily == family)
        return;
    PerfLog::ScopedTimer perf(
        QStringLiteral("typography.reflow"),
        QVariantMap{{QStringLiteral("property"), QStringLiteral("monoFamily")}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_monoFamily = family;
    save(kMonoFamily, m_monoFamily);
    emit typographyChanged();
}

int Typography::sizeForBlockType(int blockType) const
{
    // The frozen ratios (decision 4), expressed against the historical
    // 15 px base so the defaults reproduce the Phase 4/5 values exactly.
    qreal numerator;
    switch (blockType) {
    case Block::Heading1: numerator = 32.0; break;
    case Block::Heading2: numerator = 24.0; break;
    case Block::Heading3: numerator = 20.0; break;
    case Block::Heading4: numerator = 17.0; break;
    case Block::CodeBlock: numerator = 13.0; break;
    default: numerator = 15.0; break;
    }
    return qRound(m_baseSize * numerator / 15.0);
}

QStringList Typography::monospaceFamilies() const
{
    QStringList result;
    const QStringList families = QFontDatabase::families();
    for (const QString &family : families) {
        if (QFontDatabase::isFixedPitch(family)
            && !QFontDatabase::isPrivateFamily(family))
            result.append(family);
    }
    if (!result.contains(QStringLiteral("monospace")))
        result.prepend(QStringLiteral("monospace"));
    return result;
}

void Typography::resetToDefaults()
{
    setFontFamily(QString());
    setBaseSize(15);
    setLineHeight(1.0);
    setParagraphSpacing(8);
    setMaxContentWidth(0);
    setMonoFamily(QStringLiteral("monospace"));
}
