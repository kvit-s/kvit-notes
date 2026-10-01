package links

import (
	"maps"
	"os"
	"path/filepath"
	"reflect"
	"slices"
	"strings"
	"testing"
)

// move does to the index and the table what Kvit Notes does when a note is
// renamed or moved, with its links updated or left alone.
func move(ix *Index, bodies map[string]string, from, to string, updateLinks bool) {
	bodies[to] = bodies[from]
	delete(bodies, from)
	ix.Remove(from)
	ix.Add(to)
	ix.Redirects.Retarget(from, to)
	if updateLinks {
		ix.Redirects.Record(from, to)
		ix.PruneRedirects(bodies)
	}
}

// rewritePass is the background rewrite after a rename: every note that links
// through the table is rewritten, and then the table keeps only what is still
// needed. It returns the links and notes it changed.
func rewritePass(ix *Index, bodies map[string]string) (links, notes int) {
	for _, p := range slices.Sorted(maps.Keys(bodies)) {
		if !ix.NeedsRewrite(bodies[p]) {
			continue
		}
		text, n := ix.RewriteRedirected(bodies[p])
		if n > 0 {
			bodies[p] = text
			links += n
			notes++
		}
	}
	ix.PruneRedirects(bodies)
	return links, notes
}

func newIndex(bodies map[string]string) *Index {
	ix := NewIndex(slices.Collect(maps.Keys(bodies)))
	ix.Redirects = &Redirects{}
	return ix
}

// Only the note part changes; spaces, heading and alias stay, and code and
// math are left alone.
func TestRewriteTargetsIsSurgical(t *testing.T) {
	cases := []struct {
		text, want string
		n          int
	}{
		{"a [[ Old ]] b [[old#H|x]] c [[Old.md]] d [[other]]\n",
			"a [[ New ]] b [[New#H|x]] c [[New]] d [[other]]\n", 3},
		{"🙂 [[old]] `[[old]]` $[[old]]$ $$[[old]]$$ [[ old #H | a ]]\n",
			"🙂 [[New]] `[[old]]` $[[old]]$ $$[[old]]$$ [[ New #H | a ]]\n", 2},
		{"[[/old]] [[x/old]] [[#old]]", "[[/old]] [[x/old]] [[#old]]", 0},
		// Bytes that are not UTF-8 stay as they were.
		{"\xff [[old]] \xfe", "\xff [[New]] \xfe", 1},
	}
	for _, c := range cases {
		got, n := RewriteTargets(c.text, map[string]bool{"old": true}, "New")
		if got != c.want || n != c.n {
			t.Errorf("%q: got %q, %d; want %q, %d", c.text, got, n, c.want, c.n)
		}
	}
	if got, n := RewriteTargets("[[old]]", nil, "New"); got != "[[old]]" || n != 0 {
		t.Errorf("no keys: %q, %d", got, n)
	}
}

// The rename counts the links it will change, the old name resolves through
// the table until the notes are rewritten, the rewrite keeps aliases and
// headings and leaves fences alone, and the table is gone once nothing needs
// it.
func TestRenameRewritesReferringLinks(t *testing.T) {
	const ref = "One [[Target]] and [[target#Head|alias]] here.\n```\n[[Target]] stays untouched in a fence\n```\n"
	bodies := map[string]string{
		"Target.md":    "content, plus a self link [[Target]]\n",
		"Ref.md":       ref,
		"Unrelated.md": "No links\n",
	}
	ix := newIndex(bodies)

	wantReferrers := []Referrer{{"Ref.md", []string{"target"}, 2}, {"Target.md", []string{"target"}, 1}}
	if got := ix.Referrers("Target.md", bodies); !reflect.DeepEqual(got, wantReferrers) {
		t.Errorf("Referrers = %+v", got)
	}

	move(ix, bodies, "Target.md", "Renamed.md", true)
	if bodies["Ref.md"] != ref {
		t.Error("the rename itself rewrote a note")
	}
	if got := ix.Redirects.TargetFor(NormalizeTarget("Target.md")); got != "Renamed.md" {
		t.Errorf("redirect leads to %q", got)
	}
	for _, target := range []string{"Target", "target#Head"} {
		r := ix.Resolution(target, true)
		if r.Path != "Renamed.md" || !r.Redirected {
			t.Errorf("%q resolved to %+v", target, r)
		}
	}

	links, notes := rewritePass(ix, bodies)
	if links != 3 || notes != 2 {
		t.Errorf("rewrote %d links in %d notes, want 3 in 2", links, notes)
	}
	want := map[string]string{
		"Renamed.md":   "content, plus a self link [[Renamed]]\n",
		"Ref.md":       "One [[Renamed]] and [[Renamed#Head|alias]] here.\n```\n[[Target]] stays untouched in a fence\n```\n",
		"Unrelated.md": "No links\n",
	}
	if !reflect.DeepEqual(bodies, want) {
		t.Errorf("after the rewrite: %q", bodies)
	}
	if ix.Redirects.Len() != 0 {
		t.Errorf("the table kept %+v", ix.Redirects.Entries())
	}
	root := t.TempDir()
	write(t, root, ".kvit/redirects.json", `{"redirects":[{"from":"Target.md","to":"Renamed.md"}],"version":1}`)
	if err := ix.Redirects.Save(root); err != nil {
		t.Fatal(err)
	}
	if _, err := os.Stat(filepath.Join(root, ".kvit", "redirects.json")); !os.IsNotExist(err) {
		t.Error("an empty table left its file")
	}
	if got := ix.Backlinks("Renamed.md", bodies); len(got) != 1 || got[0].Path != "Ref.md" {
		t.Errorf("Backlinks(Renamed.md) = %+v", got)
	}
}

// A rename that leaves links alone records nothing, so the old name names
// nothing; and the rewrite works on the text a note has when it gets there.
func TestRenameWithLinksLeftAlone(t *testing.T) {
	bodies := map[string]string{"Target.md": "target\n", "Ref.md": "before [[Target]] after\n"}
	ix := newIndex(bodies)
	if got := ix.Referrers("Target.md", bodies); len(got) != 1 || got[0].Count != 1 {
		t.Errorf("Referrers = %+v", got)
	}
	move(ix, bodies, "Target.md", "First.md", false)
	if got := ix.Resolve("Target"); got != "" || ix.Redirects.Len() != 0 {
		t.Errorf("Target resolved to %q with table %+v", got, ix.Redirects.Entries())
	}
	if links, _ := rewritePass(ix, bodies); links != 0 || bodies["Ref.md"] != "before [[Target]] after\n" {
		t.Errorf("rewrote %d links: %q", links, bodies["Ref.md"])
	}

	bodies["Ref.md"] = "before [[First]] after\n"
	move(ix, bodies, "First.md", "Second.md", true)
	bodies["Ref.md"] = "newer external text with [[First]]\n"
	rewritePass(ix, bodies)
	if got := bodies["Ref.md"]; got != "newer external text with [[Second]]\n" {
		t.Errorf("Ref.md = %q", got)
	}
}

// A folder rename records one redirect per note, and only links written
// with the folder's path change.
func TestFolderRenameRewritesQualifiedLinksOnly(t *testing.T) {
	bodies := map[string]string{
		"Ideas/Target.md": "target\n",
		"Ref.md":          "bare [[Target]] qualified [[Ideas/Target#H|alias]]\n`[[Ideas/Target]]` $[[Ideas/Target]]$\n",
	}
	ix := newIndex(bodies)
	if got := FolderReferrers("Ideas", bodies); !reflect.DeepEqual(got, []Referrer{{Path: "Ref.md", Count: 1}}) {
		t.Errorf("FolderReferrers = %+v", got)
	}

	for _, p := range slices.Sorted(maps.Keys(bodies)) {
		if rest, ok := strings.CutPrefix(p, "Ideas/"); ok {
			bodies["Thoughts/"+rest] = bodies[p]
			delete(bodies, p)
			ix.Remove(p)
			ix.Add("Thoughts/" + rest)
			ix.Redirects.Retarget(p, "Thoughts/"+rest)
		}
	}
	if !ix.Redirects.RecordFolder("Ideas", "Thoughts", slices.Collect(maps.Keys(bodies))) {
		t.Fatal("the folder rename recorded nothing")
	}
	ix.PruneRedirects(bodies)
	if got := ix.Redirects.TargetFor(NormalizeTarget("Ideas/Target.md")); got != "Thoughts/Target.md" {
		t.Errorf("redirect leads to %q", got)
	}
	if got := ix.Resolve("Ideas/Target"); got != "Thoughts/Target.md" {
		t.Errorf("Ideas/Target resolved to %q", got)
	}

	rewritePass(ix, bodies)
	want := "bare [[Target]] qualified [[Thoughts/Target#H|alias]]\n`[[Ideas/Target]]` $[[Ideas/Target]]$\n"
	if got := bodies["Ref.md"]; got != want {
		t.Errorf("Ref.md = %q", got)
	}
	if got := ix.Resolve("Target"); got != "Thoughts/Target.md" {
		t.Errorf("Target resolved to %q", got)
	}
}

func TestRedirectChainCollapsesToOneHop(t *testing.T) {
	bodies := map[string]string{"A.md": "the note\n", "Ref.md": "see [[A]]\n"}
	ix := newIndex(bodies)
	move(ix, bodies, "A.md", "B.md", true)
	move(ix, bodies, "B.md", "C.md", true)
	if got := ix.Redirects.Entries(); !reflect.DeepEqual(got, []Redirect{{"A.md", "C.md"}}) {
		t.Errorf("table = %+v", got)
	}
	if got := ix.Resolve("A"); got != "C.md" {
		t.Errorf("A resolved to %q", got)
	}
	rewritePass(ix, bodies)
	if bodies["Ref.md"] != "see [[C]]\n" || ix.Redirects.Len() != 0 {
		t.Errorf("Ref.md = %q, table %+v", bodies["Ref.md"], ix.Redirects.Entries())
	}
}

func TestANoteCreatedAtARedirectedPathWins(t *testing.T) {
	bodies := map[string]string{"A.md": "the note\n", "Ref.md": "see [[A]]\n"}
	ix := newIndex(bodies)
	move(ix, bodies, "A.md", "B.md", true)
	if got := ix.Resolve("A"); got != "B.md" {
		t.Fatalf("A resolved to %q", got)
	}
	bodies["A.md"] = ""
	if !ix.Add("A.md") {
		t.Error("Add did not report the dropped redirect")
	}
	if got := ix.Resolve("A"); got != "A.md" || ix.Redirects.Len() != 0 {
		t.Errorf("A resolved to %q with table %+v", got, ix.Redirects.Entries())
	}
	if links, _ := rewritePass(ix, bodies); links != 0 || bodies["Ref.md"] != "see [[A]]\n" {
		t.Errorf("rewrote %d links: %q", links, bodies["Ref.md"])
	}
}

// A table saved before the rewrite finished makes the links resolve at the
// next open, and the rewrite runs then; the open note's text is rewritten in
// memory.
func TestTheTableOutlivesAnInterruptedRewrite(t *testing.T) {
	root := t.TempDir()
	bodies := map[string]string{"Target.md": "the target\n", "Ref.md": "see [[Target]]\n"}
	ix := newIndex(bodies)
	move(ix, bodies, "Target.md", "Renamed.md", true)
	if err := ix.Redirects.Save(root); err != nil {
		t.Fatal(err)
	}

	text, n := ix.RewriteRedirected("see [[Target]] and unsaved edits\n")
	if text != "see [[Renamed]] and unsaved edits\n" || n != 1 {
		t.Errorf("open note: %q, %d", text, n)
	}

	reopened := NewIndex(slices.Collect(maps.Keys(bodies)))
	reopened.Redirects = LoadRedirects(root)
	if got := reopened.Resolve("Target"); got != "Renamed.md" {
		t.Errorf("after reopening Target resolved to %q", got)
	}
	rewritePass(reopened, bodies)
	if err := reopened.Redirects.Save(root); err != nil {
		t.Fatal(err)
	}
	if bodies["Ref.md"] != "see [[Renamed]]\n" {
		t.Errorf("Ref.md = %q", bodies["Ref.md"])
	}
	if _, err := os.Stat(filepath.Join(root, ".kvit", "redirects.json")); !os.IsNotExist(err) {
		t.Error("the finished rewrite left the table's file")
	}

	// A table left by an older version's interrupted rewrite, naming a note
	// that was never there under the old name.
	ix = NewIndex([]string{"Target.md", "Referrer.md"})
	ix.Redirects = &Redirects{}
	ix.Redirects.Record("Old Name.md", "Target.md")
	if got := ix.Resolve("Old Name"); got != "Target.md" {
		t.Errorf("Old Name resolved to %q", got)
	}
	if text, _ := ix.RewriteRedirected("see [[Old Name]]\n"); text != "see [[Target]]\n" {
		t.Errorf("rewrote to %q", text)
	}
}

// A link written with a path keeps a path; a bare name becomes the new
// title only when that alone names the note; a leading "/" stays; and a
// link that resolves without the table is left as it is.
func TestReplacementKeepsTheWayALinkWasWritten(t *testing.T) {
	bodies := map[string]string{
		"Welcome.md": "",
		"A.md":       "",
		"old/Q.md":   "",
		"Ref.md":     "[[A]] [[/A|a]] [[a.md#H]] [[old/Q]] [[/old/Q#x|y]] [[Q]] [[Welcome]]",
	}
	ix := newIndex(bodies)
	move(ix, bodies, "A.md", "x/Welcome.md", true)
	move(ix, bodies, "old/Q.md", "new/Q.md", true)
	text, n := ix.RewriteRedirected(bodies["Ref.md"])
	want := "[[x/Welcome]] [[/x/Welcome|a]] [[x/Welcome#H]] [[new/Q]] [[/new/Q#x|y]] [[Q]] [[Welcome]]"
	if text != want || n != 5 {
		t.Errorf("got %q, %d\nwant %q, 5", text, n, want)
	}
}

func write(t *testing.T, root, rel, text string) {
	t.Helper()
	p := filepath.Join(root, filepath.FromSlash(rel))
	if err := os.MkdirAll(filepath.Dir(p), 0o755); err != nil {
		t.Fatal(err)
	}
	if err := os.WriteFile(p, []byte(text), 0o644); err != nil {
		t.Fatal(err)
	}
}
