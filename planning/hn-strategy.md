# Reaching the Hacker News audience

*Working notes, kept in `planning/`, which is gitignored. Written 2026-08-09,
restructured the same day as the evidence changed what the priorities are. This
document turns the measurements catalogued in `hn.md` into decisions about how
Kvit Notes reaches Hacker News: which vehicle to use, what the title says, what
the reader lands on, what the first comment discloses, how the thread is answered,
and how many attempts to plan for. Where it contradicts `launch-plan.md` §G or
`to-market.md` §6, which were written in July 2026 from a sample of successful
stories only, the contradiction is stated in place with the measurement behind
it.*

## What to do first

The document is long because the evidence is. In priority order, the work it
implies:

1. **Fix the README's download table.** It offers `AUR kvit-notes-bin`, which does
   not exist on the AUR. This is the only item that is actively wrong today
   (Decision 4).
2. **Make the README's first sentence echo the title**, now that the title is
   settled: `Show HN: Kvit Notes – a Markdown editor with drag-to-edit Mermaid
   diagrams` (Decision 2).
4. **Record the method beside the performance table** and decide whether to
   measure macOS and Linux (Decision 7).
5. **Draft the first comment**, including the paid-tier disclosure and the diagram
   security position (Decisions 5 and 8).
6. **Establish how long Kvit has been in daily use**, as a number. On this
   evidence it is worth more in the post than any feature (Decision 5).
7. **Deploy the website**, because the essay needs somewhere to live that is not a
   repository. `planning/launch-steps.md` lists this as unmet.

## The situation

Kvit Notes is a native markdown block editor built in Qt 6 C++/QML and released
under MPL-2.0, and `launch-plan.md` makes a Show HN post its main event. The
measurements in `hn.md` change both what that event can be expected to do and
whether it should be the only one.

Across four full years of on-topic Show HN posts about markdown editors,
note-taking applications and diagram tools, the number reaching 100 points has sat
between four and nine in every half-year, with no trend. The number competing for
those places went from 40 in the first half of 2023 to 334 in the first half of
2026. The front page is a fixed-size shelf and the queue for it grew eight-fold.

The per-post rate therefore fell from about 10.5% in 2023 and 2024 to 5.45% in
2025 and 2.00% so far in 2026, and the half-year split shows 2025 as the year it
turned: 7.14%, then 4.14%, then 2.40%. Intuitions about this category formed
before roughly mid-2025 describe a regime that no longer exists.

So the base rate for a Show HN like this one is **about 2%**, and the problem to
solve is not describing the product well but giving someone who has scrolled past
eleven markdown editors this month a reason to stop at this one. Two structural
answers follow, and they matter more than any individual word choice: **use more
than one vehicle**, and **plan more than one attempt**.

## Decision 1: which vehicle, and how many

Classifying every on-topic story by how it was submitted produces the most
consequential result in the catalogue.

| 2025-2026 pooled | Reached 100 points |
|---|---|
| Article or other link (n=585) | **5.81%** |
| Show HN (n=658) | 3.34% |
| Bare repository link, no prefix (n=237) | 2.53% |

**Never submit the repository without the prefix.** It is the weakest route in
every year measured, and in 2026 it reached 100 points in 1 case out of 119.
Dropping "Show HN" to look less promotional costs reach and buys nothing.

**Articles outperform Show HN posts, and the gap widened through 2026**, where 14
of the 23 on-topic hits were articles against 8 Show HN posts. Part of that is
composition: the article population includes product news from Obsidian, Microsoft
and LibreOffice, which no new project can imitate. But the achievable subset is
substantial. "Ditching Obsidian and building my own", one developer's blog post
about why the incumbent did not fit and what they built instead, took 471 points
and 559 comments in May 2025, more than any Show HN in this category since. The
Daino Notes engineering piece, "Developing a Beautiful and Performant Block Editor
in Qt C++ and QML", took 146 points where Qt-titled Show HN posts die.

### The plan: two draws (owner's decision, 2026-08-10)

The measurements above argue for three draws — a Show HN, an essay, and a
conditional second Show HN. **The owner ruled on 2026-08-10 to drop the essay and
launch on the Show HN alone.** The record of what that costs, so it is not
rediscovered later: it gives up the higher-performing vehicle (5.81% against
3.34%) and reduces three chances to two.

The remaining shape:

1. **The Show HN**, which is now the launch rather than one part of it.
2. **A second Show HN**, only if the first draws no attention at all, under the
   conditions in Decision 10.

Two consequences follow from having one surface instead of two.

**The preparation items stop being optional**, because there is no second vehicle
to recover on. In order: fix the AUR row in the README (Decision 4), move the
Mermaid clip above the download table (Decision 4), publish the performance table
(Decision 7), and write the first comment carrying the paid-tier disclosure, the
diagram security position, the QWidgets result and the duration of use
(Decisions 5 and 8).

**The QWidgets finding now lives in the first comment** rather than in its own
piece. It is one sentence there instead of an article, which is a smaller return
on the experiment but still answers the most durable objection in the catalogue.

The four engineering deep-dives in `launch-plan.md` §G5 remain post-launch content
on their original cadence. Nothing here forbids writing one later; the decision is
only that the launch does not wait for one.

### The strongest article available: QML against QWidgets, measured

An experiment reimplementing Kvit's interface in QWidgets alone has now run, and
**it found little difference: QML holds up against Widgets on this workload.**
That is a null result, and it changes what the article is rather than whether
there is one.

**It is a better outcome for the launch than a win would have been.** "QML is
complete trash" and "for the desktop I would 100% use Widgets over QML" are the
most recurring criticisms in the Qt threads in this catalogue, and measurement is
the only thing ever observed to end that argument. The answer is now "I built it
both ways" rather than a defence, which is stronger than anything the experiment
could have produced by succeeding. It also resolves the sequencing worry recorded
here earlier: the result validates the shipped choice, so the piece is a clean
launch asset rather than an awkward one.

**Reframe the article from comparison to myth-busting.** "QWidgets was not faster"
is a weak premise. "Everyone told me QML was the wrong choice, so I rebuilt my
editor in QWidgets to find out" is a strong one, because the prevailing belief is
loud, confident and, on this measurement, wrong. Contradicting a widely-held
technical belief with your own numbers is the same move the Daino Notes author
used to win his thread.

**The gap it fills is documented and empty.** QML versus Widgets is the most
recurring argument in the Qt threads in this catalogue, and every instance of it
is assertion against assertion: "QML is complete trash. It's fine for simple UIs
with minimal logic and prototypes"; "For the desktop I would 100% use Widgets over
QML"; "it does not have feature parity with Qt Widgets, so you end up having to
roll up your own components". The Daino Notes author spent his whole thread
defending QML and prevailed only by producing benchmarks. Nobody has published the
controlled experiment.

**The genre performs, and this instance is rarer than the genre.** "We rewrote the
Ghostty GTK application" took 428 points and 224 comments; "I rewrote my Mac
Electron app in Rust" took 597 and 441. Those compare different applications by
different teams, which is why framework comparisons are usually dismissed. The
same non-trivial application, by the same developer, built twice, is the closest
thing to an A/B test this argument has had.

**The objection to pre-empt has changed with the result.** It is no longer that
hindsight made the second implementation easier. It is **"you did the QWidgets
port badly"**, and that will be the top comment if it is not answered first.
Disclose how much was built and which parts, whether the Widgets version was
idiomatic or a mechanical translation of the QML, and where a Widgets expert would
be expected to do better. A null result survives scrutiny only when its author has
already named the ways it could be wrong.

**What to put in it**, from what the corpus rewards: numbers before argument
(lines of code per feature, binary size, resident memory, cold start, frame times
under the same document, build times), then specific hard problems and how each
toolkit broke on them, then a section on where Widgets did win. A comparison with
no losses on either side reads as a pitch.

**Everything depends on whether the deltas are quantified.** A null result with a
table is a finding; a null result without one is an anecdote, and this audience
tells the difference immediately. Where differences were small but consistent in
one direction, give the magnitudes rather than rounding them to "no difference" —
the credibility of the whole piece rests on not overstating its own conclusion.

**Expectation:** 60-200 points. Lower than the band estimated before the result,
because a surprise carries less far than a dramatic improvement would, and Qt
engineering draws a smaller audience than Electron-versus-Rust. That still beats
the roughly 2% odds on a Show HN in this category.

### The Rust variant, deferred

A third implementation in Rust has been considered. Recorded here so the reasoning
is not re-derived later.

Measured against the tree: the QWidgets experiment replaces `qml/*.qml` (37,117
lines across 95 files) and the `src/qml` bridge (4,682 lines) while reusing the
six C++ core libraries (64,973 lines) and `third_party` (45,584 lines). The core
is held constant, which is what makes it a clean comparison.

Rust changes the shape of the problem, and MicroTeX is the reason. Those 45,584
vendored lines are a TeX layout engine with no Rust equivalent of comparable
maturity, and the evidence for that is the nearest competitor: Ferrite deferred
LaTeX to a future version and promised "pure Rust (no JS runtimes)", still
unshipped, while three commenters in its thread named maths as their blocker.
Kvit's maths advantage over Ferrite exists because of a C++ dependency. So a Rust
attempt forks two ways, and both are poor: keep the core and MicroTeX behind a
foreign-function interface, and the experiment becomes about building that
boundary rather than about the toolkit, since Qt types do not cross a C ABI
cleanly; or rewrite through, and lose maths and native Mermaid, which is most of
what distinguishes Kvit.

On content value alone, Rust rewrites do outperform, but the genre is saturated
and draws cynicism — the corpus contains "rewriting in another language is pretext
to write long form blog posts for seo and marketing purposes" — where QML versus
Widgets has no such problem.

**Decision: do the QWidgets experiment, defer Rust.** If it is ever revisited, the
framing that earns attention is not "I rewrote it in Rust" but "what it costs to
put a Rust UI on a 65,000-line C++ core", which is a question people face and
nobody writes up with numbers attached.

### Version-update posts do not work

Quarkdown reached 668 and 358 points with project posts, and "Quarkdown 2.4 –
Redefining Markdown" reached 1 point. Monthly releases with changelogs are worth
doing for users and are not Show HN material. The Show HN rules agree: "New
features and upgrades ... generally aren't substantive enough to be Show HNs."

## Decision 2: the title

The title currently proposed in `launch-plan.md` §G2 is:

> Show HN: Kvit – a native markdown block editor with live Mermaid editing (C++/Qt, MPL-2.0)

Three of its choices are measurably wrong, one is neutral, and one is right.

**"native" should come out.** Across 33 core-category Show HN posts since 2023
whose title contains the word, 3.0% reached 100 points against a 7.6% baseline,
and 75.8% finished under 5 points. The recent misses show why: eight of the
twenty-seven dead 2026 posts listed in `hn.md` are native markdown editors for
macOS, submitted within five weeks of each other. The word no longer separates a
project from the queue it is standing in, because the queue is made of native
markdown editors.

**"Qt" and "C++" should come out of the title.** Sixteen Show HN posts naming Qt,
QML, PyQt or KDE appear in the sample since 2023; one cleared 100 points, and
nothing has cleared 173 in that time. Recent instances scored 9, 3, 3 and 2
points. In the site's whole history only three Qt-titled Show HN posts have
cleared 100, and none has ever cleared 250.

The reason is visible in a near-controlled comparison between two front-page
projects that are both C++ with Qt and differ mainly in where they put the word.
Procal put Qt in the title, scored 173, and spent roughly a quarter of its thread
on a licence argument that ended with the author relicensing from MIT to GPLv3
mid-discussion, on advice a later commenter argued was wrong; the calculator
itself was barely discussed. Vicinae put "native" in the title and "written in C++
with Qt Widgets" in the first paragraph of the body, scored 181, and drew **zero**
toolkit pushback and zero licence discussion across 39 comments of feature
requests and install reports.

Naming Qt in the title converts the thread from a discussion of the product into a
discussion of the toolkit, and the loudest participants in that discussion are
carrying a fifteen-year-old grievance: "when I see 'Qt' the first thing that comes
to my mind is 'License Tomfoolery.'" Naming a language or runtime does associate
with above-baseline outcomes (14.9%, n=47), so if one technical word survives into
the title it should be "C++". The stack belongs in the first paragraph of the body
and in the first comment, where the reader who cares is already reading and where
the answer can arrive before the objection.

**The title is too long.** Titles of 6-8 words cleared 100 points 14.1% of the
time; 12-14 words, 6.4%. The proposed title is fourteen words including the
parenthetical.

**The licence in the title is neutral-to-positive** by way of the "open source"
row (16.2%, n=74), though "MPL-2.0" specifically is jargon where "open source" is
not.

**The construction that measures best is "an alternative to X"**: 25.8% reached
100 points across 31 posts, the highest of any feature tested. That number needs
its caveat stated, since it is confounded with project substance and the same
construction appears on dead posts ("TextZen – Lightweight Obsidian Alternative
for macOS", 4 points). It works when the project can carry the comparison and
fails when it cannot.

### Candidate titles

1. **`Show HN: Kvit Notes – an open-source Obsidian alternative with editable Mermaid diagrams`**
   (12 words.) Uses the strongest measured construction, names the differentiator
   that no competitor in the catalogue offers, and drops "native", "Qt" and the
   licence string. The risk is larger than the measurement alone suggests, and it
   is documented: in the thread most similar to Kvit's, three separate commenters
   attacked the word "alternative" itself. "When I read 'an alternative', I
   assumed feature-parity and API compatibility", and "When I read 'alternative' I
   immediately had a rant in my head about people calling things alternatives that
   are not." Since Kvit will not offer Obsidian plugin compatibility, the word
   writes a cheque the product does not cash. The phrasings that thread's audience
   offered instead, and which its author accepted, were "self-hosted markdown
   notes you fully own" and "open source markdown knowledge-base **similar to**
   Obsidian". A variant using "similar to Obsidian" keeps the comparison and drops
   the parity promise.

2. **`Show HN: Kvit Notes – drag a Mermaid node, watch the Markdown rewrite itself`**
   (11 words.) Leads with the single interaction nothing else in the catalogue
   combines, and describes a demo rather than a category. This is the
   highest-variance option: it either earns the click on curiosity or reads as a
   niche feature rather than an application.

3. **`Show HN: Kvit Notes – open-source Markdown editor with wiki-links, LaTeX and live diagrams`**
   (13 words.) Answers, in the title, the three things the Ferrite thread said
   were missing. Safest and least distinctive.

### The decision (2026-08-10)

**Primary title:**

> `Show HN: Kvit Notes – a Markdown editor with drag-to-edit Mermaid diagrams`

Eleven words, doing three things the evidence supports at once. It **names the
category**, which is what separated Ferrite from every near-twin that died
describing an "engine" or a "renderer". It **attaches the capability to something
the reader already owns**, which is the pattern separating 25 diagram successes
from 149 failures. And it avoids all four measured liabilities: "native" (3.0%),
Qt in a title (three posts above 100 in the site's history), AI (2.2%) and
"minimal" (2.6%).

This supersedes the earlier recommendation of candidate 2. That version, `drag a
Mermaid node, watch the Markdown rewrite itself`, has one weakness that matters
more now the essay has been dropped: a reader cannot tell it is a markdown editor.
A title that needs curiosity to decode is a worse bet when there is no second
vehicle to recover on.

**Reserve title, for a second attempt only:**

> `Show HN: Kvit Notes – an open-source Markdown editor similar to Obsidian, with editable diagrams`

A different frame, so using one does not spend the other, and "similar to" keeps
the comparison while dropping the parity promise that three commenters attacked in
the closest thread. The comment evidence against the bare word "alternative" is
specific, while the 25.8% figure supporting it rests on 31 posts and cannot
separate the phrase from the projects that chose it.

A cross-check from the diagram corpus supports the second candidate. Comparing 149
diagram Show HN failures against 25 successes, the pattern that separated them was
that **winning titles attach a capability to something the reader already has**,
whether that is their Mermaid files, their ASCII or their codebase, while losing
titles introduce a proper noun and leave the reader to work out what it replaces.
Candidate 2 does the former; candidate 1 does it by naming Obsidian; candidate 3
does it least well.

## Decision 3: the claim to lead with

The top comment on OpenKnowledge's thread, which is the funded competitor and
still reached 381 points, was:

> "I really wanted to like this, but unfortunately couldn't see how it improves my
> experience over Obsidian or VS Code."

That sentence is the post's real adversary, and it is not answered by describing
what Kvit is. It is answered by naming something the reader cannot currently do.
The Ferrite thread supplies the list, because its audience wrote it as complaints:

| What Ferrite's thread asked for | Kvit's position |
|---|---|
| "missing `[[wikilinks]]` and back references" | Shipped, with completion, a backlinks panel and rename-safe links |
| "the one thing that prevents me from trying this out ... is the lack of support for LaTeX" | Shipped, through a vendored MicroTeX with NewTX/XCharter faces |
| WYSIWYG rather than a split-pane preview | Shipped, as live-preview editing where syntax reveals around the caret |
| A packaging defect on first run | The macOS DMG is Developer ID-signed, notarised and stapled |

The OpenKnowledge thread adds two more that Kvit answers: "Obsidian is a lot more
than 'just markdown'", meaning the Dataview and Bases query features people depend
on, which is what Kvit's live query block does; and "every 'wysiwyg markdown' tool
i've tried falls apart" on front matter and code fences, which is what
byte-faithful round-tripping is for.

**That table is credibility rather than the lead.** Counting topics across 2,313
comments in the seven largest competitor threads puts wiki-links, backlinks and
graph view at 17 mentions out of 500 top-level threads, and LaTeX at 6, near the
bottom of what this audience asks for, below sync (92), plugins (72), query and
table views (75) and export (73). Graph view is actively mocked. Ferrite's thread
asked for wiki-links and maths because Ferrite lacked them, which makes them the
answer to "why not Ferrite" rather than the answer to "why not Obsidian". Lead
with the capability nobody has and use the list as evidence of completeness when
the comparison arrives.

The frame `to-market.md` chose, local-first and files-as-truth, remains correct as
a value statement while no longer being a differentiator: the Files.md thread
shows the audience treating plain files on disk as table stakes they already get
from Obsidian, and valuing that more than whether the application itself is open
source.

One more line from that thread is worth having in hand, because a commenter asked
for it unprompted: among the things people said were missing from the Obsidian
clones was "a native Qt alternative with lower resource usage". The demand exists;
it is simply not summoned by putting the word "native" in a title.

### The diagram feature is the strongest claim, and the demand is documented

A reading of 998 comments across thirteen diagram threads (catalogued in `hn.md`)
found that editing a rendered diagram and having the source update is the
most-requested unshipped feature in that corpus, asked for unprompted since 2023,
with nobody arguing it is impossible. The one Show HN that shipped a version of
it, a CLI that drags Mermaid nodes and writes the change back into the file as
comments, took 266 points to near-uniform enthusiasm: "exactly what I'm missing
from mermaid", "I was looking for exactly this last week".

**The competitive window is narrower than `to-market.md` assumed.** Ferrite's
author announced in January 2026 a plan to store node positions as `%% @pos`
comments so files stay compatible with stock mermaid.js. That is approximately
Kvit's shipped feature, announced by the nearest competitor seven months ago.
Mermaid Chart already advertises "bi-directional editing capability", and a user
in the same thread reports it not working. The claim to make is that Kvit's
version works, which is checkable, rather than that it is unprecedented, which is
no longer true.

**The demonstration will be judged on the diff rather than the drag.** The
best-informed discussion of this problem is the TikZ editor thread, whose author
says "the difficulty lies not so much in code generation, but determining what a
user would expect", and whose editor refuses many drags for that reason. The
criticism that drew agreement was that it rewrote relative positioning into
absolute coordinates. A commenter stated the requirement:

> "The killer feature for me is not drawing TikZ visually, but being able to touch
> old TikZ without turning the source into generated-looking soup."

If dragging a node produces a Mermaid file a person would have written, with
labels intact, ordering preserved and one line changed, the feature reads as
solved. If it produces a coordinate dump, the thread becomes an argument about
generated source. **The clip must show the source text changing in the same frame
as the node moves**, which the existing `mermaid.gif` does, and the post should say
in words what the write-back does to the rest of the file.

### What Kvit actually writes

Checked against `src/content/diagrams/mermaidedits.h` rather than assumed, because
this is the detail the thread will test. Kvit's gesture engine turns every
on-diagram gesture into "a targeted, formatting-preserving edit of the fence
source, computed from AST source offsets. The fence is never reserialized from the
model; bytes outside the specified span are preserved exactly, which the
byte-preservation property tests assert." Manual arrangement writes one line,
`%% mermaid-flow:pos`, which is a Mermaid comment that stock mermaid.js ignores,
so a diagram Kvit has arranged still renders on GitHub. Semantic gestures (rename,
delete, reshape, restyle, reparent, draw an edge) edit the real Mermaid statements
rather than appending coordinates, and every one of them "refuses rather than
applying approximately when the edit cannot be guaranteed to reparse without new
diagnostics."

Set against what the competitors ship:

| Project | What it does on drag | State |
|---|---|---|
| Mermaid Chart (the format's own vendor) | Advertises bi-directional editing | Shipped and reported broken in-thread: "it just wouldn't let me organize/drag things where I wanted"; the team conceded "This is definitely a gap in current Mermaid functionality" |
| oxdraw (266 points) | Writes positions back as Mermaid comments | Shipped; drew "I'm not convinced that creating a third standard is the answer" |
| Ferrite (241 points) | Announced `%% @pos` comments for v0.3.0 | Unshipped as of that thread |
| **Kvit** | Comment-line positions plus formatting-preserving semantic edits, with refusal rather than approximation | Shipped |

Two things follow. The refusal behaviour is the same design choice the TikZ editor
author was respected for, so it should be stated as a decision rather than hidden
as a limitation. And the portability objection was raised in three separate
threads, in the form "One major downside of native rendering is the lack of layout
consistency if you're editing natively and then sharing anywhere else" and "Do you
want to risk that your diagram changes outside of your control?", so the answer
that the file still renders on GitHub belongs in the post rather than in a reply.

**Frame the drag around version control rather than ergonomics.** The sentence
that resonated in the oxdraw thread was about commits: "the alternative is to
spill over to completely manually maintained diagrams using GUI tools which then
can't be easily integrated with source control - I want the same commit that
changes the code to also change the architecture diagram." The same user
quantified the pain at more than about five components: "I'm spending 20-30% of my
time desperately trying to tweak the layout with hints."

**The claim has to be checkable in the repository.** The first comment on
Ferrite's thread asked whether its Mermaid rendering was real or a JavaScript
interpreter in disguise, and two other readers answered by linking the source file
before the author replied. A title claim gets verified against the code inside the
first hour, which is an argument for naming the mechanism precisely enough that a
reader can find it.

There is a second, quieter framing that may be stronger than the drag. The most
common layout complaint in that corpus is instability under edits: "add a node and
you might end up with a graph that doesn't look like the previous version", "one
small change and it may completely rearrange the output". Pinned positions answer
that directly, and "your diagram stops rearranging itself when you edit it" is a
problem statement more readers recognise than "you can drag nodes".

### The ASCII pair is the second claim, with one accuracy caveat

No ASCII diagram post in the corpus failed: every one lands between 120 and 450
points, and the 149-post failure set contains none. The unmet ask in those threads
is precisely the round trip Kvit implements: "Is there something that can turn
those ascii diagrams back into mermaid sources?", "How do we get the original D2
source if there's a need to update the diagram?!", "Make it importable and i am
sold." Cascii's author explained that recovering structure from pasted ASCII was
hard enough that he shipped base64 export instead.

The caveat is in this repository's own record. `planning/gallery-handoff.md` notes
that what `asciipaste.gif` shows is characters being squared up rather than art
being converted into a diagram object. The launch copy must say exactly what it
does, which is that crooked box-drawing art is straightened on paste and that a
rendered diagram copies out as characters, without implying that pasted ASCII
becomes an editable Mermaid diagram. Overclaiming would be found within the hour,
because in these threads strangers re-ran every demo against harder input and
posted the failures.

## Decision 4: what the reader lands on

The link should be the GitHub repository, **with the "Show HN" prefix kept**. In
the sample, Show HN posts linking github.com reached 100 points 7.77% of the time
and 30 points 13.43% of the time, against 6.17% and 9.00% for posts linking a
project's own domain. The prefix matters more than the destination: the same
repository posted without it falls to 2.53% over 2025-2026 and to 0.84% in 2026
(Decision 1).

What the repository page must do is set by how Files.md's page is built, since
that is the highest-scoring page in the category in two years. It opens with one
sentence naming what the thing is for, shows a picture immediately, states the
data-ownership position early, and puts installation near the top. Kvit's README
already follows that shape. Two changes are worth making before the post:

- **The animated demo has to go above the download table.** The gallery of
  fourteen clips exists and is linked, but a reader arriving from a thread decides
  in seconds, and a still image asks them to imagine the interaction that the
  whole post is about.
- **Every install row must resolve.** The download table currently offers
  `AUR kvit-notes-bin`, which does not exist on the AUR. A commenter finding that
  in the first ten minutes converts the thread's tone from evaluation to doubt.
  The OpenKnowledge thread has exactly this comment: "Webpage is lying and showing
  stuff what you don't have in the app."

The diagram-thread evidence sharpens this further, because those threads contain
repeated, specific complaints about how projects present themselves:

- **The payload goes above everything, including badges.** oxdraw, the closest
  successful analogue, opens its README with two embedded videos before any prose.
  Requests like "have a non-login demo available on your website, and high-res
  screenshots/animated gif of the app in action on your Github repo" recur, and in
  one case the same commenter returned after the author complied: "I actually
  checked out your demo now because it didn't require a login, and was impressed."
- **Nothing may stand between the reader and the demonstration.** "Is a demo
  available without creating an account?", "You lost me at Create your account",
  and an objection to a demo that required installing an agent platform first.
  Kvit's advantage here is that a clip needs no install at all.
- **It has to survive dark mode.** One project's ASCII render appeared as black on
  black for a reader in dark mode, reported in-thread. Kvit's clips are recorded
  in a light theme, so this is worth one check on a dark-themed GitHub view before
  posting.
- **Small presentation faults get reported publicly.** Three separate people in
  one thread could not find the example buttons on a demo page; another objected
  that controls were drawn over the diagram; another reported an image that failed
  to load. These are cheap to prevent and awkward to answer live.

## Decision 5: the first comment and the paid-tier disclosure

Two corrections to the current plan.

**It is not the lever the plan thinks it is.** Sampling threads from 2025 onward,
only 32.1% of the posts that cleared 100 points had a top-level comment from their
own author, against 10.0% of the posts under 10 points. The gradient mostly
reflects that a dead thread offers nothing to reply to. Two thirds of successful
posts had no author comment at all, so writing one is not what produces the
outcome.

**It is still where the commercial plan is disclosed**, and that justification
stands on its own. The evidence on how to phrase it:

- The Daino Notes thread is the closest precedent, being the same framework, the
  same product category and a solo developer, and its author's move from open
  source to closed with paid blocks drew reactions that were "mixed but
  predominantly pragmatic rather than hostile". The advice that surfaced was
  consistent: charge for infrastructure (sync, storage, collaboration) rather than
  for features that run on the user's own machine. Kvit's planned tier is
  agent-workflow tooling, which sits closer to infrastructure than to a feature
  unlock, and the disclosure should say which side it is on.
- The Files.md thread shows the audience's scepticism about the alternative:
  "Donationware is a viable business model for basically nobody." Saying that a
  paid tier is planned is better received than leaving the question open.
- The OpenKnowledge thread shows the one line to avoid. Its harshest comment
  attacks calling something open source while it depends on proprietary AI
  services. Kvit's agent integration runs a command-line agent as a subprocess
  with the user's own subscription, and saying that explicitly forecloses the
  objection.

A draft, to be edited by the person posting it rather than pasted:

> I built this because editing markdown alongside coding agents was awkward in
> everything I tried. The parts I think are technically interesting: the
> live-preview engine reveals a span's syntax only around the caret; the five
> Mermaid grammars are parsed and laid out in C++ with no JavaScript runtime, so
> dragging a node edits the fence source in place rather than regenerating it, and
> positions go in a `%%` comment that stock mermaid.js ignores, so the file still
> renders on GitHub; maths goes through a vendored MicroTeX with NewTX faces; and
> block virtualisation loads a 561,693-word document in 40 ms.
>
> It's C++ and Qt 6, LGPL modules linked dynamically, no GPL-only Qt module, and
> the whole thing is MPL-2.0 anyway. Before you ask why QML and not Widgets: I
> reimplemented the interface in QWidgets to find out, and the difference was
> small enough that it did not justify moving. Numbers in [link].
>
> Deliberately absent: Electron, a database, telemetry, mobile and sync. The vault
> is an ordinary directory, so Syncthing or a git remote handles sync and I don't
> have to run a service.
>
> **The editor is MPL-2.0 and stays that way** — everything in the app today is
> open source and I'm not going to relicense it later. What I plan to sell is a
> separate module rather than a feature carved out of this one: a review step for
> edits an agent makes to your notes, where you see a diff and approve it before
> anything is written. It's new code beside the editor, so nothing you're using
> now moves behind a paywall, and you bring your own Claude or Codex subscription
> because it runs the agent as a subprocess rather than calling a service of mine.
> Your notes stay plain markdown in a folder you control, and because the editor
> is MPL, if I get bored or get hit by a bus you keep both the files and an editor
> you can fork.
>
> The clips in the README were recorded by the project's own test harness driving
> the real application, so they re-record on every release.

Evidence for the parts that changed. "Stays that way" is modelled on a
pre-commitment that closed a hostile subthread elsewhere ("the core app is fully
open-source and always will be"); the bait-and-switch charge in this sample
attaches to the event of relicensing, at 4.7% of comments where a project actually
relicensed against 0-1% at launch, so pre-committing removes the trigger. "A
separate module rather than a feature carved out of this one" answers both the
older open-core complaint about withholding features and the 2026 version of it,
that paywalled features get reconstructed with a language model anyway. The
fork-if-abandoned clause answers the question this audience actually asks:
explicit sustainability questions run 0-3 per thread while data-portability
language runs 10-13, and the accepted answer is the licence, as in "Dendron is
basically abandonware... but because it's Apache licensed, I can fork it."

**Do not describe the paid module as "AI features", and do not let it imply the
free app is unsafe without it.** The first invites the hype-label reflex that met
"AI-first" with "It's a bit like 'Roomba-compatible'". The second invites the
reaction a different project got for gating something security-shaped: "Let's
actively not support software that chooses anti-security." Frame it as reviewing
edits the user has chosen to let an agent make.

**Say how long this has existed and how it was built.** This did more for the
highest-scoring analogue in the catalogue than any feature it listed. Files.md's
author wrote "I've been perfecting things for 5 years", and the reply was "That's
the comment that made me check it out in more detail, as that sticks out from all
the other projects that were built in a weekend." Another project's most-quoted
line was 10,000 notes over six years. Provenance is now a credibility gate in this
category, and one commenter named the mechanism: a development-timeline note reads
"as an anti-slop signal".

**Write it yourself.** The Ferrite thread contains a well-placed comment asking
whether the post had been "written using an LLM", and the author confirming that
the replies were AI-assisted. Ferrite also disclosed that its code was largely
model-generated, and a visible slice of its thread turned hostile: "In one second
I went from 'looks cool' to 'I don't want to touch it'."

The contrast worth studying is the TikZ editor at 451 points, whose author also
disclosed heavy model use but gave numbers for it, explained how he verified the
renderer's output against real LaTeX, and was defended by commenters. The
difference was not whether tools were used but whether the author demonstrated
engineering judgement alongside the disclosure, and whether the replies read as a
person answering. Kvit has the material for the first half of that: a test suite
that gates every commit, QA runs recorded per platform, and a demo recorded by the
harness itself. The second half means writing the replies live.

## Decision 6: the objections, answered in advance

Prepared answers, each traced to where the objection actually appeared.

| Objection | Where it appeared | Answer to give |
|---|---|---|
| "How is this better than Obsidian or VS Code?" | Top comment, OpenKnowledge | The four-item capability table from Decision 3, stated as facts rather than claims |
| "Why another markdown editor?" | Ferrite thread: people prefer "$EDITOR with plugins" | Ferrite's author's answer worked: the market is writers, not developers who want an IDE plugin |
| "No mobile, no sync, so it is useless to me" | Files.md, OpenKnowledge, throughout | Say it is out of scope and why, and name the sync methods that work today because the files are ordinary files |
| "Qt/QML is the wrong choice" / "QML is complete trash" | Daino thread, verbatim, and four more threads | Measurement is the only thing observed to end this. Two are now available: Kvit is 83 MB lighter than Daino on the same document (Decision 7), and the QWidgets reimplementation found little difference, so QML is not what costs anything here |
| "Why QML and not Widgets?" | The follow-up in every QML thread | Answer it unprompted in the first comment; Vicinae did exactly this in one sentence and drew no toolkit discussion at all. Kvit can now go further and say it built the Widgets version to check (Decision 1) |
| "Don't you need a commercial Qt licence?" | Files.md, stated incorrectly and corrected by two others; Procal, where it consumed a quarter of the thread and produced an MIT-to-GPLv3 relicensing | One unprompted line: LGPL modules linked dynamically, no GPL-only Qt module, and the project is MPL-2.0 regardless. Procal's trap was PyQt's GPL-or-commercial bindings, which a C++ project does not touch, so say that rather than conceding a problem Kvit does not have |
| "No plugins means I lose functionality" | 72 of 500 top-level threads across the competitor set | List the plugins people name as indispensable (Dataview/Bases, Kanban, Mermaid, Templater, omnisearch, LaTeX Suite) as things shipped first-party. Never answer "clone it and ask an LLM", which was explicitly rejected |
| "Is 'local-first' true? What does it call out to?" | The Atomic thread, where a reader ran a packet capture | State the single egress policy and the update check before anyone tests it; `docs/adr/0003` is the answer |
| "Blocks? I like typing ### for a heading" | Split audience: Notion refugees want blocks, Obsidian users want raw markdown | Say that blocks accept markdown shortcuts and round-trip byte-faithfully, which is also the answer to "does it mangle front matter and nested fences" |
| "Just another disposable app by one person, max lifespan 2 years" | Tolaria, first substantive comment | Six people pushed back on it there without the author's help; answer with duration of use and the recorded QA and release process |
| "Electron would have been fine" | Recurring | Do not fight it. Post the table from Decision 7 once, add the large-document timings, then move on |
| "Open core is a bait and switch" | Implicit in the Files.md sustainability thread | The MPL-2.0 boundary is file-level and the editor's files stay open; the paid tier is separate files |
| "Is this abandoned in a year?" | Sustainability discussion, Files.md | Point at the release ladder, CI on three platforms, and the QA record in `docs/qa-runs/` |
| "The site shows things the app does not do" | OpenKnowledge, verbatim | Prevented rather than answered: every claim in the README must be in the shipped build |
| "A visual editor for a plain-text format is like a WYSIWYG markdown editor, so why not use a word processor?" | Mermaid GUI thread, verbatim | The answer that worked in-thread was that the file stays reviewable text; Kvit's version is that the diff after a drag is one changed line |
| "Auto-layout exists so I don't have to fiddle; dragging is the disease" | Diagram threads, recurring | Position pinning is opt-in and its purpose is stability under edits rather than decoration |
| A stranger re-runs the demo on harder input and posts the failure | Every ASCII diagram thread | Answer with the limitation and the roadmap; authors who did that kept their threads friendly |
| "This is just X with a new name" | Mermaid ASCII thread, where prior art was uncredited | Name the prior art first: mermaid-ascii, asciiflow, Monodraw, svgbob, D2 |
| "Can a malicious diagram in a note exfiltrate data?" | Not yet asked of Kvit, but Microsoft 365 Copilot (218 pts, Oct 2025) and Cursor were both exploited through Mermaid rendering | Kvit's parser discards `click` and `linkStyle` with a diagnostic, and its renderer has no networking, URL handling or image loading. Say it unprompted; see Decision 8 |

## Decision 7: what to measure, and what to claim

`hn.md` records the thresholds this audience applies to a native claim, which it
checks personally and quickly: under about 20 MB resident draws no scrutiny, 85 MB
was called "electron territory" about a native macOS application, and Obsidian's
roughly 300 MB is the number readers already carry.

Kvit's own figures, all measured on one Windows machine and all hand measurements,
since nothing in the repository's performance harness measures memory:

| Application | Stack | Installed | Idle | War and Peace open | Cost of the document |
|---|---|---:|---:|---:|---:|
| **Kvit Notes** | Qt 6 C++/QML | 140 MB | **112 MB** | **185 MB** | **+73 MB** |
| Daino Notes | Qt C++/QML | **136 MB** | 156 MB | 268 MB | +112 MB |
| Obsidian | Electron, 5 processes | 320 MB | 187-268 MB | 278 MB | not computable |

**The two Qt applications ship at the same size and behave differently, which is
the most useful row in the table.** Two independently built Qt 6 QML block editors
landing within 4 MB of each other says that roughly 137 MB is what a
self-contained Qt application of this kind costs on disk, so disk footprint is
largely a property of the toolkit. Memory is where they separate: 112 MB against
156 MB idle, and 185 MB against 268 MB with the same 561,693-word document open.
**Because the stack, the language and the disk footprint are held constant, that
83 MB gap cannot be attributed to Qt or to avoiding Electron. It is attributable
to the architecture**, which is what block virtualisation is for, and it is the
rare comparison where the variable is isolated. It answers the "QML is heavy"
objection against another QML application rather than against a strawman.

**The 4 MB Kvit gives away on disk is the more interesting number once you ask
what is inside it.** Daino Notes has no formula support, confirmed by opening it
on 2026-08-09. Kvit's 140 MB includes a complete TeX implementation, and the
resource tree alone measures 2.9 MB: 1.4 MB of fonts, of which 844 KB is the NewTX
and XCharter set, plus 896 KB of Greek and 564 KB of Cyrillic coverage, with the
compiled MicroTeX code on top. So Kvit ships an entire maths stack for roughly the
space it costs to store it, and comes out 3% larger than a comparable editor that
renders no formulas at all. Read the other way, Kvit is smaller than Daino on
everything that is not maths. That is a better line than "we are smaller", because
it is an argument about density rather than a race, and it pairs with the Ferrite
thread where LaTeX was named as the single blocker to adoption. Since it is a
claim about a named competitor whose developer is active on Hacker News, re-check
it on the day and phrase it as what Kvit includes rather than what she lacks.

**Against Obsidian the disk comparison is 44%, and the composition makes it better
than the ratio.** Obsidian's 320 MB is close to the floor for an Electron
application, since Chromium, V8 and Node account for roughly 200-250 MB on Windows
before any application code, so its own payload is small and its extensibility
lives outside the install directory, in the vault. Both Qt applications are the
opposite shape. Kvit's 140 MB bundles the Qt runtime, MicroTeX with the NewTX and
XCharter faces, the Greek and Cyrillic font packs, and the FFmpeg multimedia
plugin, and the Windows packaging job proves it is self-contained by running a
headless `--math-selftest` with the Qt kit stripped from `PATH`.

Three consequences.

**Publish the table, never a bare idle number.** 112 MB on its own invites the
Electron comparison; 140 MB installed and 185 MB with the same public-domain
document open, against 320 MB and 278 MB, is a finding a reader can download and
reproduce. The table answers the QML objection, the Electron objection and the "is
it really native" objection at once, which is the only tactic observed to end any
of them. Install size is also the axis this audience reacts to most warmly: one
2 MB application drew "It can fit onto two floppy drives!"

**Pair the table with the timing claims, which measure the same file.** A
561,693-word document loading in 40 ms and keystroke latency held under 16 ms are
falsifiable, unusual, and backed by a test that fails the build. The standout
comment in a 355-comment thread was an enumerated engineering budget ("keystroke
to pixel under 8ms p99 … plus a variance-tail contract so sporadic stalls fail the
build too"), and it stood out because it was checkable. Cold start is the second
axis worth measuring. "Instant startup" was accepted from Ferrite without
challenge but cannot be defended when disputed, so a measured figure is worth more
than the adjective.

**Record the method alongside the table, because a table invites reproduction.**
State the machine, the build, that the document is War and Peace at 561,693 words
with a link to the source text, and that Obsidian's figure is the Task Manager
group total across its five processes. That last point matters: in one sample the
group row read 231.9 MB while its five children read 8.7, 56.0, 31.6, 90.6 and
44.9 MB, and the 44.9 MB child was a renderer for a Release Notes window a normal
session would not have open. Obsidian's idle memory moves by roughly 40% with what
is open, which is why the loaded column is the one to publish and an idle claim
pinned to 268 MB would fail the reproduction it invites.

Two questions remain open: whether to repeat the run on macOS and Linux, where
working-set accounting differs enough that a single cross-platform number would be
wrong on two of the three, and whether the 112 MB idle figure is worth reducing.
It is not a defect and chasing it is unscoped work, but it is worth knowing what
it consists of, since much may be one-off atlases and scene-graph buffers rather
than anything proportional to the document.

**Claim the accessibility work, because nobody else does.** None of the seven
largest competitor projects had an accessibility story, and in one of those
threads the highest-ranked comment was a blind screen-reader user and professional
auditor asking whether anyone had thought about it and offering a free review;
that subtree ran to 23 comments. Kvit has an enforced answer rather than an
intention: accessible names checked in CI by `tools/check-accessible-names.py`, a
documented contrast floor, and full keyboard reachability, described in
`accessibility.md`. Stating the mechanism rather than the intention is what would
distinguish it, and on this evidence it is as likely to earn a top comment as to
deflect one.

## Decision 8: claim the diagram security position, unprompted

The full-year census surfaced a story the earlier sampling had missed, and it is
the most useful thing found in the second half of 2025. "Microsoft 365 Copilot –
Arbitrary data exfiltration via Mermaid diagrams" took 218 points in October 2025.
The mechanism combined prompt injection with clickable links inside the rendered
diagram, and a commenter names a second case from August 2025 in which "Cursor's
Mermaid implementation could render external images."

Two exfiltration routes through Mermaid rendering are therefore publicly
documented, both against AI-adjacent products, both within a year of this launch:
interactive links and external image fetches. An editor that renders Mermaid and
discloses a plan to sell agent-workflow tooling is squarely in the blast radius of
that question, and the question is fair rather than hostile, because a note is
untrusted input.

Kvit's answer is unusually clean, and it is checkable in the repository:

- `src/content/diagrams/mermaidparser.cpp:757` parses `click` and `linkStyle` and
  discards both, with the diagnostic "`click` is ignored in Kvit (interactivity
  and link styling are not supported)". The link route does not exist.
- `mermaidrenderer.cpp` and `diagrampainter.cpp` have no networking, no URL
  handling and no image loading. The external-image route does not exist.
- What does reach the network on a note's behalf goes through one egress policy in
  `src/platform/`, documented in `docs/adr/0003-network-egress-policy.md`.

**Put this in the first comment rather than saving it for a reply.** It converts a
feature the audience might treat as a liability into evidence of judgement, it is
the kind of claim readers verify in the source within the hour, and it pairs with
the point that a native renderer with no JavaScript runtime has a smaller attack
surface than one that hosts a browser engine. It also sits well beside the
disclosed paid tier: the product that reviews what agents write to your notes is
the one that refuses to let a note's diagram call out.

## Decision 9: timing

`launch-plan.md` §G2 prescribes Tuesday to Thursday, 08:00-10:00 US Eastern. That
window has no support in the data. Across 516 core-category Show HN posts since
2024, the plan's window reached 100 points 9.41% of the time and every other time
reached it 9.28%.

By weekday there is a weak pattern in the opposite direction from the usual
advice: Saturday is the best day in the sample (15.04% over 133 submissions) and
Wednesday the worst (7.46% over 201), and the ordering tracks how many stories
were submitted that day rather than anything about the audience. Saturday against
Wednesday reaches p≈0.03, but weekend against weekday over the whole sample gives
p≈0.30, so this is a hint rather than a finding.

The practical instruction: **choose the date from readiness rather than from the
calendar**. If two dates are equally ready, prefer the quieter day.

## Decision 10: how many attempts to plan for

`launch-plan.md` §G6 states that "a Show HN gets one shot per project" with a
repost window opening at about six months. Both halves are wrong.

Hacker News's own FAQ says: "If a story has not had significant attention in the
last year or so, a small number of reposts is ok. Otherwise we bury reposts as
duplicates." The rule that is stated firmly is a different one: "Please don't
delete and repost."

The sample contains 321 projects submitted more than once. Those projects reached
100 points at some point in 27.4% of cases against 15.0% for projects submitted
once. Part of that gap is selection, since authors resubmit what they believe in.
The individual rows are harder to explain away:

- **Files.md** scored 5 points on 12 May 2026 and 730 points on 18 May 2026, the
  same project six days apart.
- **Obsidian** scored 2 points in May 2020 and 1,087 points twelve days later.
- **Zettlr** scored 4 points, then 675 seven weeks later.
- **Quarkdown** scored 3 points in April 2025 and 668 in June.
- **Standard Notes**: 3 points, then 673 eighteen days later.

The reading that fits both the arithmetic and these cases is that the outcome is
dominated by the first half hour, by who happens to be reading and what else was
submitted, so a single sample of a high-variance process carries little
information about the project. A first attempt that draws nothing tells you which
morning you picked rather than what the work is worth.

What this changes operationally:

- **Do not spend the whole launch on one morning.** Hold the reserve title in
  Decision 2 for the second attempt.
- **If the first attempt finishes under about 10 points with no discussion, treat
  it as a draw** and plan a second attempt a few weeks out, with a different title
  and something that has changed in the project to point at.
- **Do not delete the first submission.** The guidelines forbid delete-and-repost,
  and a low-scoring submission left alone is what makes the repost legitimate.
- **Do not resubmit a thread that did get attention.** If the first attempt earns
  a real discussion, that is the one Show HN, and the follow-up is the essay.

The counterexample is worth as much as the successes. On 6 August 2026 an author
posted "Show HN: Line9 – a Mermaid rendering engine with its own layout" and got 2
points; the next day the same product returned as "Show HN: Mermaid flowcharts you
don't have to redraw in a diagram editor" and got 3. The second title is a good
title, naming the reader's problem in the reader's words, and it changed nothing.
What that rules out is the cheap version of this tactic: a retitle a day later,
with nothing else different, is not a second attempt. The successful cases in the
catalogue are separated by weeks and are usually accompanied by the project itself
having moved.

## Decision 11: the things not to do

- **Do not put AI, LLM or agent in the title.** 230 posts did; 2.2% reached 100
  points against a 7.6% baseline, the worst-performing feature measured. The agent
  story belongs in the first comment, which is also where `to-market.md` put it for
  different reasons.
- **Do not lead with "native".** Covered in Decision 2.
- **Do not lead with minimal, tiny, or lightweight** (2.6%, n=38, and 84.2% of
  them finished under 5 points).
- **Do not open with "I built"** as the title's first move (3.4%, n=58). It reads
  as the default now.
- **Do not ask anyone to upvote.** The guidelines: "Don't solicit upvotes,
  comments, or submissions."
- **Do not post a landing page.** The Show HN rules exclude "blog posts, sign-up
  pages, newsletters" and want the thing runnable, which is a second argument for
  linking the repository over kvit.app.
- **Do not claim the ASCII repair does more than it does.** See Decision 3; the
  distinction between straightening characters and reconstructing a diagram is
  exactly the kind of thing these threads test within the hour.
- **Do not present the diagram work as unprecedented.** Name the prior art
  (mermaid-ascii, asciiflow, Monodraw, svgbob, D2, oxdraw) before someone else
  does. In one thread a project was accused of copying an earlier one and the
  moderators changed the submitted URL mid-discussion to credit the original.

## What success should be defined as

`to-market.md` §10 sets the expectation at 250-700 points and 1.5-4k GitHub stars,
calibrated against Ferrite (241 points to 1.7k stars), Files.md (730 to 3.8k) and
OpenKnowledge (381 to ~1.8k). Those three are the top of a four-year distribution
in which the median post in the category scores 2 points. Keeping them as the
planning number sets up a launch that is judged a failure by its own document at
almost any realistic outcome.

These bands describe the Show HN alone. Decision 1 gives the launch three possible
draws, so a weak Show HN followed by a strong essay is a normal outcome rather
than a failed launch.

| Outcome | What it means | What to do next |
|---|---|---|
| Under 10 points, no discussion | The morning rather than the project | Treat as a draw; second attempt per Decision 10 |
| 30-100 points, real comments | Seen by the audience, no front page | Work the thread, take the issues, run the follow-on venues |
| 100-300 points | A successful launch by the current base rate | Everything in `launch-steps.md`, and the content calendar starts |
| Over 300 points | Top few outcomes of the year | The plan's traffic and triage assumptions apply |

The one number worth holding onto from the old expectation is the conversion
ratio: Ferrite turned 241 points into roughly 1.7k stars, so points do convert
when they happen.

## What is still open

- ~~**Which title to use.**~~ Decided 2026-08-10; see Decision 2.
- ~~**Whether the multi-draw plan is acceptable.**~~ Decided 2026-08-10: Show HN
  only, no launch essay; see Decision 1.
- **Whether to name Obsidian in the reserve title.** `to-market.md` §3 ruled that
  "a better Obsidian" is a losing frame and that "Obsidian alternative" is an SEO
  tag rather than a headline. The measurement disagrees for a title specifically,
  and the reserve title in Decision 2 uses "similar to Obsidian" rather than
  "alternative". This only needs deciding if a second attempt happens, and the
  deciding question is whether Kvit is ready to be compared directly against
  Obsidian in a 300-comment thread.
- **Whether to publish the performance table**, which Decision 7 recommends. The
  Windows column is complete; what remains is the method note beside it and
  whether to measure macOS and Linux.
- **How long Kvit has been in development and daily use**, stated as a number. The
  record in this repository starts well before the public repository does, and the
  figure that matters is how long the editor has been used for real notes rather
  than when the first commit was made. Only the owner can answer this, and the
  evidence says it is worth more than any feature in the post.
