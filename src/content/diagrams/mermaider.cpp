// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "mermaider.h"
#include "mermaidparser.h"

#include <QStringList>

namespace Mermaid {

namespace {

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

QString stripQuotes(QString s)
{
    s = s.trimmed();
    if (s.size() >= 2 && s.startsWith(u'"') && s.endsWith(u'"'))
        s = s.mid(1, s.size() - 2);
    return s;
}

// The verbose cardinality spellings, longest first.
struct CardWord { const char *word; ErCardinality card; };
const CardWord kCardWords[] = {
    { "one or zero", ErCardinality::ZeroOrOne },
    { "zero or one", ErCardinality::ZeroOrOne },
    { "zero or more", ErCardinality::ZeroOrMore },
    { "zero or many", ErCardinality::ZeroOrMore },
    { "one or more", ErCardinality::OneOrMore },
    { "one or many", ErCardinality::OneOrMore },
    { "many(0)", ErCardinality::ZeroOrMore },
    { "many(1)", ErCardinality::OneOrMore },
    { "only one", ErCardinality::OnlyOne },
    { "many", ErCardinality::ZeroOrMore },
    { "one", ErCardinality::OnlyOne },
    { "0+", ErCardinality::ZeroOrMore },
    { "1+", ErCardinality::OneOrMore },
    { "1", ErCardinality::OnlyOne },
};

class ErParser
{
public:
    ErParser(ParseResult &r, int baseOffset)
        : m_r(r), m_ast(r.er), m_base(baseOffset) {}

    void run(const QString &body);

private:
    void parseStatement(const QString &line, int lineNo);
    bool parseRelationship(const QString &line, int lineNo);
    void parseEntityLine(QString line, int lineNo);
    void parseAttributeLine(const QString &line, int lineNo);
    int ensureEntity(const QString &writtenName, int lineNo);
    // Strip a `:::a,b` class-list suffix from an entity reference.
    QString stripCssSuffix(QString name, QStringList *cssOut);
    bool chopLeftCardinality(QString &side, ErCardinality *card);
    bool chopRightCardinality(QString &side, ErCardinality *card);
    void diag(int line, const QString &msg,
              Diagnostic::Severity sev = Diagnostic::Error);

    ParseResult &m_r;
    ErAst &m_ast;
    int m_base = 0;
    SourceSpan m_curSpan;    // span of the statement being parsed
    int m_openEntity = -1;   // entity whose attribute block is open
    int m_openLine = 0;
    bool m_entityCapWarned = false;
    bool m_relationCapWarned = false;
};

void ErParser::diag(int line, const QString &msg, Diagnostic::Severity sev)
{
    m_r.diagnostics.append({ line, 1, msg, sev });
}

int ErParser::ensureEntity(const QString &writtenName, int lineNo)
{
    const QString id = stripQuotes(writtenName);
    if (id.isEmpty())
        return -1;
    int i = m_ast.indexOfEntity(id);
    if (i < 0) {
        if (m_ast.entities.size() >= kMaxNodes) {
            if (!m_entityCapWarned) {
                diag(lineNo, QStringLiteral("Too many entities (limit %1)")
                                 .arg(kMaxNodes));
                m_entityCapWarned = true;
            }
            return -1;
        }
        ErEntity e;
        e.id = id;
        e.label = id;
        e.order = m_ast.entities.size();
        e.srcSpan = m_curSpan;
        m_ast.entities.append(e);
        i = m_ast.entities.size() - 1;
    }
    return i;
}

QString ErParser::stripCssSuffix(QString name, QStringList *cssOut)
{
    const int trip = name.indexOf(QLatin1String(":::"));
    if (trip >= 0) {
        if (cssOut)
            *cssOut = name.mid(trip + 3).trimmed()
                          .split(u',', Qt::SkipEmptyParts);
        name = name.left(trip);
    }
    return name.trimmed();
}

bool ErParser::chopLeftCardinality(QString &side, ErCardinality *card)
{
    side = side.trimmed();
    struct Sym { const char *tok; ErCardinality c; };
    static const Sym kSyms[] = {
        { "}o", ErCardinality::ZeroOrMore },
        { "}|", ErCardinality::OneOrMore },
        { "|o", ErCardinality::ZeroOrOne },
        { "||", ErCardinality::OnlyOne },
    };
    for (const Sym &s : kSyms) {
        if (side.endsWith(QLatin1String(s.tok))) {
            side.chop(2);
            side = side.trimmed();
            *card = s.c;
            return true;
        }
    }
    if (side.endsWith(u'u')
        && (side.size() == 1 || side.at(side.size() - 2).isSpace())) {
        side.chop(1);
        side = side.trimmed();
        *card = ErCardinality::MdParent;
        return true;
    }
    for (const CardWord &w : kCardWords) {
        const QLatin1String tok(w.word);
        if (side.endsWith(tok, Qt::CaseInsensitive)) {
            const int before = side.size() - tok.size() - 1;
            if (before >= 0 && !side.at(before).isSpace())
                continue;
            side.chop(tok.size());
            side = side.trimmed();
            *card = w.card;
            return true;
        }
    }
    return false;
}

bool ErParser::chopRightCardinality(QString &side, ErCardinality *card)
{
    side = side.trimmed();
    struct Sym { const char *tok; ErCardinality c; };
    static const Sym kSyms[] = {
        { "o{", ErCardinality::ZeroOrMore },
        { "|{", ErCardinality::OneOrMore },
        { "o|", ErCardinality::ZeroOrOne },
        { "||", ErCardinality::OnlyOne },
    };
    for (const Sym &s : kSyms) {
        if (side.startsWith(QLatin1String(s.tok))) {
            side.remove(0, 2);
            side = side.trimmed();
            *card = s.c;
            return true;
        }
    }
    for (const CardWord &w : kCardWords) {
        const QLatin1String tok(w.word);
        if (side.startsWith(tok, Qt::CaseInsensitive)) {
            if (side.size() > tok.size() && !side.at(tok.size()).isSpace())
                continue;
            side.remove(0, tok.size());
            side = side.trimmed();
            *card = w.card;
            return true;
        }
    }
    return false;
}

bool ErParser::parseRelationship(const QString &line, int lineNo)
{
    // Symbolic line tokens outside quotes: -- (identifying), .. / .- / -.
    // (non-identifying). Textual: ` to ` / ` optionally to `.
    int pos = -1;
    int len = 0;
    bool identifying = true;
    {
        bool inQuote = false;
        for (int i = 0; i + 1 < line.size(); ++i) {
            const QChar a = line.at(i);
            if (a == u'"') {
                inQuote = !inQuote;
                continue;
            }
            if (inQuote)
                continue;
            const QChar b = line.at(i + 1);
            if (a == u'-' && b == u'-') {
                pos = i; len = 2; identifying = true;
                break;
            }
            if ((a == u'.' && b == u'.') || (a == u'.' && b == u'-')
                || (a == u'-' && b == u'.')) {
                pos = i; len = 2; identifying = false;
                break;
            }
        }
    }
    if (pos < 0) {
        // Textual relation words with surrounding whitespace.
        bool inQuote = false;
        for (int i = 0; i < line.size(); ++i) {
            if (line.at(i) == u'"') {
                inQuote = !inQuote;
                continue;
            }
            if (inQuote || !line.at(i).isSpace())
                continue;
            const QString rest = line.mid(i + 1);
            if (rest.startsWith(QLatin1String("optionally to "),
                                Qt::CaseInsensitive)) {
                pos = i + 1; len = 13; identifying = false;
                break;
            }
            if (rest.startsWith(QLatin1String("to "), Qt::CaseInsensitive)) {
                pos = i + 1; len = 2; identifying = true;
                break;
            }
        }
    }
    if (pos < 0)
        return false;

    QString left = line.left(pos).trimmed();
    QString right = line.mid(pos + len).trimmed();

    // Role after the first `:` that is not a `:::` separator.
    QString role;
    bool haveRole = false;
    {
        bool inQuote = false;
        for (int i = 0; i < right.size(); ++i) {
            const QChar c = right.at(i);
            if (c == u'"') {
                inQuote = !inQuote;
                continue;
            }
            if (c != u':' || inQuote)
                continue;
            if (right.mid(i, 3) == QLatin1String(":::")) {
                i += 2;
                continue;
            }
            role = stripQuotes(right.mid(i + 1).trimmed())
                       .left(kMaxLabelChars);
            right = right.left(i).trimmed();
            haveRole = true;
            break;
        }
    }

    ErCardinality cardLeft = ErCardinality::OnlyOne;
    ErCardinality cardRight = ErCardinality::OnlyOne;
    const bool haveLeft = chopLeftCardinality(left, &cardLeft);
    const bool haveRight = chopRightCardinality(right, &cardRight);
    if (!haveLeft || !haveRight) {
        diag(lineNo, QStringLiteral("Expected a cardinality on both sides of "
                                    "the relationship"));
        return true;
    }
    if (!haveRole) {
        diag(lineNo,
             QStringLiteral("Expected `:` and a relationship label"));
        return true;
    }

    QStringList cssLeft, cssRight;
    left = stripCssSuffix(left, &cssLeft);
    right = stripCssSuffix(right, &cssRight);
    const int a = ensureEntity(left, lineNo);
    const int b = ensureEntity(right, lineNo);
    if (a < 0 || b < 0)
        return true;
    for (const QString &c : cssLeft)
        if (!m_ast.entities[a].cssClasses.contains(c.trimmed()))
            m_ast.entities[a].cssClasses.append(c.trimmed());
    for (const QString &c : cssRight)
        if (!m_ast.entities[b].cssClasses.contains(c.trimmed()))
            m_ast.entities[b].cssClasses.append(c.trimmed());

    if (m_ast.relationships.size() >= kMaxEdges) {
        if (!m_relationCapWarned) {
            diag(lineNo, QStringLiteral("Too many relationships (limit %1)")
                             .arg(kMaxEdges));
            m_relationCapWarned = true;
        }
        return true;
    }
    ErRelationship rel;
    rel.from = m_ast.entities.at(a).id;
    rel.to = m_ast.entities.at(b).id;
    rel.fromCard = cardLeft;
    rel.toCard = cardRight;
    rel.identifying = identifying;
    rel.label = role;
    rel.order = m_ast.relationships.size();
    rel.srcSpan = m_curSpan;
    m_ast.relationships.append(rel);
    return true;
}

void ErParser::parseAttributeLine(const QString &line, int lineNo)
{
    // Tokenize respecting quotes (comments) and backticks (literal words).
    QStringList tokens;
    QString cur;
    bool inQuote = false, inBq = false;
    bool curQuoted = false;
    QList<bool> quoted;
    for (const QChar c : line) {
        if (c == u'"' && !inBq) {
            inQuote = !inQuote;
            curQuoted = true;
            continue;
        }
        if (c == u'`' && !inQuote) {
            inBq = !inBq;
            continue;
        }
        if (c.isSpace() && !inQuote && !inBq) {
            if (!cur.isEmpty()) {
                tokens << cur;
                quoted << curQuoted;
                cur.clear();
                curQuoted = false;
            }
            continue;
        }
        cur += c;
    }
    if (!cur.isEmpty()) {
        tokens << cur;
        quoted << curQuoted;
    }
    if (tokens.isEmpty())
        return;
    if (tokens.size() < 2) {
        diag(lineNo, QStringLiteral("Expected an attribute type and name"));
        return;
    }
    ErAttribute attr;
    attr.type = tokens.at(0).left(kMaxLabelChars);
    attr.name = tokens.at(1).left(kMaxLabelChars);
    for (int i = 2; i < tokens.size(); ++i) {
        if (quoted.at(i)) {
            attr.comment = tokens.at(i).left(kMaxLabelChars);
            continue;
        }
        // A key list: PK / FK / UK, possibly comma-joined.
        const QStringList keys = tokens.at(i).toUpper()
                                     .split(u',', Qt::SkipEmptyParts);
        bool allKeys = !keys.isEmpty();
        for (const QString &k : keys)
            if (k != QLatin1String("PK") && k != QLatin1String("FK")
                && k != QLatin1String("UK"))
                allKeys = false;
        if (allKeys)
            attr.keys.append(keys);
        else
            diag(lineNo, QStringLiteral("Unexpected attribute token: %1")
                             .arg(tokens.at(i)), Diagnostic::Warning);
    }
    m_ast.entities[m_openEntity].attributes.append(attr);
}

void ErParser::parseEntityLine(QString line, int lineNo)
{
    bool openBlock = false;
    if (line.endsWith(u'{')) {
        openBlock = true;
        line.chop(1);
        line = line.trimmed();
    }
    QStringList css;
    line = stripCssSuffix(line, &css);
    // `NAME["alias"]` / `NAME[alias]`
    QString label;
    if (line.endsWith(u']')) {
        const int open = line.indexOf(u'[');
        if (open > 0) {
            label = stripQuotes(line.mid(open + 1, line.size() - open - 2));
            line = line.left(open).trimmed();
        }
    }
    const int i = ensureEntity(line, lineNo);
    if (i < 0)
        return;
    if (!label.isEmpty())
        m_ast.entities[i].label = label.left(kMaxLabelChars);
    for (const QString &c : css)
        if (!m_ast.entities[i].cssClasses.contains(c.trimmed()))
            m_ast.entities[i].cssClasses.append(c.trimmed());
    if (openBlock) {
        m_openEntity = i;
        m_openLine = lineNo;
    }
}

void ErParser::parseStatement(const QString &line, int lineNo)
{
    if (m_openEntity >= 0) {
        if (line == QLatin1String("}")) {
            m_openEntity = -1;
            return;
        }
        QString attr = line;
        if (attr.endsWith(u'}')) {
            attr.chop(1);
            attr = attr.trimmed();
            if (!attr.isEmpty())
                parseAttributeLine(attr, lineNo);
            m_openEntity = -1;
            return;
        }
        parseAttributeLine(attr, lineNo);
        return;
    }

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
    if (keyword("erDiagram", &rest)) {
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
    if (keyword("title", &rest)) {
        m_ast.title = rest;
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
        const int space = rest.indexOf(u' ');
        if (space > 0) {
            const QStringList ids =
                rest.left(space).split(u',', Qt::SkipEmptyParts);
            const QString cls = rest.mid(space + 1).trimmed();
            for (const QString &id : ids) {
                const int i = ensureEntity(id.trimmed(), lineNo);
                if (i >= 0 && !cls.isEmpty()
                    && !m_ast.entities[i].cssClasses.contains(cls))
                    m_ast.entities[i].cssClasses.append(cls);
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
                const int i = ensureEntity(id, lineNo);
                if (i >= 0 && !m_ast.entities[i].cssClasses.contains(synth))
                    m_ast.entities[i].cssClasses.append(synth);
            }
        }
        return;
    }

    if (parseRelationship(line, lineNo))
        return;

    parseEntityLine(line, lineNo);
}

void ErParser::run(const QString &body)
{
    const QStringList physical = body.split(u'\n');
    QList<int> lineOffset(physical.size() + 1, 0);
    for (int i = 0; i < physical.size(); ++i)
        lineOffset[i + 1] = lineOffset.at(i) + physical.at(i).size() + 1;

    bool inAccDescr = false;
    for (int li = 0; li < physical.size(); ++li) {
        const QString raw = stripComment(physical.at(li));
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
        if (probe.isEmpty() || probe.startsWith(u'#'))
            continue;
        {
            int lead = 0;
            while (lead < raw.size() && raw.at(lead).isSpace())
                ++lead;
            m_curSpan = { m_base + lineOffset.at(li) + lead,
                          int(probe.size()) };
        }
        if (probe.startsWith(QLatin1String("accDescr"), Qt::CaseInsensitive)
            && probe.contains(u'{') && !probe.contains(u'}')) {
            const int brace = probe.indexOf(u'{');
            const QString first = probe.mid(brace + 1).trimmed();
            if (!first.isEmpty())
                m_ast.accDescr = first;
            inAccDescr = true;
            continue;
        }
        parseStatement(probe, li + 1);
    }

    if (m_openEntity >= 0)
        diag(m_openLine, QStringLiteral("Entity block is missing its `}`"));
}

} // namespace

void parseErDiagram(const QString &body, int baseOffset, ParseResult &result)
{
    ErParser parser(result, baseOffset);
    parser.run(body);
}

} // namespace Mermaid
