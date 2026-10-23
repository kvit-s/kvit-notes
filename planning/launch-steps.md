# Launch day: what has to happen, in order

*Working notes, kept in `planning/`, which is gitignored. Written 2026-08-09.
This document covers the day Kvit Notes is announced publicly, and the hours
either side of it. It states what must already be true when the day
starts, what happens during it in sequence, how to answer the thread, what to
do when something breaks, and how the day is closed out. Where a precondition
is not met today, it says so.*

## What this project is, for a reader arriving cold

Kvit Notes is a native markdown block editor written in Qt 6 C++/QML: notes
are plain `.md` files on disk, shown fully rendered, with Mermaid diagrams and
TeX math drawn by the application rather than by a bundled browser engine. It
is open source under MPL-2.0 at `github.com/kvit-s/kvit-notes`. The
open editor is stage one of a three-stage plan; a paid tier built around
reviewing what AI agents write into your notes is stage two, and it is not
part of this launch beyond being disclosed.

Three documents in the private `kvit-notes-pro` tree govern the launch and are
not repeated here: `to-market.md` (why the editor is open source, who the
audience is, what results to expect), `launch-plan.md` (the phased execution
plan, of which §G is promotion), and `m2-owner.md` (the steps only the owner
can take). A fourth, `planning/marketing-plan.md` in this tree, plans the
materials: the video, the stills, the website, the launch post. This document
is the narrowest of the five: the runbook for the announcement itself.

## What counts as launch day

The launch event is a single Show HN post on Hacker News linking the GitHub
repository, plus the rest of that day spent answering the thread. Three things
that sound like part of it are not:

- **The soft launch** (repository public, a release candidate published, five
  to ten testers on all three platforms, defects fixed) runs for about two
  weeks before, and the stable `v1.0.0` publishes several days before the
  announcement rather than on the morning of it. Testers must have installed
  the same artifact a launch visitor downloads.
- **The subreddit, Lobsters and Qt-forum posts** are follow-ons spread over
  the days after, one per day, each written for its venue. Posting them the
  same morning splits attention and reads as a campaign.
- **The directory listings** (AlternativeTo and the rest) are the week after.

The reason for the separation is that a Show HN gets one attempt. If the
download is broken or the maintainer is not at the keyboard, that attempt is
spent.

## Preconditions

Each row must be true before the morning starts. The state column is where
things stand on 2026-08-09.

| Precondition | Why it gates the day | State |
|---|---|---|
| Stable `v1.0.0` published, several days old | Visitors install the artifact testers already vetted | **Not met.** `v1.0.0-rc1` is published as a prerelease (2026-08-05); no stable tag exists |
| Soft launch complete: 5–10 testers, zero open P0/P1 | The first-try download working is what the day is spent protecting | **Not met.** No testers recruited |
| QA checklist green per platform against the shipping artifact, recorded in `docs/qa-runs/` | Release gate in `docs/release-rollback.md` | **Not met.** Three exploratory pre-tag passes exist (2026-08-01/02/03); none is against a tag |
| Every download link in the README resolves to something installable | A dead link in the first paragraph is the worst possible first impression | **Not met.** The download table offers `AUR kvit-notes-bin`; no such package exists on the AUR |
| CI green on the tagged commit, including the licence, notices, secrets and screenshot-leak checks | Provenance of the artifacts | Green on `main` as of 2026-08-09 |
| Website live at kvit.app | The second-best landing place, and where the launch post lives | **Not met.** kvit.app currently serves an unrelated placeholder page; the Notes site is staged at `~/kvit-hub-data/projects/kvit-notes/site/` |
| Show HN title and first comment drafted, reviewed, in a file the owner can paste from | The comment goes up within a minute of the post; there is no time to write it then | **Not met.** No draft exists in either tree |
| Gallery and stills current | What a visitor looks at before deciding to download | Met. `gallery.md` with fourteen clips, four press stills, committed |
| GitHub Discussions enabled with Q&A and Ideas | Keeps the issue tracker for defects | Met |
| Mastodon account registered; X handle renamed from `@kvit132827` | The one same-day social post | **Not met** |
| Flathub submitted (review does not compress) and the AUR package published | Linux install paths, and Flathub is the only public install counter | **Not met** |
| Weekly metrics capture running | GitHub retains traffic data 14 days, so launch week is unrecoverable if missed | Script staged at `~/kvit-hub-data/projects/kvit-notes/metrics/capture.sh`; not scheduled |
| A cleared calendar | Four hours of unbroken attention, minimum | Owner's call |

Four of these are long poles rather than morning tasks: the soft launch takes
about two calendar weeks, Flathub review takes days to a couple of weeks and
starts roughly two weeks out, the website needs deploying before the launch
post has anywhere to live, and Windows code signing through Azure Artifact
Signing, about $10/month, needs its identity validation queued a week or two
before the build that carries it.

## The day, in sequence

Times are Central, the development machine's zone; the Eastern times the plan
specifies are one hour later. Post Tuesday through Thursday. Monday competes
with the weekend backlog and Friday empties by afternoon.

### The evening before

Verify from outside the development machine, because everything below has
passed on it already and that is exactly why it proves nothing:

1. Download each artifact from the GitHub release page on the platform it
   targets, install it, and open a note. Windows through the installer and the
   portable zip; macOS through Gatekeeper's first-open on the DMG; Linux by
   marking the AppImage executable and running it.
2. Open the README on github.com and click every link in it, including the
   gallery, the install rows and the badges. The AUR row is a known defect
   today; either publish the package or remove the row.
3. Load kvit.app on a phone. The download button detects the operating system,
   and a phone is the case it was least likely tested against.
4. Confirm the release is marked stable rather than prerelease, that its notes
   are generated from `CHANGELOG.md`, and that `SHA256SUMS.txt` and
   `THIRD-PARTY-NOTICES.md` are attached.
5. Have the title, the first comment and the social post open in a text file,
   already proofread. Nothing is composed on the morning.
6. Sleep. The thread is answered for four hours straight, and tone is the
   thing that degrades first.

### 06:30 — thirty minutes before

Confirm the release page still loads, the site still resolves, and no overnight
issue has been filed against the release. Open in tabs: the repository's issues
page, Discussions, the release page, the GitHub Actions tab, and the Algolia
Hacker News front page.

### 07:00–08:00 — post

Submit to Hacker News with the title decided in `launch-plan.md` §G2:

> Show HN: Kvit – a native markdown block editor with live Mermaid editing (C++/Qt, MPL-2.0)

The link is the GitHub repository, not kvit.app. Show HN is for work a reader
can try, and the calibration set supports it: Files.md at 730 points and
OpenKnowledge at 381 both linked source. The README already opens with the
claim, the hero still and a download table, which is what a reader arriving
from the thread wants in their first ten seconds.

### 07:05 — the first comment, immediately

Posted as a comment on the submission rather than as part of it. It has five jobs,
and the disclosure is the one that cannot be skipped:

1. **Why it exists.** Editing markdown alongside AI agents was awkward in
   existing tools, and this was built as the surface for that.
2. **What is technically interesting.** The hybrid live-preview engine where a
   span reveals its own syntax around the caret; five Mermaid grammars parsed
   and laid out in C++ with no JavaScript runtime, where dragging a node
   rewrites the markdown source; a vendored MicroTeX with NewTX/XCharter faces
   for math; block virtualisation that loads a 561,693-word document in 40 ms
   and holds keystroke latency under 16 ms.
3. **What is deliberately absent.** Electron, a database, telemetry, mobile
   and sync. Each of those is a question the thread will otherwise ask.
4. **The licence and the money, stated up front.** MPL-2.0, the editor stays
   free, and a paid tier for reviewing agent edits is planned. Hacker News
   rewards this said plainly and punishes it discovered in a later comment.
5. **One line on how the demos were made.** The clips in `gallery.md` are the
   real application driven by the project's own test harness, which is
   credible in a way that a sentence about testing is not.

### 07:20 — the social post

One post from the project's Mastodon account and one from X, each linking the
Hacker News thread, with a short caption and the hero clip. Do not ask anyone
to upvote, in public or in private. Hacker News penalises voting rings, and the
penalty applies to the submission rather than to the account that asked.

### 07:00–11:00 — the four hours that matter

Ranking on the front page is decided early, and a thread with an absent author
stalls. Answer every substantive comment within this window.

- **File defects as issues while the thread is live**, then reply with the
  issue link. It converts a complaint into a record and shows the reporter
  they were heard.
- **Concede accurate criticism in one sentence** and say what will happen, or
  say plainly that it will not. The "no" list in `CONTRIBUTING.md` exists so
  the answer to mobile, sync, a plugin API, vim mode and real-time
  collaboration is already written and consistent.
- **Do not argue the framework.** Someone will say Qt is the wrong choice, or
  that Electron would have been fine. State once what the native stack buys,
  which is startup time, memory, and no bundled browser, then let it go.
- **Feature requests are not commitments.** "That is a reasonable request,
  please open a Discussion" is the whole answer.
- **Answer the monetisation question the same way every time**, matching the
  first comment. Any drift between answers is what a thread notices.

### 11:00 to end of day

The pace drops. Keep triaging, cut the response target to a few hours rather
than minutes, and start the day's record: point count over time, the top
objections, every issue filed, and the questions asked more than once. Those
questions are the README's next revision and the FAQ the website needs.

### Close-out, before sleeping

1. Run the metrics capture script. GitHub's traffic API retains 14 days, so a
   missed launch day cannot be recovered later.
2. Write down the point count, comment count, star count, and release-asset
   downloads per platform. These are the numbers the launch+2-weeks checkpoint
   compares against.
3. Decide whether the follow-on posts start tomorrow. If the thread went well,
   they compound; if it stalled, the same energy is better spent on the defects
   the thread surfaced.

## When something breaks

| What happens | What to do |
|---|---|
| A download is broken on one platform | Say so in the thread within minutes, name the platform, and link the issue. A visible fix in progress costs less than a silent broken link |
| A P0 (a crash or data loss) is confirmed against the release | Follow `docs/release-rollback.md`: mark the release superseded rather than deleting it, publish `v1.0.0+1` from the previous good tag plus the fix, let CI build it, and post the new version in the thread. Never re-tag, never mutate a published artifact |
| A claim in the README turns out to be wrong | Fix the README and push. It is a documentation commit, not a release, and correcting it in the open reads better than defending it |
| A security report arrives in public | Ask the reporter to move to `info@kvit.app` per `SECURITY.md`, thank them in the thread, and do not discuss specifics there |
| The thread does not take | Do not repost, do not ask anyone to vote. The repost window opens at roughly six months, and the intervening plan is the engineering deep-dive posts on a two-to-three week cadence, each submitted on its own |
| Someone objects to the open-core plan | The disclosure in the first comment is the defence. Repeat it, name what stays MPL-2.0, and do not improvise new commitments about the boundary |
| Traffic overwhelms the site | Cloudflare Pages absorbs this; the in-app update check calls GitHub directly and adds no load of ours. Nothing to do |

## What not to do

- **No changes to `main` beyond documentation and a hotfix for a defect the
  thread reported.** A day
  spent watching CI is a day not spent in the thread.
- **No renaming, no repositioning, no new claims** invented in a reply. Every
  claim in the thread should already exist in the README or the launch post.
- **No arguing with a downvoted comment.** It is already handled.
- **No second submission** if the first is flagged or dead; email the
  moderators instead.

## Decisions still open before a date can be set

- **Whether the launch blog post publishes before the thread or after.**
  Publishing first gives the first comment something to link; publishing after
  lets the thread supply material. Either way the website deploy comes first.
- **Whether a hero MP4 exists by then.** The fourteen gallery clips are GIFs
  sized for a README. The website and the social posts want one MP4, which is
  an assembly step over clips that already exist rather than new recording.
- **Whether the Windows artifact is signed.** The current README states plainly
  that it is not, and SmartScreen's "More info → Run anyway" is a documented
  friction rather than a blocker. Signing needs its identity validation queued
  a week or two ahead, so the choice has to be made before the stable build
  rather than on the day.
