package editor

// Pictures in a document drawn away from a vault: where a picture a document
// names is found, and where a picture pasted into a message box is written.
// The editor itself only asks Editor.LoadImage for a picture by the path the
// document writes and Editor.PasteImage for a pasted one; these are what a
// program without a vault of its own gives it (the documentDirectory,
// assetRoot, siteRoot and assetSink on BlockEditorSurface).

import (
	"bytes"
	"errors"
	"fmt"
	"image"
	_ "image/gif" // pictures a document names may be GIFs
	_ "image/jpeg"
	"image/png"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"time"

	"github.com/kvit-s/kvit-ui/platform"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison"
	"golang.org/x/image/draw"
)

// PictureFolders are the folders a document's pictures are looked up in.
type PictureFolders struct {
	// Base is the folder a relative path is written against: the folder
	// the document is in, or for a transcript the session folder (
	// baseDir, documentDirectory).
	Base string
	// Root is the folder a path is also looked up in when it is not beside
	// the document: a note in a subfolder names assets/a.png from the
	// project's top ( assetRoot). For a file from a track's copy it is
	// that copy, so the pictures drawn are the copy's own.
	Root string
	// Site is the folder a path starting with "/" is looked up in, as a
	// website names a file at its root: static/ in a Hugo site (
	// siteRoot). Root when empty.
	Site string
}

// Resolve is the file a stored picture path names, by the core's rule
// (ImageAssets::resolveSource): beside the document, then from the top of
// Root, then as written, and a path starting with "/" from the site folder
// last, the first that is a file. A web address comes back as it is, and a
// path that names no file comes back "".
func (f PictureFolders) Resolve(stored string) string {
	if stored == "" {
		return ""
	}
	if remotePicture(stored) {
		return stored
	}
	native := filepath.FromSlash(stored)
	var candidates []string
	against := func(dir string) {
		if dir == "" {
			return
		}
		if filepath.IsAbs(native) {
			candidates = append(candidates, native)
			return
		}
		candidates = append(candidates, filepath.Join(dir, native))
	}
	against(f.Base)
	against(f.Root)
	if abs, err := filepath.Abs(native); err == nil {
		candidates = append(candidates, abs)
	}
	site := f.Site
	if site == "" {
		site = f.Root
	}
	if rel, ok := strings.CutPrefix(stored, "/"); ok && rel != "" && !strings.HasPrefix(rel, "/") && site != "" {
		candidates = append(candidates, filepath.Join(site, filepath.FromSlash(rel)))
	}
	for _, c := range candidates {
		if info, err := os.Stat(c); err == nil && info.Mode().IsRegular() {
			return filepath.Clean(c)
		}
	}
	return ""
}

// remotePicture reports whether a stored path is a web address or a data
// address rather than a file.
func remotePicture(stored string) bool {
	lower := strings.ToLower(strings.TrimSpace(stored))
	return strings.HasPrefix(lower, "http://") || strings.HasPrefix(lower, "https://") || strings.HasPrefix(lower, "data:")
}

// Loader is what Editor.LoadImage takes for these folders: each picture
// found by Resolve, read once and kept while the file is unchanged, and a
// picture wider than the text column is ever drawn scaled down as it is
// read. A web address is refused; the editor draws it as a card.
func (f PictureFolders) Loader() func(stored string) (unison.Drawable, error) {
	type kept struct {
		img      unison.Drawable
		size     int64
		modified time.Time
	}
	var mu sync.Mutex
	cache := map[string]kept{}
	return func(stored string) (unison.Drawable, error) {
		if remotePicture(stored) {
			return nil, errors.New("not a file: " + stored)
		}
		path := f.Resolve(stored)
		if path == "" {
			return nil, errors.New("no such picture")
		}
		info, err := os.Stat(path)
		if err != nil {
			return nil, err
		}
		if info.Size() > maxPictureBytes {
			return nil, errors.New("picture too large: " + stored)
		}
		mu.Lock()
		k, ok := cache[path]
		mu.Unlock()
		if ok && k.size == info.Size() && k.modified.Equal(info.ModTime()) {
			return k.img, nil
		}
		data, err := os.ReadFile(path)
		if err != nil {
			return nil, err
		}
		img, err := decodePicture(data, filepath.Ext(path))
		if err != nil {
			return nil, err
		}
		mu.Lock()
		cache[path] = kept{img, info.Size(), info.ModTime()}
		mu.Unlock()
		return img, nil
	}
}

// The largest picture file read, and the widest a picture is kept at: the
// text column with room for a high-density screen.
const (
	maxPictureBytes = 64 << 20
	maxPictureWidth = 1400
)

// decodePicture turns picture bytes into something to draw, scaled down to
// maxPictureWidth.
func decodePicture(data []byte, ext string) (unison.Drawable, error) {
	if strings.EqualFold(ext, ".svg") {
		svg, err := unison.NewSVGFromReader(bytes.NewReader(data))
		if err != nil {
			return nil, err
		}
		return svgPicture{svg}, nil
	}
	img, _, err := image.Decode(bytes.NewReader(data))
	if err != nil {
		// A format the standard library does not read, such as WebP.
		return unison.NewImageFromBytes(data, geom.NewPoint(1, 1))
	}
	b := img.Bounds()
	if b.Dx() > maxPictureWidth {
		h := max(1, b.Dy()*maxPictureWidth/b.Dx())
		dst := image.NewNRGBA(image.Rect(0, 0, maxPictureWidth, h))
		draw.ApproxBiLinear.Scale(dst, dst.Bounds(), img, b, draw.Over, nil)
		img = dst
	}
	var out bytes.Buffer
	if err := png.Encode(&out, img); err != nil {
		return nil, err
	}
	return unison.NewImageFromBytes(out.Bytes(), geom.NewPoint(1, 1))
}

// svgPicture draws an SVG as a picture of its own size.
type svgPicture struct{ *unison.SVG }

func (s svgPicture) LogicalSize() geom.Size { return s.Size() }

// SavePastedPicture writes the picture on the clipboard into dir, as the
// AssetStore does for an editor given an asset sink: into dir/folder as
// "<slug>-<yyyyMMdd-HHmmss>.png", with "-1", "-2" and so on added when that
// name is taken, and answers the path from dir, which is what the image
// block names. It reports false when the clipboard holds no picture or dir
// is "", which leaves a paste to take the clipboard's text instead. Kvit
// Works' message box passes its project's .kvit/pasted folder, "assets"
// and "message".
//
// Under WSL a picture copied in Windows is not on unison's clipboard, so
// Windows' clipboard is asked last. Found there, the paste is done before
// the text is read, which is also the read that crashes WSLg's compositor
// while it is still fetching the picture from Windows.
func SavePastedPicture(dir, folder, slug string) (string, bool) {
	if dir == "" {
		return "", false
	}
	for _, dt := range []*uti.DataType{uti.PNG, uti.JPEG, uti.GIF, uti.WEBP, uti.BMP, uti.TIFF, uti.Image} {
		if !unison.ClipboardHasDataType(dt) {
			continue
		}
		data := unison.ClipboardGetData(dt)
		if len(data) == 0 {
			continue
		}
		if stored, err := StorePicture(dir, folder, slug, data, time.Now()); err == nil {
			return stored, true
		}
	}
	if data, ok := platform.WindowsClipboardPicture(); ok {
		if stored, err := StorePicture(dir, folder, slug, data, time.Now()); err == nil {
			return stored, true
		}
	}
	return "", false
}

// StorePicture writes picture bytes into dir/folder as SavePastedPicture
// names them, re-encoded as PNG, and answers the path from dir.
func StorePicture(dir, folder, slug string, data []byte, at time.Time) (string, error) {
	img, _, err := image.Decode(bytes.NewReader(data))
	if err != nil {
		return "", err
	}
	if folder == "" {
		folder = "assets"
	}
	target := filepath.Join(dir, filepath.FromSlash(folder))
	if err := os.MkdirAll(target, 0o755); err != nil {
		return "", err
	}
	base := assetSegment(slug, "image") + "-" + at.Format("20060102-150405")
	name := base + ".png"
	for n := 1; ; n++ {
		if _, err := os.Stat(filepath.Join(target, name)); errors.Is(err, os.ErrNotExist) {
			break
		}
		name = fmt.Sprintf("%s-%d.png", base, n)
	}
	var out bytes.Buffer
	if err := png.Encode(&out, img); err != nil {
		return "", err
	}
	abs := filepath.Join(target, name)
	if err := os.WriteFile(abs, out.Bytes(), 0o644); err != nil {
		return "", err
	}
	rel, err := filepath.Rel(dir, abs)
	if err != nil {
		return filepath.ToSlash(abs), nil
	}
	return filepath.ToSlash(rel), nil
}

// assetSegment keeps letters, digits, "-", "_" and spaces, turning any run
// of other characters into one "-", as Kvit's asset names do.
func assetSegment(value, fallback string) string {
	var b strings.Builder
	for _, r := range value {
		switch {
		case r >= 'a' && r <= 'z' || r >= 'A' && r <= 'Z' || r >= '0' && r <= '9' || r == '-' || r == '_' || r == ' ' || r > 0x7f && isWord(r):
			b.WriteRune(r)
		case b.Len() > 0 && !strings.HasSuffix(b.String(), "-"):
			b.WriteByte('-')
		}
	}
	out := strings.TrimRight(strings.TrimSpace(b.String()), "-")
	if out == "" {
		return fallback
	}
	return out
}
