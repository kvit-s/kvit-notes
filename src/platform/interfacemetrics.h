// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#ifndef INTERFACEMETRICS_H
#define INTERFACEMETRICS_H

#include <QObject>

class SettingsStore;

// The chrome's type scale and metrics: the sidebar, the note list, the
// toolbar, the status bar, the find bar, the dialogs and the furniture around
// a block. Everything, that is, except the document itself, which Typography
// owns (accessibility.md Finding 4).
//
// Two settings rather than one, because the needs differ and both directions
// are coherent: a person who wants large body text with a small, dense note
// list is asking for something reasonable, and so is the reverse. Operating
// system display scaling — which Qt honours on all three platforms — enlarges
// everything at once and cannot express either.
//
// It is also what keeps Typography's promise. Typography freezes its ratios
// so the document renders pixel-identically at the default; that gets harder
// to hold if the chrome's metrics ride on the same base.
//
// The shape follows Typography's: state in the settings store under an
// `interface.` key prefix, clamped setters, one change signal, and role sizes
// derived from one base by ratios frozen from the values the chrome used
// before this existed. At the default base of 12 every role reproduces its
// old literal exactly, so the default build is pixel-identical.
class InterfaceMetrics : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int fontSize READ fontSize WRITE setFontSize NOTIFY changed)
    // fontSize / 12.0: what px() multiplies by, exposed so a call site that
    // needs a real rather than a rounded integer can do its own arithmetic.
    Q_PROPERTY(qreal scale READ scale NOTIFY changed)

    // The clamps, so the settings slider takes its range from the one place
    // that enforces it rather than repeating the numbers.
    Q_PROPERTY(int minFontSize READ minFontSize CONSTANT)
    Q_PROPERTY(int maxFontSize READ maxFontSize CONSTANT)

    // The five roles the chrome asks for by name. The comment on each is the
    // literal it reproduces at the default base, which is the value that was
    // written out at every call site before this existed.
    Q_PROPERTY(int caption READ caption NOTIFY changed)   // 10
    Q_PROPERTY(int small READ small NOTIFY changed)       // 11
    Q_PROPERTY(int body READ body NOTIFY changed)         // 12
    Q_PROPERTY(int strong READ strong NOTIFY changed)     // 13
    Q_PROPERTY(int title READ title NOTIFY changed)       // 15

public:
    // Clamps: 10 is the smallest size the chrome stays legible at, and 24 is
    // where a note list row stops fitting a title and a date on one line.
    static constexpr int DefaultFontSize = 12;
    static constexpr int MinFontSize = 10;
    static constexpr int MaxFontSize = 24;

    explicit InterfaceMetrics(QObject *parent = nullptr);

    void setSettings(SettingsStore *settings);

    int fontSize() const { return m_fontSize; }
    void setFontSize(int size);
    qreal scale() const { return m_fontSize / qreal(DefaultFontSize); }
    int minFontSize() const { return MinFontSize; }
    int maxFontSize() const { return MaxFontSize; }

    int caption() const { return px(10); }
    int small() const { return px(11); }
    int body() const { return px(12); }
    int strong() const { return px(13); }
    int title() const { return px(15); }

    // A design-pixel value scaled and rounded: control heights, paddings,
    // icon boxes and row heights. `implicitHeight: 28` becomes
    // `implicitHeight: Interface.px(28)`.
    //
    // Font size alone is not enough. A 24-pixel label inside a 28-pixel
    // button clips, so the geometry has to travel with the type scale; that
    // is the whole reason this is here rather than five font-size roles.
    Q_INVOKABLE int px(int designPx) const;

    // Back to the built-in default (the dialog's "Reset interface size").
    Q_INVOKABLE void resetToDefaults();

signals:
    void changed();

private:
    SettingsStore *m_settings = nullptr;
    bool m_loading = false;
    int m_fontSize = DefaultFontSize;
};

#endif // INTERFACEMETRICS_H
