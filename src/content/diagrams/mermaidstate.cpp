// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidstate.h"
#include "mermaidparser.h"

#include <QStringList>

namespace Mermaid {

namespace {

// Strip `%%` comments outside quotes; `#` comments only at line start (a `#`
// inside a description stays text, matching the pinned lexer's token order).
QString stripComment(const QString &raw)
{
    bool inQuote = false;
    for (int i = 0; i + 1 < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == u'"')
            inQuote = !inQuote;
        else if (c == u'%' && raw.at(i + 1) == u'%' && !inQuote)
            return raw.left(i);
    }
    return raw;
}

class StateParser
{
public:
    StateParser(ParseResult &r, int baseOffset)
        : m_r(r), m_ast(r.stateDiagram), m_base(baseOffset) {}

    void run(const QString &body);

private:
    void parseStatement(QString line, int lineNo);
    void parseStateDecl(QString rest, int lineNo);
    bool parseTransition(const QString &line, int lineNo);
    int ensureState(const QString &writtenId, int lineNo,
                    StateKind kindIfNew = StateKind::Normal);
    QString scopeId() const
    {
        return m_scope.isEmpty() ? QString() : m_scope.last();
    }
    // Resolve a written id, mapping `[*]` to the scoped start/end pseudo-state.
    int resolveId(QString written, bool asTarget, int lineNo,
                  QStringList *cssOut);
    void diag(int line, const QString &msg,
              Diagnostic::Severity sev = Diagnostic::Error);

    ParseResult &m_r;
    StateAst &m_ast;
    int m_base = 0;
    SourceSpan m_curSpan;       // span of the statement being parsed
    QStringList m_scope;        // open composite state ids
    QList<int> m_scopeLines;
    bool m_stateCapWarned = false;
    bool m_transitionCapWarned = false;
    bool m_dividerWarned = false;
    // Multiline `note ... end note` collection.
    bool m_inNote = false;
    StateNote m_pendingNote;
};

void StateParser::diag(int line, const QString &msg, Diagnostic::Severity sev)
{
    m_r.diagnostics.append({ line, 1, msg, sev });
}

int StateParser::ensureState(const QString &writtenId, int lineNo,
                             StateKind kindIfNew)
{
    const QString id = writtenId.trimmed();
    if (id.isEmpty())
        return -1;
    int i = m_ast.indexOfState(id);
    if (i < 0) {
        if (m_ast.states.size() >= kMaxNodes) {
            if (!m_stateCapWarned) {
                diag(lineNo, QStringLiteral("Too many states (limit %1)")
                                 .arg(kMaxNodes));
                m_stateCapWarned = true;
            }
            return -1;
        }
        StateNode s;
        s.id = id;
        s.label = id;
        s.kind = kindIfNew;
        s.order = m_ast.states.size();
        s.srcSpan = m_curSpan;
        if (!m_scope.isEmpty()) {
            const int parent = m_ast.indexOfState(m_scope.last());
            s.parentIndex = parent;
        }
        m_ast.states.append(s);
        i = m_ast.states.size() - 1;
    }
    return i;
}

int StateParser::resolveId(QString written, bool asTarget, int lineNo,
                           QStringList *cssOut)
{
    written = written.trimmed();
    // `A:::styleClass`
    const int trip = written.indexOf(QLatin1String(":::"));
    if (trip >= 0) {
        if (cssOut)
            cssOut->append(written.mid(trip + 3).trimmed());
        written = written.left(trip).trimmed();
    }
    if (written == QLatin1String("[*]")) {
        // Start/end pseudo-states are scoped to their composite.
        const QString scoped = (asTarget ? QStringLiteral("__end__")
                                         : QStringLiteral("__start__"))
                               + scopeId();
        const int i = ensureState(scoped, lineNo,
                                  asTarget ? StateKind::End : StateKind::Start);
        if (i >= 0)
            m_ast.states[i].label.clear();
        return i;
    }
    return ensureState(written, lineNo);
}

bool StateParser::parseTransition(const QString &line, int lineNo)
{
    const int pos = line.indexOf(QLatin1String("-->"));
    if (pos < 0)
        return false;
    QString left = line.left(pos).trimmed();
    QString right = line.mid(pos + 3).trimmed();
    QString label;
    // The first `:` that is not part of a `:::styleClass` separator starts
    // the transition label.
    const int trip = right.indexOf(QLatin1String(":::"));
    int colon = -1;
    for (int i = 0; i < right.size(); ++i) {
        if (right.at(i) != u':')
            continue;
        if (trip >= 0 && i >= trip && i <= trip + 2)
            continue;
        colon = i;
        break;
    }
    if (colon >= 0) {
        label = right.mid(colon + 1).trimmed().left(kMaxLabelChars);
        right = right.left(colon).trimmed();
    }
    if (left.isEmpty() || right.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected states on both sides of `-->`"));
        return true;
    }
    QStringList cssLeft, cssRight;
    const int a = resolveId(left, false, lineNo, &cssLeft);
    const int b = resolveId(right, true, lineNo, &cssRight);
    if (a < 0 || b < 0)
        return true;
    for (const QString &c : cssLeft)
        if (!m_ast.states[a].cssClasses.contains(c))
            m_ast.states[a].cssClasses.append(c);
    for (const QString &c : cssRight)
        if (!m_ast.states[b].cssClasses.contains(c))
            m_ast.states[b].cssClasses.append(c);
    if (m_ast.transitions.size() >= kMaxEdges) {
        if (!m_transitionCapWarned) {
            diag(lineNo, QStringLiteral("Too many transitions (limit %1)")
                             .arg(kMaxEdges));
            m_transitionCapWarned = true;
        }
        return true;
    }
    StateTransition t;
    t.from = m_ast.states.at(a).id;
    t.to = m_ast.states.at(b).id;
    t.label = label;
    t.order = m_ast.transitions.size();
    t.srcSpan = m_curSpan;
    m_ast.transitions.append(t);
    return true;
}

void StateParser::parseStateDecl(QString rest, int lineNo)
{
    rest = rest.trimmed();
    // `state Name <<fork>>` / `[[fork]]` markers.
    struct KindTok { const char *tok; StateKind kind; };
    static const KindTok kKinds[] = {
        { "<<fork>>", StateKind::Fork }, { "[[fork]]", StateKind::Fork },
        { "<<join>>", StateKind::Join }, { "[[join]]", StateKind::Join },
        { "<<choice>>", StateKind::Choice }, { "[[choice]]", StateKind::Choice },
    };
    for (const KindTok &k : kKinds) {
        if (rest.endsWith(QLatin1String(k.tok), Qt::CaseInsensitive)) {
            const QString id =
                rest.left(rest.size() - int(qstrlen(k.tok))).trimmed();
            const int i = ensureState(id, lineNo, k.kind);
            if (i >= 0) {
                m_ast.states[i].kind = k.kind;
                m_ast.states[i].label.clear();
            }
            return;
        }
    }
    bool openBody = false;
    if (rest.endsWith(u'{')) {
        openBody = true;
        rest.chop(1);
        rest = rest.trimmed();
    }
    QString id = rest;
    QString label;
    // `state "Long description" as s2`
    if (rest.startsWith(u'"')) {
        const int close = rest.indexOf(u'"', 1);
        if (close > 0) {
            label = rest.mid(1, close - 1);
            QString tail = rest.mid(close + 1).trimmed();
            if (tail.startsWith(QLatin1String("as "), Qt::CaseInsensitive))
                id = tail.mid(3).trimmed();
            else {
                diag(lineNo, QStringLiteral("Expected `as <id>` after the "
                                            "state description"));
                return;
            }
        }
    }
    QStringList css;
    const int trip = id.indexOf(QLatin1String(":::"));
    if (trip >= 0) {
        css.append(id.mid(trip + 3).trimmed());
        id = id.left(trip).trimmed();
    }
    if (id.contains(u' ')) {
        diag(lineNo, QStringLiteral("State name must be a single word: %1")
                         .arg(id));
        return;
    }
    const int i = ensureState(id, lineNo);
    if (i < 0)
        return;
    if (!label.isEmpty())
        m_ast.states[i].label = label.left(kMaxLabelChars);
    for (const QString &c : css)
        if (!m_ast.states[i].cssClasses.contains(c))
            m_ast.states[i].cssClasses.append(c);
    if (openBody) {
        if (m_scope.size() >= kMaxDepth) {
            diag(lineNo, QStringLiteral("Composite nesting too deep (limit %1)")
                             .arg(kMaxDepth));
            return;
        }
        m_ast.states[i].composite = true;
        m_scope.append(m_ast.states.at(i).id);
        m_scopeLines.append(lineNo);
    }
}

void StateParser::parseStatement(QString line, int lineNo)
{
    auto keyword = [&](const char *kw, QString *rest) {
        const int n = int(qstrlen(kw));
        if (!line.startsWith(QLatin1String(kw), Qt::CaseInsensitive))
            return false;
        if (line.size() > n && !line.at(n).isSpace())
            return false;
        if (rest)
            *rest = line.mid(n).trimmed();
        return true;
    };

    QString rest;
    if (line.startsWith(QLatin1String("stateDiagram"), Qt::CaseInsensitive)) {
        rest = line.mid(line.startsWith(QLatin1String("stateDiagram-v2"),
                                        Qt::CaseInsensitive) ? 15 : 12)
                   .trimmed();
        if (!rest.isEmpty())
            parseStatement(rest, lineNo);
        return;
    }
    if (line == QLatin1String("}")) {
        if (m_scope.isEmpty()) {
            diag(lineNo, QStringLiteral("`}` without an open composite state"));
            return;
        }
        m_scope.removeLast();
        m_scopeLines.removeLast();
        return;
    }
    if (keyword("direction", &rest)) {
        Direction d = Direction::TB;
        bool ok = true;
        if (rest == QLatin1String("TB") || rest == QLatin1String("TD"))
            d = Direction::TB;
        else if (rest == QLatin1String("BT"))
            d = Direction::BT;
        else if (rest == QLatin1String("LR"))
            d = Direction::LR;
        else if (rest == QLatin1String("RL"))
            d = Direction::RL;
        else
            ok = false;
        // A local direction inside a composite affects only that body
        // upstream; Kvit applies the root direction and ignores local ones.
        if (ok && m_scope.isEmpty())
            m_ast.direction = d;
        return;
    }
    if (keyword("state", &rest)) {
        parseStateDecl(rest, lineNo);
        return;
    }
    if (keyword("note", &rest)) {
        StateNote note;
        QString tail;
        auto keywordOn = [&](const QString &src, const char *kw, QString *out) {
            const int n = int(qstrlen(kw));
            if (!src.startsWith(QLatin1String(kw), Qt::CaseInsensitive))
                return false;
            if (src.size() > n && !src.at(n).isSpace())
                return false;
            *out = src.mid(n).trimmed();
            return true;
        };
        if (keywordOn(rest, "left of", &tail))
            note.leftOf = true;
        else if (keywordOn(rest, "right of", &tail))
            note.leftOf = false;
        else if (rest.startsWith(u'"')) {
            // Floating note: `note "text" as id`.
            const int close = rest.indexOf(u'"', 1);
            if (close > 0) {
                note.text = rest.mid(1, close - 1).left(kMaxLabelChars);
                m_ast.notes.append(note);
            }
            return;
        } else {
            diag(lineNo, QStringLiteral("Expected `left of`, `right of`, or "
                                        "a quoted note text"));
            return;
        }
        const int colon = tail.indexOf(u':');
        if (colon >= 0) {
            note.stateId = tail.left(colon).trimmed();
            note.text = tail.mid(colon + 1).trimmed().left(kMaxLabelChars);
            if (ensureState(note.stateId, lineNo) >= 0)
                m_ast.notes.append(note);
        } else {
            // Multiline form: collect until `end note`.
            note.stateId = tail.trimmed();
            if (ensureState(note.stateId, lineNo) < 0)
                return;
            m_pendingNote = note;
            m_inNote = true;
        }
        return;
    }
    if (keyword("classDef", &rest)) {
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QStringList names =
                rest.left(space).split(u',', Qt::SkipEmptyParts);
            for (const QString &name : names) {
                ClassDef def = m_ast.classDefs.value(name.trimmed());
                parseStyleDeclarations(def, rest.mid(space + 1));
                m_ast.classDefs.insert(name.trimmed(), def);
            }
        }
        return;
    }
    if (keyword("class", &rest)) {
        // `class id1,id2 styleClass`
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QStringList ids =
                rest.left(space).split(u',', Qt::SkipEmptyParts);
            const QString cls = rest.mid(space + 1).trimmed();
            for (const QString &id : ids) {
                const int i = ensureState(id.trimmed(), lineNo);
                if (i >= 0 && !cls.isEmpty()
                    && !m_ast.states[i].cssClasses.contains(cls))
                    m_ast.states[i].cssClasses.append(cls);
            }
        }
        return;
    }
    if (keyword("style", &rest)) {
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QStringList ids =
                rest.left(space).split(u',', Qt::SkipEmptyParts);
            for (const QString &idRaw : ids) {
                const QString id = idRaw.trimmed();
                const QString synth = QStringLiteral("__style_") + id;
                ClassDef def = m_ast.classDefs.value(synth);
                parseStyleDeclarations(def, rest.mid(space + 1));
                m_ast.classDefs.insert(synth, def);
                const int i = ensureState(id, lineNo);
                if (i >= 0 && !m_ast.states[i].cssClasses.contains(synth))
                    m_ast.states[i].cssClasses.append(synth);
            }
        }
        return;
    }
    if (keyword("hide", nullptr)) {
        // `hide empty description` — Kvit never renders empty description
        // compartments, so this is a supported no-op.
        return;
    }
    if (keyword("scale", nullptr)) {
        diag(lineNo, QStringLiteral("`scale` is ignored in Kvit"),
             Diagnostic::Warning);
        return;
    }
    if (keyword("click", nullptr) || keyword("href", nullptr)) {
        diag(lineNo, QStringLiteral("`click` is ignored in Kvit "
                                    "(interactivity is not supported)"),
             Diagnostic::Warning);
        return;
    }
    if (line.startsWith(QLatin1String("accTitle"), Qt::CaseInsensitive)) {
        const int colon = line.indexOf(u':');
        if (colon >= 0)
            m_ast.accTitle = line.mid(colon + 1).trimmed();
        return;
    }
    if (line.startsWith(QLatin1String("accDescr"), Qt::CaseInsensitive)) {
        const int colon = line.indexOf(u':');
        const int brace = line.indexOf(u'{');
        if (colon >= 0 && (brace < 0 || colon < brace))
            m_ast.accDescr = line.mid(colon + 1).trimmed();
        else if (brace >= 0)
            m_ast.accDescr = line.mid(brace + 1).remove(u'}').trimmed();
        return;
    }
    if (line == QLatin1String("--")) {
        if (!m_dividerWarned) {
            diag(lineNo, QStringLiteral("Concurrent regions (`--`) render "
                                        "without dividers in Kvit"),
                 Diagnostic::Warning);
            m_dividerWarned = true;
        }
        return;
    }

    if (parseTransition(line, lineNo))
        return;

    // `A : description` — accumulate description lines. The first `:` that is
    // not part of a `:::styleClass` separator starts the description.
    const int trip = line.indexOf(QLatin1String(":::"));
    int colon = -1;
    for (int i = 0; i < line.size(); ++i) {
        if (line.at(i) != u':')
            continue;
        if (trip >= 0 && i >= trip && i <= trip + 2)
            continue;
        colon = i;
        break;
    }
    if (colon > 0) {
        QStringList css;
        const int i = resolveId(line.left(colon), false, lineNo, &css);
        if (i >= 0) {
            m_ast.states[i].descriptions.append(
                line.mid(colon + 1).trimmed().left(kMaxLabelChars));
            for (const QString &c : css)
                if (!m_ast.states[i].cssClasses.contains(c))
                    m_ast.states[i].cssClasses.append(c);
        }
        return;
    }

    // A bare id (possibly with `:::class`) declares the state.
    if (!line.contains(u' ') || trip >= 0) {
        QStringList css;
        const int i = resolveId(line, false, lineNo, &css);
        if (i >= 0)
            for (const QString &c : css)
                if (!m_ast.states[i].cssClasses.contains(c))
                    m_ast.states[i].cssClasses.append(c);
        return;
    }

    diag(lineNo, QStringLiteral("Unrecognized state-diagram statement: %1")
                     .arg(line.left(40)));
}

void StateParser::run(const QString &body)
{
    const QStringList physical = body.split(u'\n');
    QList<int> lineOffset(physical.size() + 1, 0);
    for (int i = 0; i < physical.size(); ++i)
        lineOffset[i + 1] = lineOffset.at(i) + physical.at(i).size() + 1;

    bool inAccDescr = false;
    for (int li = 0; li < physical.size(); ++li) {
        QString raw = stripComment(physical.at(li));

        if (m_inNote) {
            if (raw.trimmed().compare(QLatin1String("end note"),
                                      Qt::CaseInsensitive) == 0) {
                m_pendingNote.text = m_pendingNote.text.trimmed()
                                         .left(kMaxLabelChars);
                m_ast.notes.append(m_pendingNote);
                m_pendingNote = StateNote();
                m_inNote = false;
            } else {
                if (!m_pendingNote.text.isEmpty())
                    m_pendingNote.text += u'\n';
                m_pendingNote.text += raw.trimmed();
            }
            continue;
        }
        if (inAccDescr) {
            const int close = raw.indexOf(u'}');
            const QString piece = close >= 0 ? raw.left(close) : raw;
            if (!piece.trimmed().isEmpty()) {
                if (!m_ast.accDescr.isEmpty())
                    m_ast.accDescr += u' ';
                m_ast.accDescr += piece.trimmed();
            }
            if (close >= 0)
                inAccDescr = false;
            continue;
        }
        const QString probe = raw.trimmed();
        if (probe.startsWith(u'#'))
            continue;
        if (probe.startsWith(QLatin1String("accDescr"), Qt::CaseInsensitive)
            && probe.contains(u'{') && !probe.contains(u'}')) {
            const int brace = probe.indexOf(u'{');
            const QString first = probe.mid(brace + 1).trimmed();
            if (!first.isEmpty())
                m_ast.accDescr = first;
            inAccDescr = true;
            continue;
        }
        if (probe.isEmpty())
            continue;

        // `;` separates statements like a newline.
        QStringList segments;
        bool inQuote = false;
        int segStart = 0;
        for (int i = 0; i < probe.size(); ++i) {
            const QChar c = probe.at(i);
            if (c == u'"')
                inQuote = !inQuote;
            else if (c == u';' && !inQuote) {
                segments << probe.mid(segStart, i - segStart);
                segStart = i + 1;
            }
        }
        segments << probe.mid(segStart);
        int probeLead = 0;
        while (probeLead < raw.size() && raw.at(probeLead).isSpace())
            ++probeLead;
        int segOffset = 0;
        for (const QString &segment : segments) {
            const QString stmt = segment.trimmed();
            if (!stmt.isEmpty()) {
                int lead = 0;
                while (lead < segment.size() && segment.at(lead).isSpace())
                    ++lead;
                m_curSpan = { m_base + lineOffset.at(li) + probeLead
                                  + segOffset + lead,
                              int(stmt.size()) };
                parseStatement(stmt, li + 1);
            }
            segOffset += segment.size() + 1;
        }
    }

    if (m_inNote)
        diag(physical.size(), QStringLiteral("Note is missing its `end note`"));
    for (const int line : m_scopeLines)
        diag(line, QStringLiteral("Composite state is missing its `}`"));
}

} // namespace

void parseStateDiagram(const QString &body, int baseOffset,
                       ParseResult &result)
{
    StateParser parser(result, baseOffset);
    parser.run(body);
}

} // namespace Mermaid
