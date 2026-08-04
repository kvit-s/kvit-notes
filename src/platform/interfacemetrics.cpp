// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "interfacemetrics.h"

#include <QVariant>

#include "settingsstore.h"
#include "perflog.h"

namespace {
const QString kFontSize = QStringLiteral("interface.fontSize");
} // namespace

InterfaceMetrics::InterfaceMetrics(QObject *parent)
    : QObject(parent)
{
}

void InterfaceMetrics::setSettings(SettingsStore *settings)
{
    m_settings = settings;
    if (!m_settings)
        return;
    // Loaded through the setter so a persisted value clamps exactly as a live
    // one does; m_loading suppresses writing back what was just read.
    m_loading = true;
    setFontSize(m_settings->value(kFontSize, m_fontSize).toInt());
    m_loading = false;
    emit changed();
}

void InterfaceMetrics::setFontSize(int size)
{
    size = qBound(MinFontSize, size, MaxFontSize);
    if (m_fontSize == size)
        return;
    // A chrome resize relays out every pane at once, which is the same class
    // of event as a typography reflow and is measured the same way.
    PerfLog::ScopedTimer perf(
        QStringLiteral("interface.reflow"),
        QVariantMap{{QStringLiteral("value"), size}},
        m_loading ? PerfLog::Verbose : PerfLog::Major);
    m_fontSize = size;
    if (m_settings && !m_loading)
        m_settings->setValue(kFontSize, m_fontSize);
    emit changed();
}

int InterfaceMetrics::px(int designPx) const
{
    if (designPx == 0)
        return 0;
    const int scaled = qRound(designPx * scale());
    // A one-pixel rule must not round away to nothing: a separator scaled to
    // zero is a line that vanishes at the smallest interface size, which
    // reads as a layout bug rather than as a smaller interface.
    if (designPx > 0)
        return qMax(1, scaled);
    return qMin(-1, scaled);
}

void InterfaceMetrics::resetToDefaults()
{
    setFontSize(DefaultFontSize);
}
