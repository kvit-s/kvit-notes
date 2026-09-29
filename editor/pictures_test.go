package editor

import (
	"bytes"
	"image"
	"image/png"
	"os"
	"path/filepath"
	"testing"
	"time"
)

// Where a document's pictures are found, after the Qt core's
// ImageAssets::resolveSource, and where a pasted one is written, after its
// AssetStore.

func TestAPictureIsFoundBesideTheDocumentThenFromTheRootThenTheSite(t *testing.T) {
	root := t.TempDir()
	base := filepath.Join(root, "notes")
	site := filepath.Join(root, "static")
	writePicture(t, filepath.Join(base, "a.png"))
	writePicture(t, filepath.Join(root, "a.png"))
	writePicture(t, filepath.Join(root, "assets", "b.png"))
	writePicture(t, filepath.Join(site, "img", "c.png"))
	f := PictureFolders{Base: base, Root: root, Site: site}
	for stored, want := range map[string]string{
		"a.png":                          filepath.Join(base, "a.png"),
		"assets/b.png":                   filepath.Join(root, "assets", "b.png"),
		"/img/c.png":                     filepath.Join(site, "img", "c.png"),
		filepath.Join(root, "a.png"):     filepath.Join(root, "a.png"),
		"missing.png":                    "",
		"https://example.com/p.png":      "https://example.com/p.png",
		"//img/c.png":                    "",
		"":                               "",
		filepath.ToSlash("assets/b.png"): filepath.Join(root, "assets", "b.png"),
	} {
		if got := f.Resolve(stored); got != want {
			t.Errorf("%q resolved to %q, want %q", stored, got, want)
		}
	}
	// With no site folder, "/" paths are looked up in the root.
	if got := (PictureFolders{Root: root}).Resolve("/assets/b.png"); got != filepath.Join(root, "assets", "b.png") {
		t.Errorf("a site path with no site folder resolved to %q", got)
	}
}

func pngBytes(t *testing.T, w, h int) []byte {
	t.Helper()
	var b bytes.Buffer
	if err := png.Encode(&b, image.NewNRGBA(image.Rect(0, 0, w, h))); err != nil {
		t.Fatal(err)
	}
	return b.Bytes()
}

func TestAPastedPictureIsStoredUnderANameOfItsOwn(t *testing.T) {
	dir := t.TempDir()
	at := time.Date(2026, 9, 21, 8, 15, 0, 0, time.Local)
	first, err := StorePicture(dir, "assets", "message", pngBytes(t, 8, 8), at)
	if err != nil || first != "assets/message-20260921-081500.png" {
		t.Fatalf("stored as %q (%v)", first, err)
	}
	second, err := StorePicture(dir, "assets", "message", pngBytes(t, 8, 8), at)
	if err != nil || second != "assets/message-20260921-081500-1.png" {
		t.Errorf("the second was stored as %q (%v)", second, err)
	}
	data, err := os.ReadFile(filepath.Join(dir, filepath.FromSlash(first)))
	if err != nil {
		t.Fatal(err)
	}
	if _, err := png.Decode(bytes.NewReader(data)); err != nil {
		t.Errorf("the stored file is not a PNG: %v", err)
	}
	if _, err := StorePicture(dir, "assets", "message", []byte("not a picture"), at); err == nil {
		t.Error("bytes that are no picture were stored")
	}
	if got, err := StorePicture(dir, "", "Été & co", pngBytes(t, 2, 2), at); err != nil || got != "assets/Été - co-20260921-081500.png" {
		t.Errorf("a name with other characters was stored as %q (%v)", got, err)
	}
	if _, ok := SavePastedPicture("", "assets", "message"); ok {
		t.Error("a picture was pasted with nowhere to put it")
	}
}

func TestTheLoaderReadsAPictureOnceAndAgainWhenItChanges(t *testing.T) {
	dir := t.TempDir()
	path := filepath.Join(dir, "wide.png")
	if err := os.WriteFile(path, pngBytes(t, 3000, 10), 0o644); err != nil {
		t.Fatal(err)
	}
	load := PictureFolders{Base: dir}.Loader()
	first, err := load("wide.png")
	if err != nil {
		t.Fatal(err)
	}
	if w := first.LogicalSize().Width; w != maxPictureWidth {
		t.Errorf("a picture 3000 wide is kept %v wide", w)
	}
	again, _ := load("wide.png")
	if again != first {
		t.Error("an unchanged picture was read again")
	}
	if err := os.WriteFile(path, pngBytes(t, 20, 10), 0o644); err != nil {
		t.Fatal(err)
	}
	changed, err := load("wide.png")
	if err != nil || changed.LogicalSize().Width != 20 {
		t.Errorf("a changed picture was not read again (%v)", err)
	}
	if _, err := load("https://example.com/p.png"); err == nil {
		t.Error("a web address was loaded as a file")
	}
	if _, err := load("missing.png"); err == nil {
		t.Error("a missing picture loaded")
	}
}
