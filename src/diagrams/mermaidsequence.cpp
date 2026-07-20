// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidsequence.h"
#include "mermaidparser.h"

#include <QColor>
#include <QStringList>

namespace Mermaid {

namespace {

// Message arrows from the pinned sequenceDiagram.jison, mapped to line style
// and head. At any position the first (longest-prefix) match wins. The exotic
// half-head and reverse forms of the pinned grammar render as the nearest
// standard arrow.
struct ArrowDef {
    const char *tok;
    SeqLine line;
    SeqHead head;
    bool bidir;
};
const ArrowDef kArrows[] = {
    { "<<-->>", SeqLine::Dotted, SeqHead::Filled, true },
    { "<<->>", SeqLine::Solid, SeqHead::Filled, true },
    { "--|\\", SeqLine::Dotted, SeqHead::Filled, false },
    { "--|/", SeqLine::Dotted, SeqHead::Filled, false },
    { "--\\\\", SeqLine::Dotted, SeqHead::Open, false },
    { "--//", SeqLine::Dotted, SeqHead::Open, false },
    { "/|--", SeqLine::Dotted, SeqHead::Filled, false },
    { "\\|--", SeqLine::Dotted, SeqHead::Filled, false },
    { "//--", SeqLine::Dotted, SeqHead::Open, false },
    { "\\\\--", SeqLine::Dotted, SeqHead::Open, false },
    { "-->>", SeqLine::Dotted, SeqHead::Filled, false },
    { "-|\\", SeqLine::Solid, SeqHead::Filled, false },
    { "-|/", SeqLine::Solid, SeqHead::Filled, false },
    { "-\\\\", SeqLine::Solid, SeqHead::Open, false },
    { "-//", SeqLine::Solid, SeqHead::Open, false },
    { "/|-", SeqLine::Solid, SeqHead::Filled, false },
    { "\\|-", SeqLine::Solid, SeqHead::Filled, false },
    { "//-", SeqLine::Solid, SeqHead::Open, false },
    { "\\\\-", SeqLine::Solid, SeqHead::Open, false },
    { "->>", SeqLine::Solid, SeqHead::Filled, false },
    { "--x", SeqLine::Dotted, SeqHead::Cross, false },
    { "--)", SeqLine::Dotted, SeqHead::Point, false },
    { "-->", SeqLine::Dotted, SeqHead::Open, false },
    { "-x", SeqLine::Solid, SeqHead::Cross, false },
    { "-)", SeqLine::Solid, SeqHead::Point, false },
    { "->", SeqLine::Solid, SeqHead::Open, false },
};

// Decode Mermaid `#code;` entity escapes (named and base-10 numeric): the
// documented way to put `<`, `;`, `#`, … into sequence labels.
QString decodeEntities(QString s)
{
    s.replace(QLatin1String("#lt;"), QLatin1String("<"));
    s.replace(QLatin1String("#gt;"), QLatin1String(">"));
    s.replace(QLatin1String("#amp;"), QLatin1String("&"));
    s.replace(QLatin1String("#quot;"), QLatin1String("\""));
    int from = 0;
    while (true) {
        const int hash = s.indexOf(u'#', from);
        if (hash < 0)
            break;
        int j = hash + 1;
        while (j < s.size() && s.at(j).isDigit())
            ++j;
        if (j > hash + 1 && j < s.size() && s.at(j) == u';') {
            const int code = s.mid(hash + 1, j - hash - 1).toInt();
            if (code > 0 && code < 0x110000) {
                s.replace(hash, j - hash + 1, QString(QChar(code)));
                from = hash + 1;
                continue;
            }
        }
        from = hash + 1;
    }
    return s;
}

// Strip the `wrap:` / `nowrap:` prefix parseMessage accepts (an optional
// leading `:` per the LINE rule) and decode entity escapes. Wrapping itself
// is a layout concern.
QString stripWrap(QString s)
{
    s = s.trimmed();
    QString probe = s;
    if (probe.startsWith(u':'))
        probe = probe.mid(1).trimmed();
    for (const char *w : { "nowrap:", "wrap:" }) {
        if (probe.startsWith(QLatin1String(w), Qt::CaseInsensitive)) {
            s = probe.mid(int(qstrlen(w))).trimmed();
            break;
        }
    }
    return decodeEntities(s);
}

QString stripQuotes(QString s)
{
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"'))
        s = s.mid(1, s.size() - 2);
    return s;
}

// Case-insensitive keyword at the start of `line`, followed by whitespace or
// end. On match returns true and sets `rest` to the remainder (trimmed).
bool keyword(const QString &line, const char *kw, QString *rest)
{
    const int n = int(qstrlen(kw));
    if (!line.startsWith(QLatin1String(kw), Qt::CaseInsensitive))
        return false;
    if (line.size() > n && !line.at(n).isSpace())
        return false;
    if (rest)
        *rest = line.mid(n).trimmed();
    return true;
}

class SeqParser
{
public:
    SeqParser(ParseResult &r, int baseOffset)
        : m_r(r), m_ast(r.sequence), m_base(baseOffset) {}

    void run(const QString &body);

private:
    struct OpenScope {
        bool isBox = false;
        SeqEvent::Block block = SeqEvent::Loop;
        int line = 1;
    };

    void parseStatement(const QString &line, int lineNo);
    void parseParticipant(const QString &rest, bool actorFigure, int lineNo);
    void parseBox(const QString &rest, int lineNo);
    void parseNote(const QString &rest, int lineNo);
    void parseAutonumber(const QString &rest, int lineNo);
    bool parseMessage(const QString &line, int lineNo);
    int ensureParticipant(const QString &id, int lineNo, bool declaration);
    void addEvent(const SeqEvent &e);
    void diag(int line, const QString &msg,
              Diagnostic::Severity sev = Diagnostic::Error);

    ParseResult &m_r;
    SequenceAst &m_ast;
    int m_base = 0;
    SourceSpan m_curSpan;   // span of the statement being parsed
    QList<OpenScope> m_scopes;
    int m_boxIndex = -1;
    bool m_sawHeader = false;
    bool m_participantCapWarned = false;
    bool m_eventCapWarned = false;
    bool m_centralConnWarned = false;
};

void SeqParser::diag(int line, const QString &msg, Diagnostic::Severity sev)
{
    m_r.diagnostics.append({ line, 1, msg, sev });
}

void SeqParser::addEvent(const SeqEvent &e)
{
    SeqEvent stamped = e;
    if (!stamped.srcSpan.valid())
        stamped.srcSpan = m_curSpan;
    if (m_ast.events.size() >= kMaxEdges) {
        if (!m_eventCapWarned) {
            diag(e.srcLine,
                 QStringLiteral("Too many statements (limit %1); the rest are "
                                "not rendered").arg(kMaxEdges));
            m_eventCapWarned = true;
        }
        return;
    }
    m_ast.events.append(stamped);
}

int SeqParser::ensureParticipant(const QString &id, int lineNo, bool declaration)
{
    if (id.isEmpty())
        return -1;
    int i = m_ast.indexOfParticipant(id);
    if (i < 0) {
        if (m_ast.participants.size() >= kMaxNodes) {
            if (!m_participantCapWarned) {
                diag(lineNo,
                     QStringLiteral("Too many participants (limit %1)")
                         .arg(kMaxNodes));
                m_participantCapWarned = true;
            }
            return -1;
        }
        SeqParticipant p;
        p.id = id;
        p.label = stripQuotes(id);
        p.order = m_ast.participants.size();
        p.srcSpan = m_curSpan;
        m_ast.participants.append(p);
        i = m_ast.participants.size() - 1;
    }
    // A `box` groups the participant statements written inside it.
    if (declaration && m_boxIndex >= 0
        && m_ast.participants[i].boxIndex < 0)
        m_ast.participants[i].boxIndex = m_boxIndex;
    return i;
}

void SeqParser::parseParticipant(const QString &restIn, bool actorFigure,
                                 int lineNo)
{
    QString rest = restIn;
    // A `@{ ... }` participant config block (pinned-grammar addition) is
    // retained but not interpreted.
    const int cfg = rest.indexOf(QLatin1String("@{"));
    if (cfg >= 0) {
        const int close = rest.indexOf(u'}', cfg);
        diag(lineNo,
             QStringLiteral("Participant configuration (`@{...}`) is ignored "
                            "in Kvit"), Diagnostic::Warning);
        rest = rest.left(cfg).trimmed()
             + (close >= 0 ? QStringLiteral(" ") + rest.mid(close + 1).trimmed()
                           : QString());
        rest = rest.trimmed();
    }
    // `participant A as Alice` — the alias splits at the first ` as `.
    static const QLatin1String asToken(" as ");
    QString id = rest;
    QString label;
    // Case-insensitive search for " as " as a separate word.
    int asPos = -1;
    for (int i = 0; i + asToken.size() <= rest.size(); ++i) {
        if (rest.mid(i, asToken.size()).compare(asToken, Qt::CaseInsensitive) == 0) {
            asPos = i;
            break;
        }
    }
    if (asPos >= 0) {
        id = rest.left(asPos).trimmed();
        label = stripWrap(rest.mid(asPos + asToken.size()));
    }
    if (id.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected a participant name"));
        return;
    }
    const int i = ensureParticipant(id, lineNo, true);
    if (i < 0)
        return;
    if (!label.isEmpty())
        m_ast.participants[i].label =
            stripQuotes(label).left(kMaxLabelChars);
    if (actorFigure)
        m_ast.participants[i].actorFigure = true;
}

void SeqParser::parseBox(const QString &rest, int lineNo)
{
    if (m_boxIndex >= 0) {
        diag(lineNo, QStringLiteral("`box` cannot nest"));
        return;
    }
    SeqBox box;
    QString title = stripWrap(rest);
    // The first token may be a color (named, #hex, or rgb()/rgba()).
    if (!title.isEmpty()) {
        QString first;
        if (title.startsWith(QLatin1String("rgb"), Qt::CaseInsensitive)) {
            const int close = title.indexOf(u')');
            if (close > 0)
                first = title.left(close + 1);
        }
        if (first.isEmpty())
            first = title.section(u' ', 0, 0);
        if (first.compare(QLatin1String("transparent"), Qt::CaseInsensitive) == 0) {
            title = title.mid(first.size()).trimmed();
        } else if (first.startsWith(QLatin1String("rgb"), Qt::CaseInsensitive)) {
            // rgb(r,g,b[,a]) — parse the components.
            const int open = first.indexOf(u'(');
            const QStringList parts = first.mid(open + 1, first.size() - open - 2)
                                          .split(u',', Qt::SkipEmptyParts);
            if (parts.size() >= 3) {
                bool ok1 = false, ok2 = false, ok3 = false;
                const int rr = parts.at(0).trimmed().toInt(&ok1);
                const int gg = parts.at(1).trimmed().toInt(&ok2);
                const int bb = parts.at(2).trimmed().toInt(&ok3);
                if (ok1 && ok2 && ok3) {
                    box.color = QColor(rr, gg, bb);
                    title = title.mid(first.size()).trimmed();
                }
            }
        } else {
            const QColor c(first);
            if (c.isValid()) {
                box.color = c;
                title = title.mid(first.size()).trimmed();
            }
        }
    }
    box.title = title;
    m_ast.boxes.append(box);
    m_boxIndex = m_ast.boxes.size() - 1;
    OpenScope s;
    s.isBox = true;
    s.line = lineNo;
    m_scopes.append(s);
}

void SeqParser::parseNote(const QString &rest, int lineNo)
{
    SeqEvent e;
    e.kind = SeqEvent::Note;
    e.srcLine = lineNo;
    QString tail;
    if (keyword(rest, "left of", &tail)) {
        e.placement = SeqEvent::LeftOf;
    } else if (keyword(rest, "right of", &tail)) {
        e.placement = SeqEvent::RightOf;
    } else if (keyword(rest, "over", &tail)) {
        e.placement = SeqEvent::Over;
    } else {
        diag(lineNo, QStringLiteral("Expected `left of`, `right of`, or "
                                    "`over` after `note`"));
        return;
    }
    const int colon = tail.indexOf(u':');
    if (colon < 0) {
        diag(lineNo, QStringLiteral("Expected `:` and note text"));
        return;
    }
    const QString actors = tail.left(colon).trimmed();
    e.text = stripWrap(tail.mid(colon + 1)).left(kMaxLabelChars);
    const QStringList parts = actors.split(u',', Qt::SkipEmptyParts);
    if (parts.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected an actor after `note`"));
        return;
    }
    if (parts.size() > 2)
        diag(lineNo, QStringLiteral("`note over` takes at most two actors"),
             Diagnostic::Warning);
    e.from = parts.first().trimmed();
    if (e.placement == SeqEvent::Over && parts.size() >= 2)
        e.to = parts.at(1).trimmed();
    if (ensureParticipant(e.from, lineNo, false) < 0)
        return;
    if (!e.to.isEmpty() && ensureParticipant(e.to, lineNo, false) < 0)
        return;
    addEvent(e);
}

void SeqParser::parseAutonumber(const QString &rest, int lineNo)
{
    SeqEvent e;
    e.kind = SeqEvent::Autonumber;
    e.srcLine = lineNo;
    const QStringList parts = rest.split(u' ', Qt::SkipEmptyParts);
    if (!parts.isEmpty()
        && parts.first().compare(QLatin1String("off"), Qt::CaseInsensitive) == 0) {
        e.autonumberVisible = false;
    } else {
        e.autonumberVisible = true;
        if (parts.size() >= 1) {
            bool ok = false;
            const int start = parts.at(0).toInt(&ok);
            if (ok)
                e.autonumberStart = start;
        }
        if (parts.size() >= 2) {
            bool ok = false;
            const int step = parts.at(1).toInt(&ok);
            if (ok)
                e.autonumberStep = step;
        }
    }
    addEvent(e);
}

bool SeqParser::parseMessage(const QString &line, int lineNo)
{
    // Find the earliest arrow occurrence; longest token wins at that position.
    int arrowPos = -1;
    const ArrowDef *arrow = nullptr;
    for (int i = 0; i < line.size() && arrowPos < 0; ++i) {
        for (const ArrowDef &d : kArrows) {
            const QLatin1String tok(d.tok);
            if (line.mid(i, tok.size()) == tok) {
                arrowPos = i;
                arrow = &d;
                break;
            }
        }
    }
    if (!arrow)
        return false;

    SeqEvent e;
    e.kind = SeqEvent::Message;
    e.srcLine = lineNo;
    e.line = arrow->line;
    e.head = arrow->head;
    e.bidirectional = arrow->bidir;

    QString fromPart = line.left(arrowPos).trimmed();
    QString rest = line.mid(arrowPos + int(qstrlen(arrow->tok))).trimmed();

    // Central `()` connections are rendered as plain messages.
    bool central = false;
    if (fromPart.endsWith(QLatin1String("()"))) {
        fromPart = fromPart.left(fromPart.size() - 2).trimmed();
        central = true;
    }
    if (rest.startsWith(QLatin1String("()"))) {
        rest = rest.mid(2).trimmed();
        central = true;
    }
    if (central && !m_centralConnWarned) {
        diag(lineNo, QStringLiteral("Central connections (`()`) are rendered "
                                    "as plain messages"), Diagnostic::Warning);
        m_centralConnWarned = true;
    }

    // The +/- activation shorthand before the target actor.
    if (rest.startsWith(u'+')) {
        e.activateTarget = true;
        rest = rest.mid(1).trimmed();
    } else if (rest.startsWith(u'-')) {
        e.deactivateSource = true;
        rest = rest.mid(1).trimmed();
    }

    const int colon = rest.indexOf(u':');
    if (fromPart.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected an actor before the arrow"));
        return true;
    }
    if (colon < 0) {
        diag(lineNo,
             QStringLiteral("Expected `:` and message text after the arrow"));
        return true;
    }
    e.from = fromPart;
    e.to = rest.left(colon).trimmed();
    e.text = stripWrap(rest.mid(colon + 1)).left(kMaxLabelChars);
    if (e.to.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected a target actor"));
        return true;
    }
    if (ensureParticipant(e.from, lineNo, false) < 0
        || ensureParticipant(e.to, lineNo, false) < 0)
        return true;
    addEvent(e);
    return true;
}

void SeqParser::parseStatement(const QString &line, int lineNo)
{
    QString rest;

    if (keyword(line, "sequencediagram", &rest)) {
        m_sawHeader = true;
        if (!rest.isEmpty())
            parseStatement(rest, lineNo);
        return;
    }
    if (keyword(line, "participant", &rest)) {
        parseParticipant(rest, false, lineNo);
        return;
    }
    if (keyword(line, "actor", &rest)) {
        parseParticipant(rest, true, lineNo);
        return;
    }
    if (keyword(line, "create", &rest)) {
        // The create/destroy lifecycle is not rendered; the participant is.
        diag(lineNo, QStringLiteral("`create` lifecycles are rendered as "
                                    "ordinary participants"),
             Diagnostic::Warning);
        QString inner;
        if (keyword(rest, "participant", &inner))
            parseParticipant(inner, false, lineNo);
        else if (keyword(rest, "actor", &inner))
            parseParticipant(inner, true, lineNo);
        else
            parseParticipant(rest, false, lineNo);
        return;
    }
    if (keyword(line, "destroy", &rest)) {
        diag(lineNo, QStringLiteral("`destroy` lifecycles are rendered as "
                                    "ordinary participants"),
             Diagnostic::Warning);
        ensureParticipant(rest.trimmed(), lineNo, false);
        return;
    }
    if (keyword(line, "box", &rest)) {
        if (m_scopes.size() >= kMaxDepth) {
            diag(lineNo, QStringLiteral("Nesting too deep (limit %1)")
                             .arg(kMaxDepth));
            return;
        }
        parseBox(rest, lineNo);
        return;
    }
    if (keyword(line, "end", nullptr)) {
        if (m_scopes.isEmpty()) {
            diag(lineNo, QStringLiteral("`end` without an open block"));
            return;
        }
        const OpenScope top = m_scopes.takeLast();
        if (top.isBox) {
            m_boxIndex = -1;
        } else {
            SeqEvent e;
            e.kind = SeqEvent::BlockEnd;
            e.srcLine = lineNo;
            addEvent(e);
        }
        return;
    }
    if (keyword(line, "activate", &rest) || keyword(line, "deactivate", &rest)) {
        SeqEvent e;
        e.kind = line.startsWith(QLatin1String("activate"), Qt::CaseInsensitive)
            ? SeqEvent::Activate : SeqEvent::Deactivate;
        e.srcLine = lineNo;
        e.from = rest.trimmed();
        if (ensureParticipant(e.from, lineNo, false) < 0)
            return;
        addEvent(e);
        return;
    }
    if (keyword(line, "note", &rest)) {
        parseNote(rest, lineNo);
        return;
    }
    if (keyword(line, "autonumber", &rest)) {
        parseAutonumber(rest, lineNo);
        return;
    }
    // Block statements. `par_over` renders as `par`.
    struct BlockKw { const char *kw; SeqEvent::Block block; };
    static const BlockKw kBlocks[] = {
        { "loop", SeqEvent::Loop }, { "alt", SeqEvent::Alt },
        { "opt", SeqEvent::Opt }, { "par_over", SeqEvent::Par },
        { "par", SeqEvent::Par }, { "critical", SeqEvent::Critical },
        { "break", SeqEvent::Break }, { "rect", SeqEvent::Rect },
    };
    for (const BlockKw &b : kBlocks) {
        if (keyword(line, b.kw, &rest)) {
            if (m_scopes.size() >= kMaxDepth) {
                diag(lineNo, QStringLiteral("Nesting too deep (limit %1)")
                                 .arg(kMaxDepth));
                return;
            }
            SeqEvent e;
            e.kind = SeqEvent::BlockStart;
            e.block = b.block;
            e.blockLabel = stripWrap(rest).left(kMaxLabelChars);
            e.srcLine = lineNo;
            addEvent(e);
            OpenScope s;
            s.block = b.block;
            s.line = lineNo;
            m_scopes.append(s);
            return;
        }
    }
    // Block dividers, validated against the enclosing block.
    struct DividerKw { const char *kw; SeqEvent::Block block; const char *inside; };
    static const DividerKw kDividers[] = {
        { "else", SeqEvent::Alt, "alt" },
        { "and", SeqEvent::Par, "par" },
        { "option", SeqEvent::Critical, "critical" },
    };
    for (const DividerKw &d : kDividers) {
        if (keyword(line, d.kw, &rest)) {
            // Find the innermost non-box scope.
            const OpenScope *top = nullptr;
            for (int i = m_scopes.size() - 1; i >= 0; --i)
                if (!m_scopes.at(i).isBox) { top = &m_scopes.at(i); break; }
            if (!top || top->block != d.block) {
                diag(lineNo, QStringLiteral("`%1` outside a `%2` block")
                                 .arg(QLatin1String(d.kw),
                                      QLatin1String(d.inside)));
                return;
            }
            SeqEvent e;
            e.kind = SeqEvent::BlockDivider;
            e.block = d.block;
            e.blockLabel = stripWrap(rest).left(kMaxLabelChars);
            e.srcLine = lineNo;
            addEvent(e);
            return;
        }
    }
    if (keyword(line, "title", &rest)) {
        m_ast.title = rest;
        return;
    }
    if (line.startsWith(QLatin1String("title:"), Qt::CaseInsensitive)) {
        m_ast.title = line.mid(6).trimmed();
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
    // Restricted popup-menu statements: retained, never rendered.
    for (const char *kw : { "links", "link", "properties", "details" }) {
        if (keyword(line, kw, &rest)) {
            diag(lineNo,
                 QStringLiteral("`%1` popups are not supported in Kvit")
                     .arg(QLatin1String(kw)), Diagnostic::Warning);
            const int colon = rest.indexOf(u':');
            if (colon > 0)
                ensureParticipant(rest.left(colon).trimmed(), lineNo, false);
            return;
        }
    }

    if (parseMessage(line, lineNo))
        return;

    diag(lineNo, QStringLiteral("Unrecognized sequence statement: %1")
                     .arg(line.left(40)));
}

void SeqParser::run(const QString &body)
{
    // The body is split verbatim (a trailing `\r` trims away with the rest of
    // the whitespace) so statement spans map onto the stored fence source.
    const QStringList physical = body.split(u'\n');
    QList<int> lineOffset(physical.size() + 1, 0);
    for (int i = 0; i < physical.size(); ++i)
        lineOffset[i + 1] = lineOffset.at(i) + physical.at(i).size() + 1;

    bool inAccDescr = false;
    for (int li = 0; li < physical.size(); ++li) {
        QString raw = physical.at(li);

        // Multiline `accDescr { ... }` body.
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

        // Comments: `%%` to end of line; a full-line `#` comment. Mid-line
        // `#` is kept — it introduces `#lt;`-style entity escapes in labels.
        int cut = raw.indexOf(QLatin1String("%%"));
        if (cut >= 0)
            raw = raw.left(cut);
        if (raw.trimmed().startsWith(u'#'))
            continue;

        // An `accDescr {` opening without its close enters multiline mode.
        const QString probe = raw.trimmed();
        if (probe.startsWith(QLatin1String("accDescr"), Qt::CaseInsensitive)
            && probe.contains(u'{') && !probe.contains(u'}')) {
            const int brace = probe.indexOf(u'{');
            const QString first = probe.mid(brace + 1).trimmed();
            if (!first.isEmpty())
                m_ast.accDescr = first;
            inAccDescr = true;
            continue;
        }

        // `;` separates statements like a newline — unless it terminates a
        // `#code;` entity escape (`#lt;`, `#59;`), which stays in the text.
        QStringList segments;
        int segStart = 0;
        for (int i = 0; i < raw.size(); ++i) {
            if (raw.at(i) != u';')
                continue;
            int j = i - 1;
            while (j > segStart && raw.at(j).isLetterOrNumber())
                --j;
            if (j >= segStart && raw.at(j) == u'#' && j < i - 1)
                continue;   // an entity escape, not a separator
            segments << raw.mid(segStart, i - segStart);
            segStart = i + 1;
        }
        segments << raw.mid(segStart);
        int segOffset = 0;
        for (const QString &segment : segments) {
            const QString line = segment.trimmed();
            if (line.isEmpty()) {
                segOffset += segment.size() + 1;
                continue;
            }
            int lead = 0;
            while (lead < segment.size() && segment.at(lead).isSpace())
                ++lead;
            m_curSpan = { m_base + lineOffset.at(li) + segOffset + lead,
                          int(line.size()) };
            parseStatement(line, li + 1);
            segOffset += segment.size() + 1;
        }
    }

    for (const OpenScope &s : m_scopes) {
        diag(s.line, s.isBox
                 ? QStringLiteral("`box` is missing its `end`")
                 : QStringLiteral("Block is missing its `end`"));
    }
}

} // namespace

void parseSequence(const QString &body, int baseOffset, ParseResult &result)
{
    SeqParser parser(result, baseOffset);
    parser.run(body);
}

} // namespace Mermaid
