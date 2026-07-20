// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaidclass.h"
#include "mermaidparser.h"

#include <QStringList>

namespace Mermaid {

namespace {

// Strip a `%%` comment (outside quotes/backticks) from a line.
QString stripComment(const QString &raw)
{
    bool inQuote = false, inBq = false;
    for (int i = 0; i + 1 < raw.size(); ++i) {
        const QChar c = raw.at(i);
        if (c == u'"' && !inBq)
            inQuote = !inQuote;
        else if (c == u'`' && !inQuote)
            inBq = !inBq;
        else if (c == u'%' && raw.at(i + 1) == u'%' && !inQuote && !inBq)
            return raw.left(i);
    }
    return raw;
}

// Find the first occurrence of `--` or `..` outside quotes and backticks.
// Returns -1 when absent; sets `dotted`.
int findLineToken(const QString &s, bool *dotted)
{
    bool inQuote = false, inBq = false;
    for (int i = 0; i + 1 < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == u'"' && !inBq) {
            inQuote = !inQuote;
            continue;
        }
        if (c == u'`' && !inQuote) {
            inBq = !inBq;
            continue;
        }
        if (inQuote || inBq)
            continue;
        if (c == u'-' && s.at(i + 1) == u'-') {
            *dotted = false;
            return i;
        }
        if (c == u'.' && s.at(i + 1) == u'.') {
            *dotted = true;
            return i;
        }
    }
    return -1;
}

// Normalize a written class name: strip backquotes, whitespace.
QString cleanClassName(QString s)
{
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(u'`') && s.endsWith(u'`'))
        s = s.mid(1, s.size() - 2);
    return s;
}

// The display label for a class id: `Name~T~` renders as `Name<T>`.
QString displayLabel(const QString &id)
{
    QString s = id;
    const int t1 = s.indexOf(u'~');
    const int t2 = s.lastIndexOf(u'~');
    if (t1 >= 0 && t2 > t1) {
        s.replace(t2, 1, QStringLiteral(">"));
        s.replace(t1, 1, QStringLiteral("<"));
    }
    return s;
}

class ClassParser
{
public:
    ClassParser(ParseResult &r, int baseOffset)
        : m_r(r), m_ast(r.classDiagram), m_base(baseOffset) {}

    void run(const QString &body);

private:
    enum ScopeKind { NamespaceScope, ClassBodyScope };
    struct Scope {
        ScopeKind kind;
        int index;   // namespace index / class index
        int line;
    };

    void parseStatement(QString line, int lineNo);
    void parseClassDecl(const QString &rest, int lineNo);
    bool parseRelation(const QString &line, int lineNo);
    void addMemberText(int classIndex, const QString &text);
    int ensureClass(const QString &writtenName, int lineNo);
    ClassRelEnd chopEndMarker(QString &side, bool leftSide);
    void diag(int line, const QString &msg,
              Diagnostic::Severity sev = Diagnostic::Error);

    ParseResult &m_r;
    ClassAst &m_ast;
    int m_base = 0;
    SourceSpan m_curSpan;   // span of the statement being parsed
    QList<Scope> m_scopes;
    bool m_classCapWarned = false;
    bool m_relationCapWarned = false;
};

void ClassParser::diag(int line, const QString &msg, Diagnostic::Severity sev)
{
    m_r.diagnostics.append({ line, 1, msg, sev });
}

int ClassParser::ensureClass(const QString &writtenName, int lineNo)
{
    const QString id = cleanClassName(writtenName);
    if (id.isEmpty())
        return -1;
    int i = m_ast.indexOfClass(id);
    if (i < 0) {
        if (m_ast.classes.size() >= kMaxNodes) {
            if (!m_classCapWarned) {
                diag(lineNo, QStringLiteral("Too many classes (limit %1)")
                                 .arg(kMaxNodes));
                m_classCapWarned = true;
            }
            return -1;
        }
        ClassNode c;
        c.id = id;
        c.label = displayLabel(id);
        c.order = m_ast.classes.size();
        c.srcSpan = m_curSpan;
        // Classes declared inside a namespace body belong to it.
        for (int s = m_scopes.size() - 1; s >= 0; --s) {
            if (m_scopes.at(s).kind == NamespaceScope) {
                c.namespaceIndex = m_scopes.at(s).index;
                m_ast.namespaces[c.namespaceIndex].classIds.append(id);
                break;
            }
        }
        m_ast.classes.append(c);
        i = m_ast.classes.size() - 1;
    }
    return i;
}

void ClassParser::addMemberText(int classIndex, const QString &text)
{
    if (classIndex < 0)
        return;
    const QString t = text.trimmed();
    if (t.isEmpty())
        return;
    ClassNode &c = m_ast.classes[classIndex];
    if (t.startsWith(QLatin1String("<<")) && t.endsWith(QLatin1String(">>"))) {
        c.annotation = t.mid(2, t.size() - 4).trimmed();
        return;
    }
    if (t.contains(u'('))
        c.methods.append(t.left(kMaxLabelChars));
    else
        c.attributes.append(t.left(kMaxLabelChars));
}

// Remove a UML end marker from the given side of the line token, if present.
ClassRelEnd ClassParser::chopEndMarker(QString &side, bool leftSide)
{
    auto chop = [&](int n) {
        if (leftSide)
            side.chop(n);
        else
            side.remove(0, n);
        side = side.trimmed();
    };
    const QString probe = side;
    auto endsWith = [&](const char *tok) {
        return leftSide ? probe.endsWith(QLatin1String(tok))
                        : probe.startsWith(QLatin1String(tok));
    };
    if (endsWith("<|") || endsWith("|>")) {
        chop(2);
        return ClassRelEnd::Extension;
    }
    if (endsWith("()")) {
        chop(2);
        return ClassRelEnd::Lollipop;
    }
    if (endsWith("*")) {
        chop(1);
        return ClassRelEnd::Composition;
    }
    if (endsWith("<") || endsWith(">")) {
        chop(1);
        return ClassRelEnd::Dependency;
    }
    // `o` is aggregation only as a standalone token, never a name's tail.
    if (leftSide && probe.endsWith(u'o')) {
        const int before = probe.size() - 2;
        if (before < 0 || probe.at(before).isSpace() || probe.at(before) == u'"') {
            chop(1);
            return ClassRelEnd::Aggregation;
        }
    }
    if (!leftSide && probe.startsWith(u'o')) {
        if (probe.size() == 1 || probe.at(1).isSpace() || probe.at(1) == u'"') {
            chop(1);
            return ClassRelEnd::Aggregation;
        }
    }
    return ClassRelEnd::None;
}

bool ClassParser::parseRelation(const QString &line, int lineNo)
{
    bool dotted = false;
    const int pos = findLineToken(line, &dotted);
    if (pos < 0)
        return false;
    // A `:` before the line token means `Class : member text` — the LABEL
    // token wins upstream, so text like `A : --flag` is a member, not a
    // relation.
    {
        bool inQuote = false;
        for (int i = 0; i < pos; ++i) {
            const QChar c = line.at(i);
            if (c == u'"')
                inQuote = !inQuote;
            else if (c == u':' && !inQuote)
                return false;
        }
    }

    ClassRelation rel;
    rel.dotted = dotted;
    QString left = line.left(pos).trimmed();
    QString right = line.mid(pos + 2);

    // A trailing `: label` (outside quotes).
    {
        bool inQuote = false;
        for (int i = 0; i < right.size(); ++i) {
            const QChar c = right.at(i);
            if (c == u'"')
                inQuote = !inQuote;
            else if (c == u':' && !inQuote) {
                rel.label = right.mid(i + 1).trimmed().left(kMaxLabelChars);
                right = right.left(i);
                break;
            }
        }
        right = right.trimmed();
    }

    rel.fromEnd = chopEndMarker(left, true);
    rel.toEnd = chopEndMarker(right, false);

    // Quoted cardinalities adjacent to the relation.
    if (left.endsWith(u'"')) {
        const int open = left.lastIndexOf(u'"', left.size() - 2);
        if (open >= 0) {
            rel.fromCard = left.mid(open + 1, left.size() - open - 2);
            left = left.left(open).trimmed();
        }
    }
    if (right.startsWith(u'"')) {
        const int close = right.indexOf(u'"', 1);
        if (close > 0) {
            rel.toCard = right.mid(1, close - 1);
            right = right.mid(close + 1).trimmed();
        }
    }

    if (left.isEmpty() || right.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected class names on both sides of "
                                    "the relation"));
        return true;
    }
    const int a = ensureClass(left, lineNo);
    const int b = ensureClass(right, lineNo);
    if (a < 0 || b < 0)
        return true;
    if (m_ast.relations.size() >= kMaxEdges) {
        if (!m_relationCapWarned) {
            diag(lineNo, QStringLiteral("Too many relationships (limit %1)")
                             .arg(kMaxEdges));
            m_relationCapWarned = true;
        }
        return true;
    }
    rel.from = m_ast.classes.at(a).id;
    rel.to = m_ast.classes.at(b).id;
    rel.order = m_ast.relations.size();
    rel.srcSpan = m_curSpan;
    m_ast.relations.append(rel);
    return true;
}

void ClassParser::parseClassDecl(const QString &restIn, int lineNo)
{
    QString rest = restIn.trimmed();
    bool openBody = false;
    if (rest.endsWith(u'{')) {
        openBody = true;
        rest.chop(1);
        rest = rest.trimmed();
    }
    // `class Name <<annotation>>`
    QString annotation;
    const int annStart = rest.indexOf(QLatin1String("<<"));
    if (annStart >= 0) {
        const int annEnd = rest.indexOf(QLatin1String(">>"), annStart);
        if (annEnd > annStart) {
            annotation = rest.mid(annStart + 2, annEnd - annStart - 2).trimmed();
            rest = (rest.left(annStart) + rest.mid(annEnd + 2)).trimmed();
        }
    }
    // `class Name:::styleClass`
    QString cssClass;
    const int trip = rest.indexOf(QLatin1String(":::"));
    if (trip >= 0) {
        cssClass = rest.mid(trip + 3).trimmed();
        rest = rest.left(trip).trimmed();
    }
    // `class Name["Display label"]`
    QString label;
    if (rest.endsWith(u']')) {
        const int open = rest.indexOf(u'[');
        if (open > 0) {
            label = rest.mid(open + 1, rest.size() - open - 2).trimmed();
            if (label.size() >= 2 && label.startsWith(u'"')
                && label.endsWith(u'"'))
                label = label.mid(1, label.size() - 2);
            rest = rest.left(open).trimmed();
        }
    }
    if (rest.isEmpty()) {
        diag(lineNo, QStringLiteral("Expected a class name"));
        return;
    }
    const int i = ensureClass(rest, lineNo);
    if (i < 0)
        return;
    if (!label.isEmpty())
        m_ast.classes[i].label = label.left(kMaxLabelChars);
    if (!annotation.isEmpty())
        m_ast.classes[i].annotation = annotation;
    if (!cssClass.isEmpty() && !m_ast.classes[i].cssClasses.contains(cssClass))
        m_ast.classes[i].cssClasses.append(cssClass);
    if (openBody) {
        if (m_scopes.size() >= kMaxDepth) {
            diag(lineNo, QStringLiteral("Nesting too deep (limit %1)")
                             .arg(kMaxDepth));
            return;
        }
        m_scopes.append({ ClassBodyScope, i, lineNo });
    }
}

void ClassParser::parseStatement(QString line, int lineNo)
{
    // Class-body members are raw text until the closing brace.
    if (!m_scopes.isEmpty() && m_scopes.last().kind == ClassBodyScope) {
        if (line == QLatin1String("}")) {
            m_scopes.removeLast();
            return;
        }
        QString member = line;
        if (member.endsWith(u'}')) {
            member.chop(1);
            addMemberText(m_scopes.last().index, member);
            m_scopes.removeLast();
            return;
        }
        addMemberText(m_scopes.last().index, member);
        return;
    }

    if (line == QLatin1String("}")) {
        if (m_scopes.isEmpty()) {
            diag(lineNo, QStringLiteral("`}` without an open block"));
            return;
        }
        m_scopes.removeLast();
        return;
    }

    auto keyword = [&](const char *kw, QString *rest) {
        const int n = int(qstrlen(kw));
        if (!line.startsWith(QLatin1String(kw)))
            return false;
        if (line.size() > n && !line.at(n).isSpace())
            return false;
        if (rest)
            *rest = line.mid(n).trimmed();
        return true;
    };

    QString rest;
    if (line.startsWith(QLatin1String("classDiagram"))) {
        // Header; anything after it on the same line is a statement.
        rest = line.mid(line.startsWith(QLatin1String("classDiagram-v2"))
                            ? 15 : 12).trimmed();
        if (!rest.isEmpty())
            parseStatement(rest, lineNo);
        return;
    }
    if (keyword("direction", &rest)) {
        if (rest == QLatin1String("TB") || rest == QLatin1String("TD"))
            m_ast.direction = Direction::TB;
        else if (rest == QLatin1String("BT"))
            m_ast.direction = Direction::BT;
        else if (rest == QLatin1String("LR"))
            m_ast.direction = Direction::LR;
        else if (rest == QLatin1String("RL"))
            m_ast.direction = Direction::RL;
        return;
    }
    if (keyword("namespace", &rest)) {
        bool openBody = false;
        if (rest.endsWith(u'{')) {
            openBody = true;
            rest.chop(1);
            rest = rest.trimmed();
        }
        ClassNamespace ns;
        ns.name = cleanClassName(rest);
        m_ast.namespaces.append(ns);
        if (openBody) {
            if (m_scopes.size() >= kMaxDepth) {
                diag(lineNo, QStringLiteral("Nesting too deep (limit %1)")
                                 .arg(kMaxDepth));
                return;
            }
            m_scopes.append({ NamespaceScope, int(m_ast.namespaces.size()) - 1,
                              lineNo });
        }
        return;
    }
    if (keyword("class", &rest)) {
        parseClassDecl(rest, lineNo);
        return;
    }
    if (keyword("note", &rest)) {
        ClassNote note;
        if (rest.startsWith(QLatin1String("for "))) {
            QString tail = rest.mid(4).trimmed();
            const int quote = tail.indexOf(u'"');
            if (quote > 0) {
                note.forClass = cleanClassName(tail.left(quote));
                tail = tail.mid(quote);
            }
            rest = tail;
            if (!note.forClass.isEmpty())
                ensureClass(note.forClass, lineNo);
        }
        QString text = rest.trimmed();
        if (text.size() >= 2 && text.startsWith(u'"') && text.endsWith(u'"'))
            text = text.mid(1, text.size() - 2);
        note.text = text.left(kMaxLabelChars);
        if (!note.text.isEmpty())
            m_ast.notes.append(note);
        return;
    }
    if (keyword("classDef", &rest)) {
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QStringList names =
                rest.left(space).split(u',', Qt::SkipEmptyParts);
            const QString styles = rest.mid(space + 1);
            for (const QString &name : names) {
                ClassDef def = m_ast.classDefs.value(name.trimmed());
                parseStyleDeclarations(def, styles);
                m_ast.classDefs.insert(name.trimmed(), def);
            }
        }
        return;
    }
    if (keyword("style", &rest)) {
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QString id = cleanClassName(rest.left(space));
            const QString synth = QStringLiteral("__style_") + id;
            ClassDef def = m_ast.classDefs.value(synth);
            parseStyleDeclarations(def, rest.mid(space + 1));
            m_ast.classDefs.insert(synth, def);
            const int i = ensureClass(id, lineNo);
            if (i >= 0 && !m_ast.classes[i].cssClasses.contains(synth))
                m_ast.classes[i].cssClasses.append(synth);
        }
        return;
    }
    if (keyword("cssClass", &rest)) {
        // cssClass "A,B" styleName
        const int close = rest.lastIndexOf(u'"');
        const int open = rest.indexOf(u'"');
        if (open == 0 && close > open) {
            const QStringList ids = rest.mid(1, close - 1)
                                        .split(u',', Qt::SkipEmptyParts);
            const QString cls = rest.mid(close + 1).trimmed();
            for (const QString &id : ids) {
                const int i = ensureClass(id.trimmed(), lineNo);
                if (i >= 0 && !cls.isEmpty()
                    && !m_ast.classes[i].cssClasses.contains(cls))
                    m_ast.classes[i].cssClasses.append(cls);
            }
        }
        return;
    }
    for (const char *kw : { "click", "callback", "link" }) {
        if (keyword(kw, nullptr)) {
            diag(lineNo,
                 QStringLiteral("`%1` is ignored in Kvit (interactivity is "
                                "not supported)").arg(QLatin1String(kw)),
                 Diagnostic::Warning);
            return;
        }
    }
    if (line.startsWith(QLatin1String("accTitle"))) {
        const int colon = line.indexOf(u':');
        if (colon >= 0)
            m_ast.accTitle = line.mid(colon + 1).trimmed();
        return;
    }
    if (line.startsWith(QLatin1String("accDescr"))) {
        const int colon = line.indexOf(u':');
        const int brace = line.indexOf(u'{');
        if (colon >= 0 && (brace < 0 || colon < brace))
            m_ast.accDescr = line.mid(colon + 1).trimmed();
        else if (brace >= 0)
            m_ast.accDescr = line.mid(brace + 1).remove(u'}').trimmed();
        return;
    }
    // `<<annotation>> ClassName`
    if (line.startsWith(QLatin1String("<<"))) {
        const int end = line.indexOf(QLatin1String(">>"));
        if (end > 0) {
            const QString ann = line.mid(2, end - 2).trimmed();
            const QString cls = line.mid(end + 2).trimmed();
            const int i = ensureClass(cls, lineNo);
            if (i >= 0)
                m_ast.classes[i].annotation = ann;
            return;
        }
    }

    // Relations before member statements: a `:` may follow the relation.
    if (parseRelation(line, lineNo))
        return;

    // `ClassName : member text` / `ClassName:::styleClass`
    const int trip = line.indexOf(QLatin1String(":::"));
    if (trip > 0) {
        const int i = ensureClass(line.left(trip), lineNo);
        const QString cls = line.mid(trip + 3).trimmed();
        if (i >= 0 && !cls.isEmpty()
            && !m_ast.classes[i].cssClasses.contains(cls))
            m_ast.classes[i].cssClasses.append(cls);
        return;
    }
    const int colon = line.indexOf(u':');
    if (colon > 0) {
        const int i = ensureClass(line.left(colon), lineNo);
        addMemberText(i, line.mid(colon + 1));
        return;
    }

    // A bare class name is a valid (no-op) member statement upstream.
    bool bareWord = !line.isEmpty();
    for (const QChar c : line) {
        if (!c.isLetterOrNumber() && c != u'_' && c != u'.' && c != u'~'
            && c != u'`')
            bareWord = false;
    }
    if (bareWord)
        return;

    diag(lineNo, QStringLiteral("Unrecognized class-diagram statement: %1")
                     .arg(line.left(40)));
}

void ClassParser::run(const QString &body)
{
    const QStringList physical = body.split(u'\n');
    QList<int> lineOffset(physical.size() + 1, 0);
    for (int i = 0; i < physical.size(); ++i)
        lineOffset[i + 1] = lineOffset.at(i) + physical.at(i).size() + 1;

    bool inAccDescr = false;
    for (int li = 0; li < physical.size(); ++li) {
        QString raw = stripComment(physical.at(li));
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
        if (probe.startsWith(QLatin1String("accDescr"))
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
        int lead = 0;
        while (lead < raw.size() && raw.at(lead).isSpace())
            ++lead;
        m_curSpan = { m_base + lineOffset.at(li) + lead, int(probe.size()) };
        parseStatement(probe, li + 1);
    }

    for (const Scope &s : m_scopes) {
        diag(s.line, s.kind == ClassBodyScope
                 ? QStringLiteral("Class body is missing its `}`")
                 : QStringLiteral("Namespace is missing its `}`"));
    }
}

} // namespace

void parseClassDiagram(const QString &body, int baseOffset,
                       ParseResult &result)
{
    ClassParser parser(result, baseOffset);
    parser.run(body);
}

} // namespace Mermaid
