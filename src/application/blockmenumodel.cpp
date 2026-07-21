// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at https://mozilla.org/MPL/2.0/.
#include "blockmenumodel.h"

#include <QVariantMap>

#include <algorithm>

#include "codelanguages.h"
#include "fuzzymatch.h"

namespace {
// Human labels for the canonical language ids, so the /code menu reads
// "Code: Python" rather than "code: python".
QString languageDisplayName(const QString &id)
{
    static const QHash<QString, QString> names = {
        {"python", "Python"}, {"javascript", "JavaScript"}, {"cpp", "C++"},
        {"java", "Java"}, {"html", "HTML"}, {"css", "CSS"}, {"sql", "SQL"},
        {"bash", "Bash"}, {"json", "JSON"}, {"xml", "XML"},
        {"markdown", "Markdown"},
    };
    return names.value(id, id);
}
} // namespace

BlockMenuModel::BlockMenuModel(QObject *parent)
    : QObject(parent)
{
    // The full implemented set (features.md §4.2 minus the wave-2 types,
    // which join the catalog together with their block types). Names
    // match the status bar's; entries are grouped into Basic, Lists,
    // Advanced and Media; icons are typographic glyphs until a drawn
    // icon set lands. Aliases feed the fuzzy filter only — they are
    // never displayed.
    m_catalog = {
        { Block::Paragraph, QStringLiteral("Text"),
          QStringLiteral("Plain paragraph"),
          QStringLiteral("Basic"), QStringLiteral("¶"),
          { QStringLiteral("paragraph"), QStringLiteral("plain"),
            QStringLiteral("p") } },
        { Block::Heading1, QStringLiteral("Heading 1"),
          QStringLiteral("Largest heading, for titles"),
          QStringLiteral("Basic"), QStringLiteral("H1"),
          { QStringLiteral("h1"), QStringLiteral("heading1"),
            QStringLiteral("title"), QStringLiteral("#") } },
        { Block::Heading2, QStringLiteral("Heading 2"),
          QStringLiteral("Section heading"),
          QStringLiteral("Basic"), QStringLiteral("H2"),
          { QStringLiteral("h2"), QStringLiteral("heading2"),
            QStringLiteral("section"), QStringLiteral("##") } },
        { Block::Heading3, QStringLiteral("Heading 3"),
          QStringLiteral("Subsection heading"),
          QStringLiteral("Basic"), QStringLiteral("H3"),
          { QStringLiteral("h3"), QStringLiteral("heading3"),
            QStringLiteral("subsection"), QStringLiteral("###") } },
        { Block::Heading4, QStringLiteral("Heading 4"),
          QStringLiteral("Minor heading"),
          QStringLiteral("Basic"), QStringLiteral("H4"),
          { QStringLiteral("h4"), QStringLiteral("heading4"),
            QStringLiteral("####") } },
        { Block::BulletList, QStringLiteral("Bulleted List"),
          QStringLiteral("Unordered list item"),
          QStringLiteral("Lists"), QStringLiteral("•"),
          { QStringLiteral("bullet"), QStringLiteral("unordered"),
            QStringLiteral("ul"), QStringLiteral("list"),
            QStringLiteral("-") } },
        { Block::NumberedList, QStringLiteral("Numbered List"),
          QStringLiteral("Ordered list item"),
          QStringLiteral("Lists"), QStringLiteral("1."),
          { QStringLiteral("numbered"), QStringLiteral("ordered"),
            QStringLiteral("ol"), QStringLiteral("1.") } },
        { Block::Todo, QStringLiteral("To-do"),
          QStringLiteral("Checkbox item"),
          QStringLiteral("Lists"), QStringLiteral("☐"),
          { QStringLiteral("todo"), QStringLiteral("task"),
            QStringLiteral("checkbox"), QStringLiteral("check"),
            QStringLiteral("[]") } },
        { Block::Quote, QStringLiteral("Quote"),
          QStringLiteral("Block quotation"),
          QStringLiteral("Advanced"), QStringLiteral("❝"),
          { QStringLiteral("quote"), QStringLiteral("blockquote"),
            QStringLiteral(">") } },
        { Block::CodeBlock, QStringLiteral("Code Block"),
          QStringLiteral("Verbatim monospace code"),
          QStringLiteral("Advanced"), QStringLiteral("<>"),
          { QStringLiteral("code"), QStringLiteral("codeblock"),
            QStringLiteral("monospace"), QStringLiteral("snippet"),
            QStringLiteral("```") } },
        { Block::Divider, QStringLiteral("Divider"),
          QStringLiteral("Horizontal separator"),
          QStringLiteral("Advanced"), QStringLiteral("—"),
          { QStringLiteral("divider"), QStringLiteral("hr"),
            QStringLiteral("rule"), QStringLiteral("separator"),
            QStringLiteral("line"), QStringLiteral("---") } },
        // Callout and Toggle: both insert a Callout block,
        // seeded with a type (info) or the toggle marker via `language`.
        { Block::Callout, QStringLiteral("Callout"),
          QStringLiteral("Highlighted info/warning/tip box"),
          QStringLiteral("Advanced"), QStringLiteral("!"),
          { QStringLiteral("callout"), QStringLiteral("admonition"),
            QStringLiteral("note"), QStringLiteral("info"),
            QStringLiteral("warning"), QStringLiteral("[!") },
          QStringLiteral("info") },
        { Block::Callout, QStringLiteral("Toggle"),
          QStringLiteral("Collapsible section"),
          QStringLiteral("Advanced"), QStringLiteral("▸"),
          { QStringLiteral("toggle"), QStringLiteral("collapse"),
            QStringLiteral("fold"), QStringLiteral("details") },
          QStringLiteral("toggle") },
        { Block::Table, QStringLiteral("Table"),
          QStringLiteral("Grid with rows and columns"),
          QStringLiteral("Advanced"), QStringLiteral("▦"),
          { QStringLiteral("table"), QStringLiteral("grid"),
            QStringLiteral("spreadsheet") } },
        // Task Board: a `kanban`-tagged code fence. Rides the
        // convertBlock(language) path like Callout/Toggle,
        // seeded with starter columns by BlockMenu.
        { Block::CodeBlock, QStringLiteral("Task Board"),
          QStringLiteral("Kanban columns of draggable cards"),
          QStringLiteral("Advanced"), QStringLiteral("▤"),
          { QStringLiteral("kanban"), QStringLiteral("board"),
            QStringLiteral("task"), QStringLiteral("tasks"),
            QStringLiteral("todo"), QStringLiteral("trello") },
          QStringLiteral("kanban") },
        // Table of contents: a `toc`-tagged code fence,
        // rides the convertBlock(language) path like Task Board; BlockMenu
        // seeds it with the document's current headings.
        { Block::CodeBlock, QStringLiteral("Table of Contents"),
          QStringLiteral("Auto-generated list of headings"),
          QStringLiteral("Advanced"), QStringLiteral("☰"),
          { QStringLiteral("toc"), QStringLiteral("contents"),
            QStringLiteral("outline"), QStringLiteral("index"),
            QStringLiteral("headings") },
          QStringLiteral("toc") },
        // Math block: a $$ … $$ display-math fence rendered through MicroTeX.
        { Block::MathBlock, QStringLiteral("Math Block"),
          QStringLiteral("LaTeX equation, rendered"),
          QStringLiteral("Advanced"), QStringLiteral("∑"),
          { QStringLiteral("math"), QStringLiteral("equation"),
            QStringLiteral("latex"), QStringLiteral("tex"),
            QStringLiteral("formula"), QStringLiteral("$$") } },
        // Mermaid diagram: a `mermaid`-tagged code fence rendered natively.
        // BlockMenu seeds a small flowchart.
        { Block::CodeBlock, QStringLiteral("Mermaid Diagram"),
          QStringLiteral("Flowchart and graph diagrams from Mermaid syntax"),
          QStringLiteral("Advanced"), QStringLiteral("◈"),
          { QStringLiteral("mermaid"), QStringLiteral("flowchart"),
            QStringLiteral("graph"), QStringLiteral("flow"),
            QStringLiteral("diagram") },
          QStringLiteral("mermaid") },
        // Collection query: a `query`-tagged code fence rendering a live
        // table/board over the collection's front-matter. BlockMenu seeds a
        // commented starter spec.
        { Block::CodeBlock, QStringLiteral("Collection Query"),
          QStringLiteral("Live table or board over your notes' front-matter"),
          QStringLiteral("Advanced"), QStringLiteral("⌕"),
          { QStringLiteral("query"), QStringLiteral("database"),
            QStringLiteral("dataview"), QStringLiteral("filter"),
            QStringLiteral("frontmatter") },
          QStringLiteral("query") },
        // Drop cap (§1.2.16): a paragraph attribute rather than a stored type,
        // so the `dropcap` marker in defaultLanguage routes the menu to set
        // dropcap=<lines> on the target and keep its text, instead of running
        // the convert path (which clears content).
        { Block::Paragraph, QStringLiteral("Drop Cap"),
          QStringLiteral("Enlarge this paragraph's first letter"),
          QStringLiteral("Advanced"), QStringLiteral("A"),
          { QStringLiteral("dropcap"), QStringLiteral("drop cap"),
            QStringLiteral("initial"), QStringLiteral("illuminated"),
            QStringLiteral("capital") },
          QStringLiteral("dropcap") },
        // Media group (§4.3). Image inserts (file dialog or URL) rather than
        // converts.
        { Block::Image, QStringLiteral("Image"),
          QStringLiteral("Embed an image from a file or URL"),
          QStringLiteral("Media"), QStringLiteral("▨"),
          { QStringLiteral("image"), QStringLiteral("img"),
            QStringLiteral("picture"), QStringLiteral("photo"),
            QStringLiteral("![") } },
        // Local audio/video: inserts like an image (file dialog); the path's
        // extension lands it as a Media block.
        { Block::Media, QStringLiteral("Audio / Video"),
          QStringLiteral("Play a local audio or video file"),
          QStringLiteral("Media"), QStringLiteral("▷"),
          { QStringLiteral("audio"), QStringLiteral("video"),
            QStringLiteral("media"), QStringLiteral("sound"),
            QStringLiteral("movie"), QStringLiteral("mp4"),
            QStringLiteral("mp3") } },
        // Web embed: a preview card for a web page or
        // video URL. Stored as an ![](url) image expression (no new type); the
        // `embed` marker in defaultLanguage routes it to the URL prompt.
        { Block::Image, QStringLiteral("Web Embed"),
          QStringLiteral("Preview card for a web page or video URL"),
          QStringLiteral("Media"), QStringLiteral("◧"),
          { QStringLiteral("embed"), QStringLiteral("bookmark"),
            QStringLiteral("link"), QStringLiteral("url"),
            QStringLiteral("youtube"), QStringLiteral("web") },
          QStringLiteral("embed") },
    };
}

bool BlockMenuModel::isSubsequence(const QString &needle, const QString &haystack)
{
    return FuzzyMatch::isSubsequence(needle, haystack);
}

BlockMenuModel::MatchTier BlockMenuModel::matchTier(const Entry &entry,
                                                    const QString &loweredQuery) const
{
    QStringList candidates = entry.aliases;
    candidates.prepend(entry.name);
    // The shared matcher (fuzzymatch.h) so the
    // block menu, quick switcher, and [[ completion rank identically; the
    // tier enums map one to one.
    return MatchTier(FuzzyMatch::tierFor(loweredQuery, candidates));
}

QString BlockMenuModel::entryId(const Entry &entry)
{
    return QString::number(static_cast<int>(entry.type))
         + QLatin1Char(':') + entry.defaultLanguage;
}

const BlockMenuModel::Entry *BlockMenuModel::entryForId(const QString &id) const
{
    for (const Entry &entry : m_catalog) {
        if (entryId(entry) == id)
            return &entry;
    }
    return nullptr;
}

QVariantMap BlockMenuModel::entryRow(const Entry &entry) const
{
    QVariantMap row{
        { QStringLiteral("kind"), QStringLiteral("entry") },
        { QStringLiteral("entryId"), entryId(entry) },
        { QStringLiteral("name"), entry.name },
        { QStringLiteral("description"), entry.description },
        { QStringLiteral("icon"), entry.icon },
        { QStringLiteral("type"), static_cast<int>(entry.type) },
    };
    if (!entry.defaultLanguage.isEmpty())
        row.insert(QStringLiteral("language"), entry.defaultLanguage);
    return row;
}

QVariantMap BlockMenuModel::headerRow(const QString &text) const
{
    return {
        { QStringLiteral("kind"), QStringLiteral("header") },
        { QStringLiteral("text"), text },
    };
}

QVariantList BlockMenuModel::itemsFor(const QString &query) const
{
    const QString lowered = query.trimmed().toLower();
    QVariantList rows;

    if (lowered.isEmpty()) {
        // Recently used first (§3.7), then the catalog under its group
        // headers in canonical order.
        if (!m_recent.isEmpty()) {
            rows.append(headerRow(QStringLiteral("Recently used")));
            for (const QString &id : m_recent) {
                if (const Entry *entry = entryForId(id))
                    rows.append(entryRow(*entry));
            }
        }
        QString currentGroup;
        for (const Entry &entry : m_catalog) {
            if (entry.group != currentGroup) {
                currentGroup = entry.group;
                rows.append(headerRow(currentGroup));
            }
            rows.append(entryRow(entry));
        }
        return rows;
    }

    // "/code <language>": the query after the "code " prefix matches
    // language names and aliases, and selecting one inserts a code block
    // already tagged with that language. Only the plain query "code" (no
    // remainder) falls through to the ordinary Code Block entry.
    if (lowered.startsWith(QStringLiteral("code")) && lowered.contains(QLatin1Char(' '))) {
        const QString remainder =
            lowered.mid(lowered.indexOf(QLatin1Char(' ')) + 1).trimmed();
        if (!remainder.isEmpty()) {
            QVariantList langRows;
            // An exact alias (e.g. "py", "c++") leads.
            const QString exact = CodeLanguages::canonicalLanguage(remainder);
            const auto codeRow = [](const QString &id) {
                return QVariantMap{
                    { QStringLiteral("kind"), QStringLiteral("entry") },
                    { QStringLiteral("name"),
                      QStringLiteral("Code: ") + languageDisplayName(id) },
                    { QStringLiteral("description"),
                      languageDisplayName(id) + QStringLiteral(" syntax highlighting") },
                    { QStringLiteral("icon"), QStringLiteral("<>") },
                    { QStringLiteral("type"), static_cast<int>(Block::CodeBlock) },
                    { QStringLiteral("language"), id },
                };
            };
            if (!exact.isEmpty())
                langRows.append(codeRow(exact));
            for (const QString &id : CodeLanguages::supportedLanguages()) {
                if (id == exact)
                    continue;
                if (id.startsWith(remainder) || isSubsequence(remainder, id))
                    langRows.append(codeRow(id));
            }
            if (!langRows.isEmpty())
                return langRows;
        }
    }

    // Filtering: a flat ranked list. Stable sort keeps catalog order
    // within a tier, so "Heading 1" precedes "Heading 2" for query "h".
    QList<QPair<MatchTier, const Entry *>> matches;
    for (const Entry &entry : m_catalog) {
        const MatchTier tier = matchTier(entry, lowered);
        if (tier != NoMatch)
            matches.append({ tier, &entry });
    }
    std::stable_sort(matches.begin(), matches.end(),
                     [](const auto &a, const auto &b) { return a.first < b.first; });
    for (const auto &match : matches)
        rows.append(entryRow(*match.second));
    return rows;
}

void BlockMenuModel::noteUsedEntry(const QString &entryId)
{
    if (!entryForId(entryId))
        return;  // not a catalog entry; nothing to remember
    m_recent.removeAll(entryId);
    m_recent.prepend(entryId);
    while (m_recent.size() > MaxRecent)
        m_recent.removeLast();
    emit recentChanged();
}

void BlockMenuModel::noteUsed(int type)
{
    // The plain entry for a type is the one with no default language; a
    // type whose every entry is specialized falls back to the first.
    const QString plain = QString::number(type) + QLatin1Char(':');
    if (entryForId(plain)) {
        noteUsedEntry(plain);
        return;
    }
    for (const Entry &entry : m_catalog) {
        if (static_cast<int>(entry.type) == type) {
            noteUsedEntry(entryId(entry));
            return;
        }
    }
}

QVariantList BlockMenuModel::recentTypes() const
{
    QVariantList list;
    for (const QString &id : m_recent)
        list.append(id);
    return list;
}

void BlockMenuModel::setRecentTypes(const QVariantList &types)
{
    m_recent.clear();
    for (const QVariant &value : types) {
        QString id;
        // Entry ids are strings. Settings written by earlier versions hold
        // plain block-type numbers (JSON delivers them as doubles), which
        // resolve to that type's plain entry so recency survives the
        // upgrade instead of being silently dropped.
        if (value.typeId() == QMetaType::QString) {
            id = value.toString();
        } else {
            bool ok = false;
            const int type = value.toInt(&ok);
            if (!ok)
                continue;
            id = QString::number(type) + QLatin1Char(':');
        }
        if (id.isEmpty() || m_recent.contains(id))
            continue;
        if (!entryForId(id))
            continue;  // stale entry (or a hand-edited value)
        m_recent.append(id);
        if (m_recent.size() == MaxRecent)
            break;
    }
}
