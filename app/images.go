package app

// Loading the pictures a note's image blocks name: beside the note first,
// then from the top of the vault, then as written, and a path starting with
// "/" from the vault's site folder, the first that is a file.

import (
	"bytes"
	"context"
	"errors"
	"fmt"
	"image"
	_ "image/gif"
	_ "image/jpeg"
	"image/png"
	_ "image/png"
	"os"
	"path"
	"path/filepath"
	"strings"
	"sync"
	"time"
	"unicode"

	"github.com/kvit-s/kvit-notes/vault"
	"github.com/kvit-s/kvit-ui/platform"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/toolbox/v2/uti"
	"github.com/richardwilkes/unison"
	"golang.org/x/image/draw"
)

// maxPicture is the largest picture file read.
const maxPicture = 64 << 20

// The image cache: decoded pictures kept by resolved key, downscaled to the
// display width, evicted oldest-first over a byte budget. Loading is lazy
// (only when drawn) and a large picture never reaches the screen at full
// size.
const (
	// maxImageWidth is the widest a picture is kept at, in pixels: the text
	// column with room for HiDPI. A wider picture is scaled down on load.
	maxImageWidth = 1400
	// imageCacheBudget bounds decoded pictures, about 16 screens of photos.
	imageCacheBudget = 64 << 20
)

// isRemoteURL reports whether stored names a web address: an http or https
// scheme, case-insensitively, as the editor reads it.
func isRemoteURL(s string) bool {
	lower := strings.ToLower(strings.TrimSpace(s))
	return strings.HasPrefix(lower, "http://") || strings.HasPrefix(lower, "https://")
}

type imageCacheEntry struct {
	drawable unison.Drawable
	size     int
	lastUse  time.Time
}

type imageCache struct {
	mu      sync.Mutex
	entries map[string]*imageCacheEntry
	bytes   int
}

func (c *imageCache) get(key string) (unison.Drawable, bool) {
	c.mu.Lock()
	defer c.mu.Unlock()
	e, ok := c.entries[key]
	if !ok {
		return nil, false
	}
	e.lastUse = time.Now()
	return e.drawable, true
}

func (c *imageCache) add(key string, d unison.Drawable, size int) {
	if c.entries == nil {
		c.entries = map[string]*imageCacheEntry{}
	}
	c.mu.Lock()
	defer c.mu.Unlock()
	if old, ok := c.entries[key]; ok {
		c.bytes -= old.size
	}
	c.entries[key] = &imageCacheEntry{drawable: d, size: size, lastUse: time.Now()}
	c.bytes += size
	for c.bytes > imageCacheBudget && len(c.entries) > 1 {
		var oldest string
		var at time.Time
		for k, e := range c.entries {
			if oldest == "" || e.lastUse.Before(at) {
				oldest, at = k, e.lastUse
			}
		}
		c.bytes -= c.entries[oldest].size
		delete(c.entries, oldest)
	}
}

// scaledSize is the size a picture is kept at: wider than maxImageWidth
// scales down to it, preserving aspect. ok is false when it already fits.
func scaledSize(w, h int) (nw, nh int, ok bool) {
	if w <= maxImageWidth || w <= 0 {
		return w, h, false
	}
	nw = maxImageWidth
	nh = h * nw / w
	if nh < 1 {
		nh = 1
	}
	return nw, nh, true
}

// decodeImage decodes picture bytes and scales them down to maxImageWidth,
// returning the drawable and its cached size.
func decodeImage(data []byte, ext string) (unison.Drawable, int, error) {
	if strings.EqualFold(ext, ".svg") {
		svg, err := unison.NewSVGFromReader(bytes.NewReader(data))
		if err != nil {
			return nil, 0, err
		}
		w, h := svg.Size().Width, svg.Size().Height
		return svgDrawable{svg}, int(w*h*4) + len(data), nil
	}
	img, _, err := image.Decode(bytes.NewReader(data))
	if err != nil {
		// Not a still the stdlib reads (e.g. WebP, BMP): let unison try.
		d, err2 := unison.NewImageFromBytes(data, geom.NewPoint(1, 1))
		if err2 != nil {
			return nil, 0, err
		}
		sz := d.LogicalSize()
		return d, int(sz.Width*sz.Height*4) + len(data), nil
	}
	bounds := img.Bounds()
	w, h := bounds.Dx(), bounds.Dy()
	if nw, nh, ok := scaledSize(w, h); ok {
		dst := image.NewNRGBA(image.Rect(0, 0, nw, nh))
		draw.ApproxBiLinear.Scale(dst, dst.Bounds(), img, bounds, draw.Over, nil)
		img = dst
		w, h = nw, nh
	}
	d, err := unison.NewImageFromBytes(encodePNG(img), geom.NewPoint(1, 1))
	if err != nil {
		return nil, 0, err
	}
	return d, w * h * 4, nil
}

func encodePNG(img image.Image) []byte {
	var b bytes.Buffer
	// PNG keeps every pixel exact; the cache, not the file, is what is
	// bounded, so size on disk never changes.
	_ = png.Encode(&b, img)
	return b.Bytes()
}

// imageLoader loads pictures for the note at a vault path.
func (w *Window) imageLoader(note string) func(string) (unison.Drawable, error) {
	root := w.Vault.Root
	noteDir := filepath.Join(root, filepath.FromSlash(vault.Parent(note)))
	if w.imgCache == nil {
		w.imgCache = &imageCache{}
	}
	cache := w.imgCache
	return func(stored string) (unison.Drawable, error) {
		// Remote pictures need their origin approved first; the card
		// offers Load, which approves it.
		if isRemoteURL(stored) {
			if w.egress == nil || !w.egress.isAllowed(stored) {
				if w.egress != nil {
					if reason := refusalReason(stored); reason != "" {
						return nil, errors.New(reason + ": " + stored)
					}
				}
				return nil, errors.New("needs approval: " + stored)
			}
			if d, ok := cache.get("remote:" + stored); ok {
				return d, nil
			}
			ctx, cancel := context.WithTimeout(context.Background(), previewTimeout)
			defer cancel()
			data, err := fetchGuarded(ctx, stored, maxRemoteBytes)
			if err != nil {
				return nil, err
			}
			d, size, err := decodeImage(data, path.Ext(stored))
			if err != nil {
				return nil, err
			}
			cache.add("remote:"+stored, d, size)
			return d, nil
		}
		var candidates []string
		if !filepath.IsAbs(stored) {
			candidates = append(candidates, filepath.Join(noteDir, filepath.FromSlash(stored)),
				filepath.Join(root, filepath.FromSlash(stored)))
		} else {
			candidates = append(candidates, stored)
		}
		if rel, ok := strings.CutPrefix(stored, "/"); ok && rel != "" && !strings.HasPrefix(rel, "/") {
			candidates = append(candidates, filepath.Join(w.Vault.SiteRoot(), filepath.FromSlash(rel)))
		}
		for _, c := range candidates {
			info, err := os.Stat(c)
			if err != nil || !info.Mode().IsRegular() || info.Size() > maxPicture {
				continue
			}
			key := "file:" + filepath.Clean(c)
			if d, ok := cache.get(key); ok {
				return d, nil
			}
			data, err := os.ReadFile(c)
			if err != nil {
				continue
			}
			d, size, err := decodeImage(data, filepath.Ext(c))
			if err != nil {
				continue
			}
			cache.add(key, d, size)
			return d, nil
		}
		return nil, errors.New("no such picture")
	}
}

// svgDrawable draws an SVG as a picture of its own size.
type svgDrawable struct{ *unison.SVG }

func (s svgDrawable) LogicalSize() geom.Size { return s.Size() }

// pickImage asks for a picture file for a new image block, adds it to the
// vault by Kvit's asset rule, and writes the block's line.
func (w *Window) pickImage(block int64) {
	d := unison.NewOpenDialog()
	d.SetAllowedExtensions("png", "jpg", "jpeg", "gif", "webp", "svg", "bmp")
	if !d.RunModal() || len(d.Paths()) == 0 {
		return
	}
	note := ""
	if w.open != nil {
		note = w.open.Path
	}
	stored, err := w.ingestFile(d.Paths()[0], note)
	if err != nil {
		w.fail("Could not add the picture", err)
		return
	}
	w.setImageLine(block, stored)
}

// setImageLine writes an image block's line for a stored path.
func (w *Window) setImageLine(block int64, stored string) {
	doc := w.Editor.Doc
	b := doc.Block(block)
	if b == nil {
		return
	}
	doc.Edit("image", func() {
		b.Text = "![](" + stored + ")"
		doc.SetCaret(block, len([]rune(b.Text)))
	})
	w.Editor.Refresh()
}

// ingestFile adds a picture to the vault: a file already inside the vault is
// linked where it is; any other is copied into the vault's picture folder
// (assets/ unless set) as "<note>-<yyyyMMdd-HHmmss>.<ext>", named after the
// note it is for, with "-N" added when that is taken. The path returned is
// from the top of the vault, or from "/" for a file in the site folder.
func (w *Window) ingestFile(src, note string) (string, error) {
	abs, err := filepath.Abs(src)
	if err != nil {
		return "", err
	}
	if rel, err := filepath.Rel(w.Vault.Root, abs); err == nil && !strings.HasPrefix(rel, "..") {
		return w.storedPath(abs), nil
	}
	data, err := os.ReadFile(abs)
	if err != nil {
		return "", err
	}
	dir := filepath.Join(w.Vault.Root, filepath.FromSlash(w.Vault.ImageFolder()))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	ext := safeSegment(strings.TrimPrefix(filepath.Ext(abs), "."), "bin")
	base := noteSlug(note) + "-" + time.Now().Format("20060102-150405")
	name := base + "." + ext
	for n := 1; ; n++ {
		if _, err := os.Stat(filepath.Join(dir, name)); errors.Is(err, os.ErrNotExist) {
			break
		}
		name = fmt.Sprintf("%s-%d.%s", base, n, ext)
	}
	target := filepath.Join(dir, name)
	if err := os.WriteFile(target, data, 0o644); err != nil {
		return "", err
	}
	return w.storedPath(target), nil
}

// storedPath is how a note names a picture file in the vault: from "/" inside
// a site folder that is not the vault's top, otherwise from the top of the
// vault.
func (w *Window) storedPath(abs string) string {
	root := filepath.Clean(w.Vault.Root)
	site := filepath.Clean(w.Vault.SiteRoot())
	if site != root {
		if rel, err := filepath.Rel(site, abs); err == nil && !strings.HasPrefix(rel, "..") {
			return "/" + filepath.ToSlash(rel)
		}
	}
	rel, err := filepath.Rel(root, abs)
	if err != nil || strings.HasPrefix(rel, "..") {
		return abs
	}
	return filepath.ToSlash(rel)
}

// noteSlug is a note's name as new picture names start with: its file name
// lowercased, every run of other characters than a to z and 0 to 9 a dash, or
// "image".
func noteSlug(note string) string {
	name := strings.TrimSuffix(path.Base(note), path.Ext(note))
	var b strings.Builder
	dash := false
	for _, r := range strings.ToLower(name) {
		if r >= 'a' && r <= 'z' || r >= '0' && r <= '9' {
			b.WriteRune(r)
			dash = false
		} else if !dash {
			b.WriteByte('-')
			dash = true
		}
	}
	slug := strings.Trim(b.String(), "-")
	if slug == "" {
		return "image"
	}
	return slug
}

// safeSegment keeps letters, digits, "-", "_" and spaces, turning any run
// of other characters into one "-", as Kvit's asset names do.
func safeSegment(value, fallback string) string {
	var b strings.Builder
	for _, r := range value {
		switch {
		case unicode.IsLetter(r) || unicode.IsDigit(r) || r == '-' || r == '_' || r == ' ':
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

// pasteImage saves a picture on the clipboard into the vault and reports its
// stored path. It reports false when the clipboard holds no picture, leaving
// the text paste path to run instead. Under WSL a picture copied in Windows
// is not on unison's clipboard, so Windows' clipboard is asked last.
func (w *Window) pasteImage() (string, bool) {
	for _, dt := range []*uti.DataType{uti.PNG, uti.JPEG, uti.GIF, uti.WEBP, uti.BMP, uti.TIFF} {
		if !unison.ClipboardHasDataType(dt) {
			continue
		}
		data := unison.ClipboardGetData(dt)
		if len(data) == 0 {
			continue
		}
		note := ""
		if w.open != nil {
			note = w.open.Path
		}
		if stored, err := w.ingestImageBytes(data, dt, note); err == nil {
			return stored, true
		}
	}
	// A generic image flavour (e.g. public.image from another app): try its
	// bytes as a picture before giving up.
	if unison.ClipboardHasDataType(uti.Image) {
		if data := unison.ClipboardGetData(uti.Image); len(data) > 0 {
			note := ""
			if w.open != nil {
				note = w.open.Path
			}
			if stored, err := w.ingestImageBytes(data, uti.PNG, note); err == nil {
				return stored, true
			}
		}
	}
	if data, ok := platform.WindowsClipboardPicture(); ok {
		note := ""
		if w.open != nil {
			note = w.open.Path
		}
		if stored, err := w.ingestImageBytes(data, uti.PNG, note); err == nil {
			return stored, true
		}
	}
	return "", false
}

// ingestImageBytes adds clipboard picture bytes to the vault by Kvit's asset
// rule, as ingestFile does for files: into the vault's picture folder as
// "<note>-<yyyyMMdd-HHmmss>.<ext>", named after the note it is for. The
// bytes must decode as a picture; SVG clipboard data is stored as-is.
func (w *Window) ingestImageBytes(data []byte, dt *uti.DataType, note string) (string, error) {
	ext := "png"
	if dt != nil {
		for _, e := range dt.Extensions {
			if s := strings.TrimPrefix(strings.ToLower(strings.TrimSpace(e)), "."); s != "" {
				ext = s
				break
			}
		}
	}
	if ext == "svg" || ext == "svgz" {
		// SVG has no raster to decode; store the bytes when they parse.
		if _, err := unison.NewSVGFromReader(bytes.NewReader(data)); err != nil {
			return "", err
		}
		ext = "svg"
	} else if _, _, err := image.Decode(bytes.NewReader(data)); err != nil {
		// Not a still the stdlib reads (e.g. WebP, BMP): let unison try,
		// as decodeImage does.
		if _, err2 := unison.NewImageFromBytes(data, geom.NewPoint(1, 1)); err2 != nil {
			return "", err
		}
		if ext == "jpe" || ext == "jif" || ext == "jfif" || ext == "jfi" {
			ext = "jpg"
		}
	}
	dir := filepath.Join(w.Vault.Root, filepath.FromSlash(w.Vault.ImageFolder()))
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return "", err
	}
	ext = safeSegment(strings.ToLower(ext), "bin")
	base := noteSlug(note) + "-" + time.Now().Format("20060102-150405")
	name := base + "." + ext
	for n := 1; ; n++ {
		if _, err := os.Stat(filepath.Join(dir, name)); errors.Is(err, os.ErrNotExist) {
			break
		}
		name = fmt.Sprintf("%s-%d.%s", base, n, ext)
	}
	target := filepath.Join(dir, name)
	if err := os.WriteFile(target, data, 0o644); err != nil {
		return "", err
	}
	return w.storedPath(target), nil
}

// saveDroppedImage copies an image file dropped from another application into
// the vault's picture folder, through ingestFile's rule: a file already in
// the vault is linked where it is, any other is copied as
// "<note>-<yyyyMMdd-HHmmss>.<ext>".
func (w *Window) saveDroppedImage(source string) (string, bool) {
	note := ""
	if w.open != nil {
		note = w.open.Path
	}
	if stored, err := w.ingestFile(source, note); err == nil {
		return stored, true
	}
	return "", false
}
