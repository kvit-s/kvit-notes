# Hacker News: what happened to projects like this one

*Working notes, kept in `planning/`, which is gitignored. Compiled 2026-08-09 from
the Hacker News search API. This is the evidence file: a catalogue of how markdown
editors, note-taking applications, diagram tools and Qt projects have actually
performed on Hacker News, including the ones that failed. The conclusions drawn
from it, and what to do about them, are in `hn-strategy.md`. Nothing here is
recalled from memory; every number came from a query run on the date above and
can be re-run with the scripts described at the end.*

## Why this file exists

Kvit Notes is a native markdown block editor (Qt 6 C++/QML, MPL-2.0) preparing for
a public launch, and the plan makes a Show HN post the main launch event. The
private strategy document `to-market.md` set expectations from a sample of
high-scoring stories: a good Show HN "plausibly lands at 250-700 points and 1.5-4k
GitHub stars." That sample looked only at stories that succeeded. This file looks
at the whole population, successes and failures together, because the failure rate
is the number that actually governs planning.

### Method, and what it cannot tell you

Stories were pulled from the Algolia-hosted Hacker News search API across about a
hundred themed queries covering markdown, notes and personal knowledge management,
diagram tooling, text editors, local-first software, maths and typesetting, native
versus Electron framing, Qt and QML, named competitors, and open-source business
models. That harvest yielded 28,296 unique stories.

The harvest mixes relevance-ranked and recency-ranked results, which over-samples
old high-scoring stories and recent everything, so it cannot support a claim about
change over time. A second pass therefore enumerated **every** story matching six
core queries inside fixed time windows, using timestamp range filters and full
pagination. That census covers whole calendar years for 2023, 2024 and 2025, plus
1 January to 10 August 2026, split into half-years so the truncated final year can
be compared like for like. It holds 6,673 stories, of which 2,351 have on-topic
titles, and it is what the trend numbers below rest on. An earlier version of this
file used a January-to-July window for every year, which left the second halves of
2023, 2024 and 2025 unexamined; the fuller data moved the individual percentages
by a point or two and left the shape unchanged.

Two limits are worth stating plainly. Points are a noisy measure of a story's
merit, since ranking depends heavily on the first half hour and on what else was
submitted that morning. And every correlation below is confounded by project
quality: a title analysis cannot separate "this phrasing worked" from "better
projects tend to choose this phrasing." Where that matters, it is said again in
place.

Throughout, **100 points is used as the front-page marker** and 30 points as
"visible at all." Those thresholds match the ones the launch plan already uses.

## The headline finding: the front page did not shrink, the queue grew

This is the census over whole calendar years, filtered to stories whose titles are
actually on topic. 2026 runs to 10 August, which is 7.3 months rather than a full
year, and is marked accordingly.

**All on-topic stories:**

| Year | Stories | Median points | Reached 100+ | Reached 30+ | Under 5 points | 100+ hits |
|---|---:|---:|---:|---:|---:|---:|
| 2023 | 364 | 2 | 5.77% | 9.07% | 74.2% | 21 |
| 2024 | 449 | 3 | 7.35% | 12.03% | 70.4% | 33 |
| 2025 | 667 | 2 | 6.00% | 8.85% | 76.9% | 40 |
| 2026 (7.3 months) | 871 | 2 | 2.64% | 6.43% | 80.6% | 23 |

**Show HN submissions only:**

| Year | Show HN posts | Median | Reached 100+ | Reached 30+ | Under 5 points | Hits |
|---|---:|---:|---:|---:|---:|---:|
| 2023 | 82 | 4 | 10.98% | 15.85% | 58.5% | 9 |
| 2024 | 143 | 4 | 10.49% | 13.99% | 59.4% | 15 |
| 2025 | 257 | 2 | 5.45% | 7.78% | 78.2% | 14 |
| 2026 (7.3 months) | 401 | 2 | **2.00%** | 6.23% | **77.1%** | 8 |

**2025 is the year it changed, and the half-year split shows it happening:**

| Half-year | Show HN posts | Reached 100+ | Hits |
|---|---:|---:|---:|
| 2023 H1 | 40 | 10.00% | 4 |
| 2023 H2 | 42 | 11.90% | 5 |
| 2024 H1 | 56 | 10.71% | 6 |
| 2024 H2 | 87 | 10.34% | 9 |
| 2025 H1 | 112 | 7.14% | 8 |
| 2025 H2 | 145 | 4.14% | 6 |
| 2026 H1 | 334 | 2.40% | 8 |

The right-hand column is the interesting one. The **number** of on-topic Show HN
posts reaching the front page has sat between 4 and 9 in every half-year for four
years, with no trend. The number competing for those places went from 40 to 334,
an eight-fold rise. The front page has a fixed number of slots per day and this
category's share of them has not grown, so the per-post probability fell by
roughly a factor of five while the work required per post did not change.

The decline is not a 2026 event. The rate was stable near 10.5% through 2023 and
2024, halved during 2025 (7.14% then 4.14% across its two halves), and halved
again in 2026. Anyone whose intuitions about this category were formed before
about mid-2025 is working from a regime that no longer exists.

For a launch being planned now, the practical reading is that a markdown or notes
Show HN has about a **2% chance of clearing 100 points and about a 77% chance of
finishing under 5 points**. The `to-market.md` expectation of 250-700 points
describes the top few outcomes of a whole year, whereas the median post in this
category finishes in the low single digits.

## The catalogue

### Recent successes: Show HN, 2025-2026, 150 points or more

| Date | Title (the "Show HN:" prefix is stripped) | Pts | Cmts | HN id |
|---|---|---:|---:|---|
| 2026-05-18 | Files.md – Open-source alternative to Obsidian | 730 | 356 | 48179677 |
| 2025-08-17 | OverType – A Markdown WYSIWYG editor that's just a textarea | 471 | 99 | 44932651 |
| 2026-06-23 | TikZ Editor – WYSIWYG editor for figures in LaTeX | 451 | 74 | 48645437 |
| 2025-05-22 | Defuddle, an HTML-to-Markdown alternative to Readability | 418 | 68 | 44067409 |
| 2026-06-25 | OpenKnowledge – open source AI-first alternative to Obsidian/Notion | 381 | 173 | 48675435 |
| 2026-03-24 | Email.md – Markdown to responsive, email-safe HTML | 380 | 94 | 47505144 |
| 2025-02-23 | Jq-Like Tool for Markdown | 325 | 75 | 43152704 |
| 2026-04-23 | Tolaria – Open-source macOS app to manage Markdown knowledge bases | 318 | 142 | 47882697 |
| 2025-02-14 | Transform your codebase into a single Markdown doc for feeding into AI | 281 | 164 | 43048027 |
| 2025-10-25 | Diagram as code tool with draggable customizations | 266 | 60 | 45706792 |
| 2026-04-25 | A Karpathy-style LLM wiki your agents maintain (Markdown and Git) | 260 | 114 | 47899844 |
| 2026-01-11 | **Ferrite – Markdown editor in Rust with native Mermaid diagram rendering** | 241 | 190 | 46571980 |
| 2025-03-16 | Cascii – A portable ASCII diagram builder written in vanilla JavaScript | 238 | 53 | 43377897 |
| 2025-01-17 | GUI for editing Mermaid class diagrams | 230 | 59 | 42738656 |
| 2025-08-26 | Turn Markdown into React/Svelte/Vue UI at runtime, zero build step | 202 | 97 | 45024532 |
| 2025-04-12 | memEx, a personal knowledge base inspired by zettlekasten and org-mode | 194 | 45 | 43667061 |
| 2025-02-03 | Marksmith – a GitHub-style Markdown editor for Ruby on Rails | 161 | 28 | 42917712 |
| 2025-12-04 | Fanfa – Interactive and animated Mermaid diagrams | 154 | 33 | 46147329 |
| 2026-04-18 | MDV – a Markdown superset for docs, dashboards, and slides with data | 152 | 55 | 47816629 |
| 2025-06-12 | McWig – A modal, Vim-like text editor written in Go | 152 | 28 | 44257609 |

That is twenty posts across twenty months, and it is the complete set above 150
points in this category rather than a selection from it.

### The same category before the crowding: 2023-2024, 250 points or more

| Date | Title | Pts | Cmts | HN id |
|---|---|---:|---:|---|
| 2024-12-01 | Markwhen: Markdown for Timelines | 912 | 98 | 42289690 |
| 2023-07-30 | Khoj – Chat offline with your second brain using Llama 2 | 565 | 150 | 36933452 |
| 2024-06-29 | I am building an open-source Confluence and Notion alternative | 551 | 217 | 40832146 |
| 2023-06-23 | An open-source, collaborative, WYSIWYG Markdown editor | 460 | 128 | 36446045 |
| 2024-06-10 | Markdown HN profiles at {user}.at.hn | 459 | 154 | 40632773 |
| 2024-04-14 | I made a tool to clean and convert any webpage to Markdown | 441 | 99 | 40033490 |
| 2023-08-24 | Open-source obsidian.md sync server | 416 | 152 | 37247767 |
| 2024-10-10 | Tenno – Markdown and JavaScript = a hybrid of Word and Excel | 401 | 138 | 41798477 |
| 2023-06-16 | I made an open-source Notion-style WYSYWIG editor | 370 | 133 | 36360789 |
| 2024-11-09 | HTML-to-Markdown – convert entire websites to Markdown with Golang/CLI | 360 | 47 | 42093511 |
| 2023-07-31 | Markwhen: Markdown for Timelines | 327 | 58 | 36944152 |

### Show HN against the other ways to submit

The census classifies every on-topic story by how it was submitted. "Bare repo
link" means a project's repository posted without the Show HN prefix; "article"
means anything else with a URL, which covers blog posts, project news and
third-party coverage.

| Year | Show HN | Bare repo link | Article or other link |
|---|---|---|---|
| 2023 | 10.98% (n=82) | 2.99% (n=67) | 4.97% (n=181) |
| 2024 | 10.49% (n=143) | 2.44% (n=82) | 8.29% (n=193) |
| 2025 | 5.45% (n=257) | 4.24% (n=118) | 7.52% (n=266) |
| 2026 | 2.00% (n=401) | 0.84% (n=119) | **4.39% (n=319)** |
| **2025-2026 pooled** | **3.34% (n=658)** | **2.53% (n=237)** | **5.81% (n=585)** |

Percentages are the share reaching 100 points. Two findings, pointing in opposite
directions.

**Posting a project as a bare repository link is the worst option, in every year
measured.** It loses to Show HN in 2023 (2.99% against 10.98%), 2024, 2025 and
2026, and in 2026 it is the weakest route in the dataset at 0.84% across 119
posts. Dropping the prefix to look less promotional costs reach rather than buying
credibility.

**Articles now outperform Show HN, and the gap has widened.** In 2026 an on-topic
article reached 100 points 4.39% of the time against 2.00% for a Show HN, and 14
of the year's 23 on-topic hits were articles while only 8 were Show HN posts. In
2023 the ranking was the other way round.

That comparison needs one large caveat before it is acted on. The article
population includes product news from projects that already have an audience —
Obsidian Bases at 695, Obsidian Sync's headless client at 587, Windows Notepad
gaining markdown at 353, LibreOffice Writer at 407 — and no new project can
manufacture that. The achievable subset is the essay written by a developer about
their own work, and the dataset contains a clear example of it:

| Date | Title | Pts | Cmts | Type |
|---|---|---:|---:|---|
| 2025-05-18 | Ditching Obsidian and building my own | 471 | 559 | one developer's blog post |
| 2025-11-22 | Markdown is holding you back | 246 | 182 | opinion essay |
| 2025-02-22 | If it is worth keeping, save it in Markdown | 338 | 273 | opinion essay |
| 2025-12-26 | Developing a Beautiful and Performant Block Editor in Qt C++ and QML | 146 | 60 | engineering deep-dive |

"Ditching Obsidian and building my own" is the shape most relevant here: a solo
developer explaining why the incumbent did not fit and what they built instead,
which drew 559 comments. It is the same content a Show HN carries, submitted as a
story rather than as a product.

### The second half of 2025, added after the full-year census

These are on-topic stories from July to December 2025 that the earlier
January-to-July sampling had missed. They are worth having because 2025 H2 is the
half in which the success rate halved, so it is the closest thing to the regime a
launch now enters.

| Date | Title | Pts | Cmts | HN id |
|---|---|---:|---:|---|
| 2025-08-18 | Obsidian Bases | 695 | 255 | 44945532 |
| 2025-12-24 | Show HN: Minimalist editor that lives in browser, stores everything in the URL | 468 | 168 | 46378554 |
| 2025-10-25 | Show HN: Diagram as code tool with draggable customizations | 266 | 60 | 45706792 |
| 2025-08-17 | Show HN: Doxx – Terminal .docx viewer inspired by Glow | 261 | 72 | 44934391 |
| 2025-07-06 | Backlog.md – Markdown-native task manager and kanban for any git repo | 254 | 62 | 44483530 |
| 2025-11-22 | Markdown is holding you back | 246 | 182 | 46017782 |
| 2025-10-26 | **Microsoft 365 Copilot – Arbitrary data exfiltration via Mermaid diagrams** | 218 | 49 | 45715837 |
| 2025-08-26 | Show HN: Turn Markdown into React/Svelte/Vue UI at runtime, zero build step | 202 | 97 | 45024532 |
| 2025-09-17 | Notion API importer, with Databases to Bases conversion bounty | 189 | 59 | 45271942 |
| 2025-12-08 | Show HN: A system for active note-taking in regular meetings like 1-1s | 177 | 133 | 46198430 |
| 2025-12-04 | Show HN: Fanfa – Interactive and animated Mermaid diagrams | 154 | 33 | 46147329 |
| 2025-08-29 | Show HN: Sosumi.ai – Convert Apple Developer docs to AI-readable Markdown | 133 | 72 | 45063874 |
| 2025-12-04 | Show HN: Kraa – Writing App for Everything | 127 | 75 | 46144801 |
| 2025-10-23 | Be Careful with Obsidian | 127 | 150 | 45678941 |
| 2025-09-18 | Obsidian Note Codes | 116 | 55 | 45288739 |

### Mermaid as an attack surface

The row above at 218 points is the one with direct bearing on Kvit, and it did not
appear anywhere in the earlier sampling. Microsoft 365 Copilot could be induced to
exfiltrate data through a Mermaid diagram. From the thread, the mechanism combined
prompt injection with **clickable links inside the rendered diagram**: "Copilot's
doesn't let you do that so you need to trick users with a fake Login button that
activates a hyperlink instead." A commenter names a second, earlier instance
against Cursor in August 2025, where "Cursor's Mermaid implementation could render
external images."

So two exfiltration routes through Mermaid rendering are publicly documented, both
against AI-adjacent products, both in the year before this launch: interactive
links, and external image fetches. A markdown editor that renders Mermaid and
markets itself around AI agents editing notes will be asked about this, and the
question is legitimate rather than hostile — a note is untrusted input.

Kvit's position, checked in the source rather than assumed:

- `src/content/diagrams/mermaidparser.cpp:757` parses `click` and `linkStyle` and
  discards both, raising the diagnostic "`click` is ignored in Kvit (interactivity
  and link styling are not supported)". The interactive-link route does not exist.
- `mermaidrenderer.cpp` and `diagrampainter.cpp` contain no networking, no URL
  handling and no image loading. Diagrams are laid out and painted from parsed
  text. The external-image route does not exist either.
- Anything that does reach the network on a note's behalf goes through the single
  egress policy in `src/platform/`, per `docs/adr/0003-network-egress-policy.md`.

That is an unusually clean answer to a question the category has recently earned,
and it is verifiable in a few minutes by a reader who wants to check, which the
Ferrite thread shows is exactly what happens.

### The nearest analogue: Ferrite

`Show HN: Ferrite – Markdown editor in Rust with native Mermaid diagram rendering`
(2026-01-11, 241 points, 190 comments, HN id 46571980) is the closest thing to
Kvit's planned launch that exists: a markdown editor whose headline feature is
rendering Mermaid diagrams natively rather than through a browser engine,
submitted as a Show HN by its solo author, linking a GitHub repository. It differs
in language (Rust and `egui`) and in maturity.

Its thread is the single most useful document in this catalogue, because the
audience spent it listing what the editor lacked:

- `[[wikilinks]]` and back references (raised as a blocker for adoption)
- LaTeX maths — "the one thing that prevents me from trying this out"
- WYSIWYG rather than a split-pane preview; the editor offered split view
- A packaging defect on macOS, where it "spawns a terminal on startup"

Kvit ships all four of those. Wiki-links with backlinks, TeX maths through a
vendored MicroTeX, live-preview editing in place rather than a split pane, and a
signed and notarised DMG.

The thread also contains a warning. A commenter asked whether the post had been
"written using an LLM", and the author confirmed that the replies were
AI-assisted, explaining that English was not their first language. That exchange
sits high in the thread. The audience is actively pattern-matching for generated
prose in Show HN posts and replies.

The author's own framing, from their comment in the thread, was: "a fast, native
Markdown editor that 'just works' - no Electron, no web tech, instant startup."

### The other 2026 analogues that worked

- **Files.md** (730 points, 2026-05-18): the highest-scoring post in the category
  in two years. Its README opens with "Private, quiet space for thinking. Simple
  app for `.md` files", shows a dark-themed screenshot early, describes itself as
  local-first and LLM-friendly, and mentions no commercial plan. It is a browser
  application installed as a progressive web app, so it beat native editors while
  not being one.
- **OpenKnowledge** (381 points, 2026-06-25): the funded competitor named in
  `to-market.md`, which chose the AI-first framing that the data below says
  underperforms, and still cleared 381 points.
- **Tolaria** (318 points, 2026-04-23): an open-source macOS application for
  managing markdown knowledge bases.

### The misses: 2026 Show HN posts in this category under 10 points

This is the part no summary of front-page stories will show you. All of these are
from the last five weeks of the sample.

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2026-08-08 | Hosted LLM Wiki | 3 | 0 |
| 2026-08-07 | Mermaid flowcharts you don't have to redraw in a diagram editor | 3 | 0 |
| 2026-08-07 | Markdown Buddy – Native macOS Markdown Editor with Quick Look and Xcode | 2 | 0 |
| 2026-08-06 | **Line9 – a Mermaid rendering engine with its own layout** | 2 | 0 |
| 2026-08-06 | Femto: A fork of kilo (text editor) | 3 | 0 |
| 2026-08-05 | Nodes – A native macOS Markdown editor that runs its AI on device | 2 | 1 |
| 2026-08-05 | Lapse – a notes app, but also shared memory space for your agents (MCP) | 3 | 1 |
| 2026-08-04 | Beauty Diagram – 9 themes for Mermaid/PlantUML/draw.io, in your editor | 3 | 0 |
| 2026-08-03 | Wander, a Wysiwyg Markdown Editor for macOS and iOS | 1 | 0 |
| 2026-08-02 | My public second brain – 660 notes, 15 years, open source | 4 | 6 |
| 2026-07-31 | Stash – native Mac Markdown notes | 4 | 0 |
| 2026-07-30 | Scriptum, a native Mac Markdown editor that live reloads | 1 | 2 |
| 2026-07-29 | MD Flow – native macOS/iOS app for browsing and reading Markdown files | 2 | 0 |
| 2026-07-27 | Foglio – macOS Markdown editor: live preview, MIT, 5MB, no vault | 4 | 2 |
| 2026-07-27 | macOS-first Markdown editor with inline live preview (100% Swift) | 3 | 0 |
| 2026-07-27 | QuickMark – Markdown app for macOS with a Quick Look preview extension | 2 | 0 |
| 2026-07-27 | Panorama Markdown – Markdown dialect and editor for academic papers | 1 | 0 |
| 2026-07-26 | I wrote this slowly, a needlessly patient text editor | 4 | 0 |
| 2026-07-23 | MinkNote – Native macOS notes and journaling using Markdown files | 2 | 0 |
| 2026-07-22 | MindBase – an LLM that maintains a wiki from your notes and papers | 3 | 2 |
| 2026-07-21 | Brookmd – streaming Markdown renderer in Rust (web, RN, Swift and more) | 2 | 0 |
| 2026-07-20 | Local-first CLI to make Obsidian vaults searchable for AI agents | 4 | 2 |
| 2026-07-16 | I open-sourced a macOS native Markdown rendering engine | 7 | 2 |
| 2026-07-15 | OpenMarkdown – A Markdown editor you and your agent co-edit | 7 | 0 |
| 2026-07-15 | Hillnote – local first Markdown workspace, AI via CLI, Ollama, MCP | 3 | 0 |
| 2026-07-14 | Noteato – Notion-like notes as local Markdown files | 2 | 0 |
| 2026-07-13 | Quarkdown 2.4 – Redefining Markdown | 1 | 0 |

The sample holds several hundred more from 2026 alone. Two rows deserve attention
before any launch copy is written. **Line9**, three days before this file was
compiled, is a Mermaid rendering engine with its own layout algorithm — which is
one of Kvit's four planned engineering deep-dive posts — and it received 2 points
and no comments. **Quarkdown 2.4** received 1 point, while earlier submissions of
the same project reached 668 and 358 points; the difference is that those were not
version-update posts.

The macOS pattern in that table is its own lesson. Eight of the twenty-seven rows
are native macOS markdown editors, all of them dead. "Native markdown editor" is
now a saturated phrase rather than a distinguishing one.

### The middle band: 2025-2026, 30 to 149 points

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2026-02-09 | JavaScript-first, open-source WYSIWYG DOCX editor | 130 | 44 |
| 2026-01-09 | Executable Markdown files with Unix pipes | 126 | 101 |
| 2025-12-04 | Kraa – Writing App for Everything | 127 | 75 |
| 2025-06-03 | Ephe – A minimalist open-source Markdown paper for today | 143 | 54 |
| 2025-04-24 | Colanode, open-source and local-first Slack and Notion alternative | 147 | 49 |
| 2026-04-15 | SmallDocs – Markdown without the frustrations | 92 | 50 |
| 2026-04-10 | A WYSIWYG word processor in Python | 91 | 38 |
| 2026-01-04 | I replaced Beads with a faster, simpler Markdown-based task tracker | 84 | 51 |
| 2026-05-31 | Atomic Editor – Obsidian-style live preview for CodeMirror 6 | 67 | 19 |
| 2026-04-24 | Atomic – Local-first, AI-augmented personal knowledge base | 62 | 43 |
| 2026-02-04 | Mmdr – 1000x faster Mermaid rendering in pure Rust (no browser) | 47 | 8 |
| 2026-02-15 | Lightwave – Real-time notes app, 3.5 years of hand-rolled JavaScript | 52 | 37 |
| 2026-01-18 | Opal Editor, free Obsidian alternative for markdown and site publishing | 36 | 6 |
| 2025-08-29 | Octarine – a fast, lightweight, opinionated Markdown notes app | 44 | 20 |

`Mmdr` at 47 points is worth noting beside `Line9` at 2 points: both are
browserless Mermaid renderers, submitted five months apart, and the one with a
performance number in its title did an order of magnitude better while still
falling short of the front page.

### Diagram tooling, which travels better than editors do

These are not Show HN posts, which is part of the point — the diagram stories that
did best were project news and blog posts rather than launches.

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2025-10-29 | Who needs Graphviz when you can build it yourself? | 524 | 108 |
| 2024-11-04 | Diagram as Code | 515 | 131 |
| 2025-08-19 | D2 (text to diagram tool) now supports ASCII renders | 441 | 73 |
| 2026-01-29 | Mermaid ASCII: Render Mermaid diagrams in your terminal | 418 | 66 |
| 2023-02-23 | Mermaid Cheat Sheet | 368 | 95 |
| 2026-04-09 | Pretty Fish: A better mermaid diagram editor | 186 | 30 |
| 2026-06-23 | Markdy: Like Mermaid Diagrams, but for Motion | 121 | 73 |

### What the diagram threads actually argue about

A separate pass read 998 comments across thirteen diagram threads — the six above
plus seven adjacent ones chosen because they test Kvit's exact features. Counts
below are of comments, and the quotes are verbatim.

**Editing a rendered diagram and having the source update is the most-requested
unshipped feature in this corpus.** People ask for it unprompted, in threads about
tools that do not offer it, going back to 2023, and nobody argues it is
impossible:

- "Still waiting for the day someone out there adds some drag-and-drop capability
  in a Mermaid based UI" (2023)
- "what we need is visual editor that generates code and so we can keep that code
  versioned in github" (2024)
- "Do the inverse now. Generate the code from the diagram." (2024)
- "Diagrams from code are nice ... until you want to nudge that one particular
  little thing" (2024)
- "what all these tools are missing is the ability to add a manual 'corrective'
  layer" (2025)

The design space is publicly occupied and unshipped. Mermaid Chart advertises
"bi-directional editing capability" in one of these threads, and a user in the
same thread reports that "it just wouldn't let me organize/drag things where I
wanted." Terrastruct sells position-locking as a paid tier. Ferrite's author
announced in January 2026 a plan to encode node positions as `%% @pos` comments so
diagrams stay compatible with stock mermaid.js, which is close to what Kvit
already does.

**The one Show HN that shipped a version of it did well.** "Diagram as code tool
with draggable customizations" (oxdraw, October 2025, 266 points) is a CLI that
lets you drag Mermaid nodes and writes adjustments back as Mermaid comments.
Reception was near-uniform: "exactly what I'm missing from mermaid", "I was
looking for exactly this last week", "I have been wanting something like this for
a really long time!" The critical comments were about packaging, hosting and a
missing licence file rather than the concept.

**Layout quality is the dominant complaint** — 46 of 547 comments in the six main
threads, rising to 17% in the Graphviz thread. Two forms recur. The first is that
automatic layout degrades with size: "once the diagram has more than a few nodes
the layout goes wacky", "any diagram becomes unreadable past a certain number of
nodes". The second matters more for Kvit's pitch: layout is unstable under edits.
"add a node and you might end up with a graph that doesn't look like the previous
version", and "one small change and it may completely rearrange the output."

**The people best placed to know say the layout algorithm is the hard part.** A
Mermaid maintainer concedes "we cannot easily add layout logic without sacrificing
the simplicity of the syntax". A Graphviz maintainer estimates adopting better
edge routing at "close to a master's degree worth of work."

**The hard problem in drag-to-edit is not the drag, it is the diff.** The TikZ
editor thread, at 451 points, is the best-informed discussion of it. Its author:
"the difficulty lies not so much in code generation, but determining what a user
would expect", and his editor "will in many cases refuse to let you drag things
since it doesn't know what the drag should mean." The criticism that drew
agreement was that it rewrites relative positioning into absolute coordinates. The
requirement
that follows was stated by a commenter and is the most useful sentence in this
corpus for Kvit's launch:

> "The killer feature for me is not drawing TikZ visually, but being able to touch
> old TikZ without turning the source into generated-looking soup."

**ASCII diagram posts do not fail.** Every one in the corpus lands between 120 and
450 points (D2 ASCII 441, Mermaid ASCII 418, Cascii 238, ASCIIFlow 143), and the
149-post failure set contains no ASCII diagram flop. The stated reason is always
placement rather than looks: the diagram goes where no renderer exists, in a
terminal, a source comment, a code review, or a model's context window.

**The round trip is the top unmet ask in those threads specifically**, which is
what Kvit's paste-and-straighten and copy-out-as-text pair addresses:

- "Is there something that can turn those ascii diagrams back into mermaid
  sources?"
- "Conversion from ASCII is nice. How do we get the original D2 source if there's
  a need to update the diagram?!"
- "Is is possible to import and edit diagrams after they have been created?" —
  answered by another user with "Exactly. Make it importable and i am sold."

Cascii's author spent several replies explaining that recovering structure from
pasted ASCII is hard enough that he shipped base64 export instead, and floated
training a neural network for it.

**Examples get audited within the hour.** In the ASCII threads, strangers re-ran
the demos against harder input and posted the failures: a spurious arrow between
two nodes, a label rendered as "thickted", a dropped edge, "I just tried a more
complex graph and it didn't render well." Authors who answered with "not all
syntax works yet, here is the roadmap" kept their threads friendly.

**Two counter-currents worth knowing before writing copy.** A subset of this
audience values automatic layout precisely because it stops them fiddling —
Mermaid's biggest value is "its ability to connect nodes 'well enough' without me
needing to fiddle with it", and the wish for a pretty chart is "a huge source of
wasted time using WYSIWYG tools". And one commenter frames the whole category
sceptically: a visual editor for a plain-text format "is akin to a WYSIWYG
Markdown editor", so why not use a word processor.

**Diagram Show HN failures, 2024 onward.** From 149 posts under 15 points, the
ones nearest to Kvit's pitch:

| Date | Pts | Title |
|---|---:|---|
| 2026-08-07 | 3 | Mermaid flowcharts you don't have to redraw in a diagram editor |
| 2026-08-06 | 2 | Line9 – a Mermaid rendering engine with its own layout |
| 2026-08-04 | 3 | Beauty Diagram – 9 themes for Mermaid/PlantUML/draw.io, in your editor |
| 2026-07-23 | 2 | DrillFlow – a diagram editor where every node opens into a sub-diagram |
| 2026-07-21 | 9 | Chalk: a Mac app for technical diagrams with a built-in MCP for Claude |
| 2026-06-27 | 1 | DiagramIDE – Diagrams as Source Files |
| 2026-06-03 | 4 | Mermaid Studio – preview and fix broken mermaid diagrams |
| 2026-04-27 | 1 | Archik – architecture diagrams as YAML, with a Claude Code skill |
| 2026-03-17 | 1 | Yuzudraw – visual editor for ASCII diagrams with token-efficient DSL |
| 2026-01-23 | 3 | MermaidTUI – Deterministic Unicode/ASCII diagrams in the terminal |
| 2025-12-19 | 2 | Merview – OSS Mermaid and Markdown Editor and Viewer |

Comparing those 149 flops against 25 diagram Show HN posts over 100 points from
the same query set: **artificial intelligence appears in 27% of the flops and 4%
of the hits**, and a coined product name leads 46% of flops against 24% of hits.
That second measurement disagrees with the broader one reported earlier in this
file, which found no effect from leading with a product name across 713
notes-and-markdown posts. The two samples differ in category and in method — the
diagram figure compares the two extremes of the distribution, the broader figure
compares across all of it — so the safest reading is that a product name is not
what carries a title either way.

**The most sobering pair in this file.** One author posted "Line9 – a Mermaid
rendering engine with its own layout" on 6 August 2026 (2 points) and, one day
later, "Mermaid flowcharts you don't have to redraw in a diagram editor" (3
points), both linking the same product. The second title is a good title: it names
the reader's problem in the reader's words. It got nothing. Whatever separates
oxdraw's 266 points from this is not the sentence.

### Qt on Hacker News

Every Show HN in the sample whose title names Qt, QML, PyQt or KDE:

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2018-02-06 | Cross-platform desktop applications with PyQT | 315 | 285 |
| 2017-04-08 | Qt binding for Go with support for all major operating systems | 222 | 74 |
| 2023-03-04 | Procal: A simple Qt-based programming calculator | 173 | 90 |
| 2021-02-24 | I built a cute, little isometric block stacking editor in QML | 100 | 17 |
| 2019-09-25 | A Quantum Perceptron – First Steps in QML | 65 | 15 |
| 2024-12-29 | Resizer2 – i3/KDE window movement on Windows | 40 | 19 |
| 2018-09-20 | Crash course in Qt for C++ developers – a blog post series | 27 | 8 |
| 2018-07-15 | Qt/Qml support for .NET | 27 | 1 |
| 2019-04-11 | OPC UA Server Qt C++ Library | 15 | 1 |
| 2026-07-22 | Vivace – A single-process Qt media player with interactive DVD menus | 9 | 0 |
| 2026-08-03 | Kwayk – Reimplementing Quake in Qt Quick3D with Jolt Physics | 3 | 0 |
| 2025-03-07 | Qt 6 for C and Zig | 3 | 0 |
| 2026-05-08 | Rore GUI – An Open Qt Alternative in Rust (WGPU, Zero-VDOM) | 2 | 1 |
| 2025-02-23 | Shijima-Qt – Desktop Pets for Linux, macOS and Windows | 2 | 1 |

Sixteen such posts since 2023 in the sample, of which one cleared 100 points, and
nothing since 2023 has cleared 173. The best results in the list are eight years
old and are about Qt bindings for other languages rather than about applications.

Qt reaches the front page constantly, but not this way:

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2025-09-18 | KDE is now my favorite desktop | 892 | 742 |
| 2024-02-29 | The KDE desktop gets an overhaul with Plasma 6 | 817 | 616 |
| 2025-09-10 | KDE launches its own distribution | 680 | 528 |
| 2026-03-06 | Plasma Bigscreen – 10-foot interface for KDE plasma | 673 | 230 |
| 2025-10-12 | KDE Connect: Enabling communication between all your devices | 476 | 198 |
| 2020-12-08 | Qt 6.0 | 447 | 360 |
| 2015-12-18 | Wireshark 2.0: Now with Qt | 354 | 149 |
| 2018-06-07 | Cutter – A Qt and C++ GUI for Radare2 reverse engineering framework | 336 | 50 |
| 2024-09-16 | Do Not Sign the Qt License Agreement Unchanged | 174 | 78 |

The audience reads about Qt applications enthusiastically when the story is the
application or the ecosystem. It does not appear to upvote a Show HN because the
application is written in Qt.

### What the Qt threads actually say

A separate pass read 3,365 comments across twenty threads chosen to test the
native and Qt framing, of which 166 comments mention Qt, QML, PyQt, PySide,
QWidgets or Qt Creator.

**The controlled comparison is the most useful thing in that corpus.** Two
projects, both C++ with Qt, both on the front page, differing in where they put
the word:

| Post | Score | What the comments were about |
|---|---:|---|
| Procal, **Qt in the title** | 173 | Roughly a quarter of the thread became a licence argument; the author relicensed his project from MIT to GPLv3 mid-discussion. The calculator itself was barely discussed |
| Vicinae, "native" in the title, **Qt in the first paragraph of the body** | 181 | Zero toolkit pushback, zero licence discussion. Thirty-nine comments of feature requests, install reports and praise |
| Daino Notes block editor, **Qt and QML in a blog-post title** | 146 | The author spent the thread defending QML, and won only by producing benchmarks |

Vicinae's post says in its opening paragraph: "written in C++ with Qt Widgets. I
chose Widgets over QML for more imperative control." Nobody argued.

**Sentiment on Qt itself is better than its Show HN record suggests.** People who
have shipped with it are specific: "The documentation is excellent, there aren't
maddening incomplete corners like GTK"; "I'll take QObject over GObject any day of
the week"; "Qt is probably the only alternative that anyone might consider
'platform native'"; and from the Vicinae author, unchallenged, "it's a powerful
framework that mostly stayed out of my way."

**The criticisms are four separable things**, which means they are separately
answerable: that Qt applications look wrong ("QT just looks uglier and feels more
windows XP-esque"); that no cross-platform toolkit reproduces platform behaviour
("there's a truckload of little things that native AppKit apps get you that
nothing else will, not even other 'native' toolkits like Qt"); weight and the
absence of a stable ABI; and the difficulty of writing bindings from other
languages.

**QML draws more heat than Qt, and the critics have used it in production.** This
is the objection most likely to dominate a Show HN for a QML block editor:

- "QML is complete trash. It's fine for simple UIs with minimal logic and
  prototypes but for anything beyond that you'll always need to implement the
  logic in C++."
- "QML has terrible shortcomings in its lack of good typing, debugging abilities,
  error reporting or general maintainability."
- "For the desktop I would 100% use Widgets over QML."
- "Dealing with layouts in QML is also an exercise in frustration."

Counterweights exist from long-time Qt developers, and one thing was observed to
end the argument: measurement. The Daino Notes author, told repeatedly that QML is
slow, replied "You can't claim something is slow without showing empiric data. I
showed mine", and the objection stopped there.

**Licensing is the largest avoidable failure mode.** The reputational residue is
old and durable: "when I see 'Qt' the first thing that comes to my mind is
'License Tomfoolery.'" In the Files.md thread a developer who merely mentioned a
Qt project was told, incorrectly, that he would need a commercial licence to ship
closed source, and it took two further commenters to correct it.

The Procal case is worth recording exactly, because it is the clearest picture of
what an unprepared author loses. Procal is a Python calculator using **PyQt**,
whose bindings are GPL-or-commercial, unlike Qt's own C++ modules which are LGPL.
It shipped under **MIT**. A commenter wrote: "I believe your license is incorrect.
PyQt is licensed either commercial or GPL, so you would have to either purchase a
license or license this under GPL verses the more permissive MIT license you are
using." The author replied "Thanks! I changed the license to GPLv3" — so the
project went **MIT to GPLv3**, inside the thread.

Two follow-ups then complicated it. One commenter pointed out he could have used
PySide, the official "Qt for Python" bindings, which are LGPL, and avoided the
question entirely. Another disputed the premise outright: "I don't think that's
right. The MIT license is compatible with the GPL... Only someone who wants to
fork the code into a proprietary program would need to purchase a license", and
then, on the specific objection, "the project as a whole (which includes the
library itself) need to be GPL, but part of it can be MIT. So the code of the
program on GitHub can be MIT." The author had already relicensed by that point,
on advice that a later commenter argued was wrong.

Note what does and does not transfer. The trap was **PyQt-specific**, and a C++
project using Qt's LGPL modules directly does not meet it at all. The corrective is
present in every thread and favours an open-source project: "Why do people even
buy QT Commercial? LGPL is a very easy license to comply with." The real pain is
concentrated in embedded shipping and in the commercial contract's terms, neither
of which touches an open-source desktop application. One author pre-empted the
static-linking misconception in his post body and turned a fight into a
two-comment factual confirmation.

**The native and no-Electron framing is close to a cliché.** Of 152 comments
mentioning Electron across the corpus, one classification put 22% supportive of
the framing, 25% against it and 53% neutral — and most of the opposition came from
one thread about Tauri specifically, where the complaint is that it renders in the
system webview, which does not transfer to an application that draws its own
widgets. The only Show HN in the site's history with "no Electron" in its title
scored 98 points and produced no Electron discussion at all. Disclosing Electron
does not sink a post either: a Jupyter client wrote "Full disclosure: it's built
with Electron" and scored 611.

**What the audience demands before it believes a performance claim**, in order of
frequency: resident memory in megabytes, which readers measure themselves; binary
or install size; a concrete startup time rather than the word "instant"; and a
like-for-like comparison against the incumbent. One author never mentioned memory,
a reader opened Activity Monitor, reported 85 MB, and a second reader replied "85mb
is electron territory".

The thresholds visible in the corpus:

| Figure | What it was | How it was received |
|---|---|---|
| 2 MB bundle | A SwiftUI client | "It can fit onto two floppy drives!" |
| 13 MB RAM | WordPad, cited as the floor a text editor should hit | Approving |
| 15 MB RAM idle | A Qt6/QWidgets Obsidian clone | Accepted without challenge |
| ~15 MB binary, "instant startup" | Ferrite | Became the project's identity in its thread |
| ~85 MB RAM | A native macOS client, measured by a reader | "85mb is electron territory" |
| ~200 MB | Chromium's floor before any application code | Cited as the structural cost |
| ~300 MB RAM | Obsidian showing static text | "Wordpad does the same in 13mb ram" |

Under about 20 MB resident reads as unambiguously native and draws no scrutiny;
between 26 MB and 85 MB the claim starts being tested. Obsidian's 300 MB and
Chromium's 200 MB are the numbers this audience already carries around, which
makes them the natural comparison points.

### What note-taking audiences actually demand

A third pass read 2,313 comments across the seven largest competitor threads,
counted by how many of the 500 top-level threads mention each topic anywhere in
their subtree. Read the numbers as airtime rather than as a headcount.

| Topic | Root threads | Comments |
|---|---:|---:|
| git as the sync and version layer | 103 | 215 |
| sync across devices | 92 | 228 |
| AI, LLMs, agents | 89 | 195 |
| database, table and kanban views | 75 | 156 |
| export and migration path | 73 | 149 |
| plugins and extensibility | 72 | 166 |
| plain files, no lock-in | 66 | 121 |
| self-hosting | 57 | 105 |
| search quality | 52 | 86 |
| mobile access | 47 | 104 |
| emacs, org-mode, vim | 42 | 89 |
| real-time collaboration | 41 | 72 |
| "why another one" | 30 | 41 |
| "how do you make money" | 26 | 63 |
| **wiki-links, backlinks, graph view** | **17** | **23** |
| diagrams | 15 | 32 |
| Electron versus native | 13 | 20 |
| **LaTeX and maths** | **6** | **9** |

The two rows in bold matter for how Kvit positions itself. Wiki-links and maths
are near the bottom of what this audience asks for, and graph view is actively
mocked: "The graph is often the most overhyped and underused thing there." They
read as credibility items that a serious editor is expected to have rather than as
reasons anybody switches.

What people do demand is sync they control, which in their own words means the
application staying out of the way of Syncthing, git, or a cloud drive over a
plain directory. One of these threads runs to 559 comments largely devoted to
explaining that the author never needed to pay for sync. An application that does
not ship sync is not seen as deficient; one that requires its own sync service is.

Plugins are the hardest question, named by several people as the only thing
keeping them on Obsidian. The material for an answer is in the same threads: the
security argument is already theirs ("The community plugin system is both an
unacceptable security risk, and a necessary part of achieving even a baseline
level of usability"), and the specific plugins people name as indispensable —
Dataview and Bases, Kanban, Excalidraw and Mermaid, Templater, omnisearch, LaTeX
Suite — map onto things Kvit ships first-party.

**Accessibility is unclaimed.** None of the seven projects had an accessibility
story, and the highest-ranked comment in one of these threads was a blind
screen-reader user and professional auditor asking whether anyone had thought
about it and offering a free review. That subtree ran to 23 comments.

**One comment in the Files.md thread is Kvit's market, stated by someone else.**
Ranked second in a 355-comment thread, a developer described building "a native
version of Obsidian in Qt6 (QWidgets, cpp)... its lightweight, using about 15mb
ram". Reception was warm for 23 replies until he said it would be closed source,
at which point: "Free to download, free to use, closed source - for that we
already have Obsidian", and then "If you're building something that's Free
software, fully compatible with Obsidian, and a native app, I'm willing to
contribute tokens." Kvit is that description under MPL-2.0.

### How a commercial plan is received

A fourth pass read thirteen threads about open-source business models, licence
changes and paid tiers.

**Disclosing early was never punished in this set; withholding was.** In Files.md
one commenter asked seven words — "And the developers get compensated for their
work how?" — the author replied "That's yet to be decided :D", and that exchange
grew into a 32-reply subtree, the largest in a 355-comment thread, with strangers
arguing his business model for him and none of it about the product. By contrast
the OpenKnowledge founder answered the same question with "we're same team behind
Inkeep — already have a healthy biz", and it closed in three. A commenter on
another launch stated the reason directly: "Would be great if you could include in
your launch message how you plan to monetize this... what's coming down the line
would be good to know before deciding whether to give it a shot or just move on."

**MPL-2.0 has an unusually good reputation here.** Across roughly thirty comments
spanning 2012 to 2026 the recurring description is a sensible middle ground: "a
good intermediate between GNU family of licenses (strong copyleft) and Apache /
MIT / 3-Clause BSD", and Oxide Computer's rationale is quoted approvingly — its
file-based copyleft "allows it to be proprietary-friendly while also forcing
certain kinds of derived work to be contributed back", and it "obviates the need
for a contributor license agreement." No instance of MPL being called a
bait-and-switch appears in the sample.

**The bait-and-switch charge attaches to licence changes, not to launches:**

| Thread | Comments | Bait-and-switch language |
|---|---:|---:|
| Cal.com going closed source | 317 | 15 (4.7%) |
| Ferrite | 190 | 2 (1.1%) |
| Files.md | 355 | 1 (0.3%) |
| Six other launches | 60-263 each | 0 |

**The abandonment question is really a data question.** Explicit sustainability
questions run 0-3 per thread, while data-portability language runs 10-13 per
thread in every notes-app thread counted. The answer this audience accepts is the
licence itself: "Dendron is basically abandonware... but because it's Apache
licensed, I can fork it." An abandoned open project is tolerable to them; an
abandoned closed one is not.

**Phrasings that worked**, from the same set: naming gated features and a price
("Some non-essential features like custom templates or multi-turn chat are gated
behind a paid license", with "There's still no sign-up required" and a yearly
figure) drew no pushback on the gating itself; and a pre-commitment, "the core app
is fully open-source and always will be", closed a hostile subthread. Moving a
paywall off local features and onto something the maintainer must run drew
explicit endorsement in the Daino Notes thread: "tying Pro to cloud features makes
sense for subscription model."

**Phrasings that backfired**: leaving the model blank; "Free to download, free to
use, closed source", which was answered with "for that we already have Obsidian";
and calling a non-OSI licence open source, which was caught within hours in two
separate threads by people who had opened the LICENSE file.

**Five people across three threads asked, unprompted, for the feature Kvit plans
to sell**: "a simplified review/track-changes system that I can collaborate with
my AI on these docs" (adding "If I had more free time, I'd build it myself!"),
"integrating 'red lining' as a channel for LLM input. This is the best interface
for working on a text", "comments, suggestions and history. I see this as
important even when working with AI", and a question about what happens "when both
the user and an agent edit the same note at the same time." One described
enforcing it by hand with snapshots around every tool call, prefaced with "I don't
let agents edit files that I'm writing - that's how you get psychosis."

### The Obsidian ecosystem, as a measure of audience appetite

| Date | Title | Pts | Cmts |
|---|---|---:|---:|
| 2025-08-18 | Obsidian Bases | 695 | 255 |
| 2026-02-28 | Obsidian Sync now has a headless client | 587 | 212 |
| 2025-05-18 | Ditching Obsidian and building my own | 471 | 559 |
| 2026-05-12 | The Future of Obsidian Plugins | 452 | 177 |
| 2026-05-10 | Obsidian plugin was abused to deploy a remote access trojan | 366 | 229 |
| 2025-06-28 | I deleted my second brain | 598 | 348 |
| 2026-04-11 | Introduction to Obsidian | 209 | 154 |
| 2026-04-03 | Why are we still using Markdown? | 233 | 309 |

The appetite for reading and arguing about this product category is intact and
large. What has become improbable is a new entrant capturing it by announcing
itself.

### Second attempts that worked

Selected from 321 projects in the sample that were submitted more than once,
showing only cases where an early attempt drew almost nothing and a later one
reached 250 points or more:

| Project | First attempt | Best attempt | Gap |
|---|---|---|---|
| Obsidian – A knowledge base that works on local Markdown files | 2 pts, 2020-05-15 | **1087 pts**, 2020-05-27 | 12 days |
| Stop Taking Regular Notes; Use a Zettelkasten Instead | 3 pts, 2020-04-07 | **785 pts**, 2020-06-02 | 56 days |
| Files.md – Open-source alternative to Obsidian | 5 pts, 2026-05-12 | **730 pts**, 2026-05-18 | 6 days |
| Obsidian Bases | 6 pts, 2025-05-21 | **695 pts**, 2025-08-18 | 89 days |
| Zettlr – FOSS markdown editor for PKM and publishing | 4 pts, 2020-05-14 | **675 pts**, 2020-07-03 | 50 days |
| Standard Notes – longevity, portability, privacy | 3 pts, 2016-12-30 | **673 pts**, 2017-01-17 | 18 days |
| Quarkdown: A modern Markdown-based typesetting system | 3 pts, 2025-04-03 | **668 pts**, 2025-06-03 | 61 days |
| I deleted my second brain | 3 pts, 2025-06-20 | **598 pts**, 2025-06-28 | 8 days |
| Pell – A simple and small rich-text editor for the web | 6 pts, 2017-07-10 | **309 pts**, 2017-07-13 | 3 days |
| Logseq: Privacy-First, Joyful Platform for Knowledge Management | 11 pts, 2020-07-29 | **276 pts**, 2022-10-15 | 808 days |
| Shaky: ASCII Diagram to PNG | 2 pts, 2015-09-27 | **272 pts**, 2015-09-28 | 1 day |

Across the whole sample, projects submitted more than once cleared 100 points at
some point in **27.4%** of cases (88 of 321), against **15.0%** for projects
submitted once (377 of 2,521). Some of that gap is selection: authors resubmit
projects they believe in, and a project with several submissions has more chances.
The mechanism is visible in the individual rows, though — Files.md scored 5 points
on 12 May and 730 points on 18 May with the same link.

Hacker News permits this. The site FAQ states: "If a story has not had significant
attention in the last year or so, a small number of reposts is ok. Otherwise we
bury reposts as duplicates." The separate rule the guidelines do state firmly is
"Please don't delete and repost."

## Title features, measured

Every core-theme Show HN since 2023 (n=885), grouped by whether its title contains
a given pattern. The baseline row is the whole population.

| Title contains | n | Median pts | Mean pts | Reached 100+ | Under 5 pts |
|---|---:|---:|---:|---:|---:|
| *(baseline: all core Show HN)* | 885 | 3 | 26.8 | **7.6%** | 65.2% |
| an "alternative to X" construction | 31 | 7 | 96.8 | **25.8%** | 48.4% |
| "open source" / OSS / FOSS | 74 | 5 | 61.0 | **16.2%** | 44.6% |
| a language or runtime (Rust, C++, Go, Swift…) | 47 | 3 | 44.7 | 14.9% | 59.6% |
| "local-first" or "offline" | 40 | 4 | 37.7 | 10.0% | 57.5% |
| terminal / CLI / TUI / vim / emacs | 59 | 3 | 23.1 | 8.5% | 66.1% |
| a dash-separated description | 454 | 3 | 24.8 | 7.0% | 66.3% |
| a named competitor (Obsidian, Notion…) | 193 | 3 | 28.7 | 6.7% | 62.7% |
| macOS-specific framing | 42 | 3 | 15.4 | 4.8% | 66.7% |
| first person ("I built", "I made") | 58 | 2 | 20.3 | 3.4% | 70.7% |
| **"native"** | 33 | 2 | 11.8 | **3.0%** | 75.8% |
| "minimal", "tiny", "lightweight", "N lines" | 38 | 2 | 10.1 | 2.6% | 84.2% |
| **AI / LLM / GPT / agent** | 230 | 2 | 11.3 | **2.2%** | 71.3% |
| mobile / iOS / Android | 15 | 2 | 3.0 | 0.0% | 73.3% |

Read this as correlation confounded by project quality rather than as a set of
levers to pull. A
project that can describe itself as an alternative to Obsidian usually has more
substance than one describing itself as minimal. Two rows still deserve weight
because they are large and they point the same way as everything else in this
file: **AI or LLM in the title, across 230 posts, associates with a third of the
baseline success rate**, and **"native", across 33 posts, with under half of it**.

Other measurements from the same population:

- **Title length.** Titles of 6-8 words cleared 100 points 14.1% of the time
  (n=128); 9-11 words, 6.6% (n=348); 12-14 words, 6.4% (n=377); 15 or more, 4.3%.
- **Leading with a product name** ("Name – description") made no difference: 6.78%
  against 6.70% for titles without one, over 713 posts since 2024.
- **What the post links to.** Linking github.com: 7.77% reached 100 points and
  13.43% reached 30 (n=283). Linking a project's own domain: 6.17% and 9.00%
  (n=389). A text post with no link: 4.88% (n=41). The direction supports linking
  the repository, though the difference is not large enough at these sample sizes
  to be conclusive.

## Timing, and why the plan's window is not supported

The launch plan prescribes posting Tuesday to Thursday, 08:00-10:00 US Eastern.
Testing that against 516 core-theme Show HN posts since 2024, converted to US
Eastern from their submission timestamps:

| Window | n | Median | Reached 100+ |
|---|---:|---:|---:|
| Monday-Thursday, 08:00-11:00 ET (the plan's window) | 85 | 3 | 9.41% |
| Every other time | 431 | 3 | 9.28% |

There is no effect. By weekday, over 1,226 core stories since 2024:

| Weekday (ET) | Submissions | Reached 100+ |
|---|---:|---:|
| Saturday | 133 | 15.04% |
| Thursday | 201 | 12.44% |
| Sunday | 139 | 11.51% |
| Tuesday | 201 | 9.95% |
| Friday | 173 | 9.83% |
| Monday | 176 | 9.09% |
| Wednesday | 201 | 7.46% |

The best days are the ones with the fewest submissions, which is what the
fixed-slots explanation predicts. The effect is weak evidence rather than a
finding: Saturday against Wednesday gives p≈0.03, but weekend against weekday
across the whole sample gives p≈0.30, so it does not survive the wider comparison.
What the data does rule out is that the specific weekday-morning window is worth
planning around.

## Does the author's prepared first comment matter?

The launch plan treats a prepared first comment as essential. Sampling threads
from 2025 onward and checking whether the submitting account posted a top-level
comment:

| Group | n | Author posted own comment |
|---|---:|---:|
| Hits (100+ points) | 28 | 32.1% |
| Middle (20-99 points) | 30 | 16.7% |
| Misses (under 10 points) | 40 | 10.0% |

The gradient is confounded in an obvious way: a thread nobody visited gives an
author nothing to reply to. The finding that survives is the other one — **only a
third of the successful posts had an author comment at all**, so it is not the
practice that separates success from failure. It remains the right place to
disclose a commercial plan, which is a different justification from the one the
plan currently gives.

## Reproducing this

The scripts are in the session scratchpad rather than the repository, since they
are one-off research tools:

- `harvest.py`, `harvest2.py` — the broad themed harvest (28,296 stories) and the
  Qt/competitor pass.
- `unbiased.py` — the per-year census with timestamp range filters; this is the
  one that supports the trend claim.
- `analyze.py`, `analyze2.py` — bucket statistics and the title-feature table.
- `timing.py`, `timeanalysis.py` — submission timestamps, hour and weekday.
- `authorcomment.py` — the first-comment check.
- `catalog.py` — emits the tables above so no number is transcribed by hand.

All of them hit `https://hn.algolia.com/api/v1/`, which needs no authentication.
Re-running them against a later date is the way to check whether the crowding
described here continues.
