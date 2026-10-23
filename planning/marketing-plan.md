# Kvit Notes: launch marketing materials

*Written 2026-08-05, while `v1.0.0-rc1` is published as a prerelease and the
stable release has not been cut. This document plans the materials the launch
needs: what each one is, who it is for, what has to exist before it can be
made, and the order to make them in. It lives in `planning/`, which is
gitignored, because it is working material rather than something a user of the
application should read.*

## Where this sits

Three documents already govern the launch, all of them in the private
`kvit-notes-pro` tree rather than here:

- `to-market.md` is the strategy: why the editor is open source, who the
  audience is, what the paid tier will be, and what results to expect.
- `launch-plan.md` is the execution plan, phases A to H, of which phases F
  (website and presentation) and G (promotion) are the ones this document
  serves.
- `m2-owner.md` lists the steps only the owner can take, several of which are
  prerequisites here.

This document is narrower than any of them. It covers the artifacts a reader
or viewer actually encounters: the demo video, the still captures, the README,
the website, the launch blog post, and the copy for each channel. Decisions
already ruled elsewhere are restated here only where a material depends on
them.

## The audience and the claim

The launch aims at developers and technical writers who keep notes in
markdown files and who care that those files stay theirs. On Hacker News that
audience responds to the local-first frame roughly twice as strongly as to the
AI frame, measured across thirteen months of front-page stories in
`to-market.md` §6, which is why the headline claim is:

> A native markdown block editor. Your notes stay plain `.md` files on disk,
> shown fully rendered, with Mermaid diagrams and TeX math drawn by the
> application rather than a bundled browser.

The agent story is the second paragraph rather than the headline, and the
paid tier is disclosed rather than hinted at, because a reader who discovers
a commercial plan later reacts worse than one who is told up front.

The single demonstration nothing else combines is dragging a node in a
rendered Mermaid diagram and watching the markdown source rewrite itself.
Four separate diagram-tooling stories reached the front page in the measured
window, all circling this interaction without offering it inside a markdown
editor. Every asset below leads with it.

## What exists and what is missing

| Material | State |
|---|---|
| README with hero stills | Done. Four captures under `screenshots/press/`, regenerated from the checked-in demo vault, with an OCR leak gate in CI. |
| Published release with artifacts | `v1.0.0-rc1` is live as a prerelease. Stable is not cut. |
| Demo video (the paced tour) | **Missing.** The largest gap and the subject of the next section. |
| Test-suite video | **Missing.** A second, supporting asset for the technical reader; see the section after it. |
| Short feature loops for the README | **Missing.** Five were scoped: Mermaid drag, live-preview reveal, character-diagram repair, math typing, live query update. |
| Website at kvit.app | Page written and staged in the project record; the domain is owned and on Cloudflare; not deployed. |
| Launch blog post | **Missing.** Discussed below. |
| Show HN title and first comment | **Missing.** Drafted last, once the video exists, since the post describes it. |
| Social accounts | Not registered. A Mastodon account on fosstodon.org, and the existing X handle wants renaming from its auto-generated form. |
| Deep-dive posts | Four topics identified, none written. These are post-launch. |

## The demo video

### Why the test harness is the right camera

The application already has a UI driver, `tools/uidriver.cpp`, built with
`-DKVIT_UI_DRIVER=ON`, which launches the real shell against a nominated vault
and drives it through a named scenario. It is what produced the four README
stills, through `tools/capture-press-stills.sh` and a checked-in demo vault at
`screenshots/demo-vault/`. Recording the demo the same way has three
advantages over screen-recording a hand-driven session: the result is
reproducible, it can be re-recorded on every release rather than aging out,
and the line "this was recorded by our test harness driving the real
application" is itself worth saying in the launch post.

### What has to change first

Capturing the integration suite as it stands would not produce a watchable
video. The suite is paced for a machine: it acts with no dwell time, moves no
visible pointer, and runs in an order set by test dependencies rather than by
what a viewer needs to see next. The work is therefore a new `tour` scenario
in the UI driver that drives the same real interface with three additions:

1. **Human pacing.** A dwell after each action long enough to read the result,
   roughly 600 to 1,200 ms, tuned by watching it rather than by picking a
   number.
2. **A visible pointer.** The driver synthesises events without moving the
   cursor, so a viewer sees results with no visible cause. Because capture on
   Windows records the real pointer, the tour scenarios move it with
   `QCursor::setPos` along a path to each target rather than drawing a
   synthetic one. See `windows-video-plan.md`.
3. **A scripted order** that tells a story, rather than the order a test
   suite happens to need.

### Record on Windows

Recording from the Windows side rather than from WSL is the better choice, and
it removes the constraint that otherwise dominates this work.

The glyph corruption that makes WSL captures risky comes from Mesa's `d3d12`
Gallium driver inside WSL, not from anything Qt does: white text renders
yellow, some glyphs lose their alpha so the window goes partly transparent,
and dark text renders as pale outlines, while rectangles and images stay
correct. A recording made on that path can look structurally fine with every
letter in it wrong. On real Windows the application draws through the actual
GPU and none of that applies; the `GALLIUM_DRIVER=llvmpipe` pin in
`KvitApplication::applyPlatformWorkarounds()` only fires under WSL, so it is
inert there.

Two further reasons to prefer it. Windows is the largest desktop share, so the
recording shows what most viewers will see on their own machine, including
DirectWrite text shaping rather than the Linux font stack. And the capture
tooling is simpler: `ffmpeg` with `ddagrab`, or OBS, records a chosen window
region without a compositor in the way.

What it needs: the driver has no platform gate, so a Windows build with
`-DKVIT_UI_DRIVER=ON` added to the `windows-msvc-release` preset produces
`kvit-uidriver.exe`. That has not been built on Windows yet, so budget an hour
for first-build surprises.

If a recording is ever made from WSL after all, confirm with `QSG_INFO=1` that
the `RENDERER:` line names `llvmpipe` before recording and again before
publishing.

### Other constraints

- **No identifying paths in frame.** The status bar renders the open note's
  absolute path, so the capture must stage its vault somewhere neutral such as
  `/tmp/kvit-notes-demo/vault`. The OCR leak gate in CI reads shipped raster
  images and would catch this in a still; it does not look inside a video, so
  the capture script has to enforce the staged path itself.
- **A fixed window size,** chosen once so every asset crops consistently.
  1600×1000 gives a 16:10 frame that downsamples cleanly for both the site and
  social cards.

### One clip per feature, with the feature named on screen

Record each feature as its own clip rather than shooting one continuous take.
Three things follow from that, and together they are worth more than the
convenience of a single recording:

- A feature that comes out badly is re-shot on its own, without redoing the
  set.
- The per-feature clips are the README loops and the social posts. There is no
  separate cutting pass.
- The full tour is the clips concatenated, so its order can change without
  re-recording anything.

The driver already selects behaviour by name through `--scenario=`, so each
feature becomes its own scenario: `tour-mermaid`, `tour-livepreview`,
`tour-math`, `tour-repair`, `tour-query`. A `--title=` argument supplies the
caption text, and the driver draws it into its own window rather than into the
shipped interface, by parenting a small overlay item to the window's content
item at startup. Keeping the caption inside the capture rather than adding it
in an editor means it is locked to the frame, it re-renders identically on
every release, and no post-production step can forget it.

Place the caption as a lower third: a band across the bottom of the frame,
clear of the toolbar and the editing area, holding the feature name and a
one-line description of what is about to happen. Hold it for the first two
seconds of the clip, then fade it, since a viewer who has read it does not
need it competing with the interaction.

The shipped interface must not learn about any of this. The overlay belongs to
the driver, which is off by default and is not part of a release build.

### Shot list

Around 75 seconds when concatenated, in this order, each clip leading into the
next:

1. **The Mermaid drag** (0:00–0:15). Open a note with a rendered flowchart,
   drag a node, and show the markdown source rewriting itself as it moves.
   This is the strongest fifteen seconds and belongs first, before a viewer
   decides whether to keep watching.
2. **Live-preview editing** (0:15–0:30). Type into a paragraph and show
   syntax revealing itself around the caret and hiding again when the caret
   leaves.
3. **Math** (0:30–0:45). Type an equation and show it rendering inline at the
   text baseline, then a display equation.
4. **Character-diagram repair** (0:45–0:60). Paste crooked box-drawing art of
   the kind a language model emits, watch it straighten, then copy a rendered
   Mermaid diagram back out as text. The pair is the demonstration no
   competitor can currently show.
5. **The live query block** (0:60–0:75). A table built from front matter
   across the vault, updating when a note changes underneath it.

Each shot is also cut as a standalone loop of five to ten seconds for the
README and for social posts.

### How the frames are captured

Two routes, and the cheap one is right for this launch.

**Screen-record the driver's window from outside it,** with `ffmpeg` using
`ddagrab` on Windows, or OBS. The driver runs at ordinary speed on a real
desktop and something else records the region. Animations, fades and the
diagram drag all record smoothly because they run in real time. This is an
afternoon of work and needs no new capture code.

**Grab a frame at a time from inside the driver** and encode the sequence
afterwards. The driver already writes PNG frames through
`QQuickWindow::grabWindow()` for the still captures. Extending that to a
sequence would produce byte-identical output on every release with no dropped
frames and no compositor artifacts. The obstacle is animation: Qt advances
animations against the wall clock, so stepping logically through frames
freezes or skips them unless the driver installs its own animation driver to
advance animation time in step with the grabs. That is a known Qt technique
rather than an experiment, and it is more work than the launch needs.

Start with external recording. Note the frame-grab route as the version worth
building if re-recording every release becomes tedious, since that is exactly
the point at which reproducibility repays its cost.

### Outputs

- One MP4, H.264, for the website and for the X and Mastodon posts.
- Five short GIFs for the README, each kept small enough not to stall a phone;
  under about 5 MB each is the practical ceiling.
- A poster frame for the video and one social card at 1200×630.

### Where the work goes

The `tour` scenario and its capture script are ordinary repository code and
belong in the public tree beside the existing still harness. Only the finished
video files and the draft copy stay in `planning/`, since a repository is a
poor place to version binary media.

## The second video: the test suite driving the application

A separate, unpaced recording of the integration suite running against the
real interface. Where the tour explains the product to someone deciding
whether to download it, this one is for the technical reader who is already
interested and wants to know whether the thing is built seriously. Developers
enjoy watching a suite drive a graphical application at speed, and 351 cases
exercising a real window is evidence of coverage that no sentence about
testing conveys as quickly.

**It is a supporting asset, not the lead.** It belongs in the launch blog post
and as a follow-on social post a day or two after the announcement. Keeping it
off the README and off the website's first screen matters: a viewer who has
not yet been told what the application is will read rapid automated flailing
as instability rather than as coverage.

### Which run to record

Record `IntegrationTests`, the single-process entry: one window, 351 cases,
about two and a half minutes of continuous activity. Do not record
`IntegrationTestsIsolated`, which starts one process and one window per case
and takes eight minutes; it is the entry that decides whether the tree is
good, and it looks like several hundred windows opening and closing.

Two properties of that choice are worth knowing before recording:

- **Text renders correctly with no special handling.** The entry sets
  `QT_QUICK_BACKEND=software`, so rendering happens on the CPU and is
  pixel-correct. The glyph corruption that constrains the tour comes from the
  GPU path, which this run does not use.
- **Roughly twenty of its cases fail on residue** accumulated over the
  preceding few hundred, and pass when run alone. Those failures are invisible
  on screen, because a failed assertion prints to the console rather than
  showing in the window, so the recording is unaffected. What it means is that
  the captions must name the case being exercised without claiming a result,
  and any pass count quoted in the post has to cite the isolated gate, where
  all 351 pass in about eight minutes, rather than the run being watched.

### Treatment

- Real time or at most 2x. The suite is already fast, and speeding it further
  turns it into noise.
- Overlay the name of the case being exercised. The suite's names read as
  English (`test_wiki2_backlinksPanelListsAndUpdatesLive`), which is what makes
  the video legible rather than a blur of motion.
- A caption at the top stating what is being watched: the real application,
  driven by the same suite that gates every commit.
- Confirm no absolute home path appears in the status bar before publishing.
  The suite uses temporary vaults, so this should hold, and it needs checking
  rather than assuming, because the leak gate in CI reads shipped still images
  and does not look inside a video.
- Cut a 20 to 30 second excerpt for social platforms alongside the full run.

## The launch blog post, and what to link from Hacker News

The user question behind this section is whether the Show HN should link a
blog post rather than the repository. My recommendation is to keep the
repository as the Show HN link, and to write the blog post anyway, because it
has three other jobs.

Show HN is specifically for work a reader can try, and the posts in the
calibration set that did best there linked their repositories: Files.md at 730
points and OpenKnowledge at 381 both linked source. A post that leads with an
essay invites the first comment to ask where the code is. The README already
works as the landing page: it opens with the claim, the hero capture, and a
download table, which is what a reader arriving from the thread wants in the
first ten seconds.

The blog post then does the work the repository page cannot:

1. **It is the first comment's link.** The prepared first comment explains why
   the project exists and what is technically interesting; a link there to a
   longer piece converts the curious without cluttering the README.
2. **It is the lead for the other venues.** Lobsters, r/opensource and the Qt
   community all take an article better than a repository link, and each has
   its own self-promotion rules that a written piece satisfies more gracefully.
3. **It is the seed of the content calendar.** The four deep-dive topics are
   the follow-on posts; the launch post is the piece that establishes the voice
   and the site they are published on.

The post itself should be a build story rather than a feature list: why a
markdown editor written in C++ and Qt in 2026, what "files as the source of
truth" costs once you refuse to keep a database (a byte-faithful round trip
where saving rewrites nothing the user did not edit, and a file watcher that
knows its own writes), and the one interaction that came out of it. Around 1,200 to 1,800 words with the demo video embedded near the top.

**This creates a dependency worth noting:** the blog post needs somewhere to
live, which makes deploying the website a prerequisite rather than a
nice-to-have. The site is written and staged already, so this is a deployment
step rather than a writing one.

## Channel materials

| Channel | What it needs |
|---|---|
| Show HN | Title, repository link, prepared first comment posted immediately, and a cleared day to answer replies. |
| README | Hero GIF above the download table; the four stills already in place below it. |
| Website | The video, the feature sections, an OS-detecting download button, and the blog post at a stable path. |
| Lobsters | The blog post, tagged `show`. Requires an invitation. |
| r/opensource, r/selfhosted | The local-first angle, linking the post. |
| r/ObsidianMD | Conversational framing, asking what a user would miss, rather than an announcement. This community removes drive-by promotion. |
| Qt community | The blog post, framed as a large real-world QML application. |
| Mastodon and X | The MP4 with a two-sentence caption, posted when the HN thread goes up. |
| Directory listings | Description, screenshots, category tags. Submitted the week after launch. |

## Order of work

1. Deploy the website, since two later items depend on it.
2. Build the `tour` scenario and the capture script; record the video and the
   five loops. This is the long pole and the one that most changes how the
   launch reads.
3. Add the hero GIF to the README and the video to the site.
4. Record the test-suite video, which needs no new code and can be done in an
   hour once a green run is available.
5. Write the launch blog post around both videos.
6. Draft the Show HN title and first comment last, once the assets they
   describe exist.
7. Register the Mastodon account and rename the X handle, which can happen at
   any point before the post.

## Open questions

- **Where the blog lives.** A path on kvit.app is the assumption here. The
  alternative, publishing to a hosted platform, gets discovery for free and
  gives up control of the page.
- **Whether the launch post ships with the stable release or a few days
  after.** Publishing it first gives the HN thread something to link that is
  not the repository; publishing after means the thread itself supplies
  material for the post.
- **How much of the tour to narrate.** Silent video with captions travels
  further on social platforms, where most playback is muted, while a narrated
  version suits the website.
