package editor

// Embeds (features.md 1.2.14, Kvit's mediakinds.cpp and EmbedBlock.qml): an
// image line whose address is a web page rather than a picture or a media
// file is drawn as a card: a picture, the page's title and the site. The
// page is fetched only when the reader presses Load preview, since reading
// a note must not tell a web site that it was read; until then the card
// shows the address.

import (
	"net/url"
	"strconv"
	"strings"
	"unicode"

	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/icons"
	"github.com/kvit-s/kvit-ui/text"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// Preview is what a web page says about itself, for its card.
type Preview struct {
	Title, Description string
	// Image is the page's picture, or nil.
	Image unison.Drawable
	// Failed says why the page could not be read, "" when it was.
	Failed string
}

// isEmbed reports whether an image line names a web page.
func isEmbed(ref ImageRef) bool {
	ext := extensionOf(ref.Path)
	return ref.Remote && !imageExts[ext] && !mediaExts[ext]
}

// isRemoteURL reports whether s has an http or https scheme, case
// insensitively (ImageAssets::isRemote, via QUrl::scheme).
func isRemoteURL(s string) bool {
	sch := embedScheme(s)
	return strings.EqualFold(sch, "http") || strings.EqualFold(sch, "https")
}

// embedScheme is the URL scheme of s, "" when it has none: a leading
// letter followed by letters, digits, "+", "-" or "." before the first
// colon.
func embedScheme(s string) string {
	i := strings.IndexByte(s, ':')
	if i <= 0 {
		return ""
	}
	if c := s[0]; !(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z') {
		return ""
	}
	for j := 1; j < i; j++ {
		c := s[j]
		if !(c >= 'a' && c <= 'z' || c >= 'A' && c <= 'Z' || c >= '0' && c <= '9' || c == '+' || c == '-' || c == '.') {
			return ""
		}
	}
	return s[:i]
}

// isHostWithPort reports whether s is a bare host with a numeric port,
// such as "localhost:8080/wiki", which parses as a URL whose scheme is the
// host (ImageAssets::isHostWithPort).
func isHostWithPort(s string) bool {
	i := strings.IndexByte(s, ':')
	if i <= 0 {
		return false
	}
	for _, r := range s[:i] {
		if unicode.IsSpace(r) || r == ':' || r == '/' || r == '?' || r == '#' {
			return false
		}
	}
	rest := s[i+1:]
	if rest == "" || rest[0] < '0' || rest[0] > '9' {
		return false
	}
	j := 0
	for j < len(rest) && rest[j] >= '0' && rest[j] <= '9' {
		j++
	}
	if j == len(rest) {
		return true
	}
	return rest[j] == '/' || rest[j] == '?' || rest[j] == '#'
}

// normalizeEmbedURL is ImageAssets::normalizeEmbedUrl: what typed text is
// inserted as. A bare host gains "https://"; text that cannot be a web
// address yields "".
func normalizeEmbedURL(input string) string {
	u := strings.TrimSpace(input)
	if u == "" || strings.ContainsFunc(u, unicode.IsSpace) {
		return ""
	}
	if isRemoteURL(u) {
		return u
	}
	if strings.HasPrefix(u, "//") {
		return "https:" + u
	}
	if sch := embedScheme(u); sch != "" && !isHostWithPort(u) {
		return ""
	}
	return "https://" + u
}

// NormalizeEmbedURL is normalizeEmbedURL for the application: what typed
// text an embed Edit URL… dialog writes.
func NormalizeEmbedURL(input string) string { return normalizeEmbedURL(input) }

// isEmbedURL reports whether u is an address the embed card draws: remote
// with no picture or media extension (ImageAssets::isEmbedUrl).
func isEmbedURL(u string) bool {
	if !isRemoteURL(u) {
		return false
	}
	ext := extensionOf(u)
	return !imageExts[ext] && !mediaExts[ext]
}

// embedQueryURL is the address a / menu query names, "" when it names none:
// the query itself, or the address after "embed ". A bare word is not an
// address: the candidate must hold a ".", "/" or ":" so filtering for
// "h1" or "embed" does not offer an embed of "https://h1".
func embedQueryURL(q string) string {
	t := strings.TrimSpace(q)
	if rest, ok := strings.CutPrefix(strings.ToLower(t), "embed "); ok {
		t = strings.TrimSpace(t[len(t)-len(rest):])
	} else if !strings.ContainsAny(t, "./:") {
		return ""
	}
	if t == "" || !strings.ContainsAny(t, "./:") {
		return ""
	}
	if u := normalizeEmbedURL(t); u != "" && isEmbedURL(u) {
		return u
	}
	return ""
}

// SetPreview gives a page's card what the page says, once it is read.
func (e *Editor) SetPreview(address string, p Preview) {
	if e.previews == nil {
		e.previews = map[string]*Preview{}
	}
	e.previews[address] = &p
	e.generation++
	e.changed()
}

// escapeImageField escapes a field of an image line, as written.
func escapeImageField(s string) string {
	s = strings.ReplaceAll(s, "\\", "\\\\")
	s = strings.ReplaceAll(s, "\n", "\\n")
	s = strings.ReplaceAll(s, "]", "\\]")
	s = strings.ReplaceAll(s, "\"", "\\\"")
	s = strings.ReplaceAll(s, "|", "\\|")
	return s
}

// formatImageRef writes an image line, preserving its alt text, width and
// caption (src/content/imageassets.cpp).
func formatImageRef(ref ImageRef) string {
	alt := escapeImageField(ref.Alt)
	if ref.Width > 0 {
		alt = escapeImageField(ref.Alt) + "|" + strconv.Itoa(ref.Width)
	}
	line := "![" + alt + "](" + ref.Path
	if ref.Caption != "" {
		line += " \"" + escapeImageField(ref.Caption) + "\""
	}
	return line + ")"
}

// SetEmbedURL rewrites an embed block's address, keeping its alt text, as
// one undo step (the block menu's Edit URL…, EmbedBlock.qml editEmbedUrl).
func (e *Editor) SetEmbedURL(id int64, url string) bool {
	b := e.Doc.Block(id)
	if b == nil {
		return false
	}
	ref, ok := ParseImageLine(strings.TrimSpace(b.Text))
	if !ok || !isEmbed(ref) && !isEmbedURL(url) && !ref.Remote {
		// Only embed lines are retargeted; other blocks are left alone.
		if !ok {
			return false
		}
	}
	ref.Path = strings.TrimSpace(url)
	ref.Remote = isRemoteURL(ref.Path)
	e.Doc.Edit("embed", func() {
		b.Text = formatImageRef(ref)
	})
	e.touched()
	e.changed()
	return true
}

// SetEmbedSize gives embed blocks a configured width and height in px, kept
// in their attributes (EmbedBlock.qml embedWidth/embedHeight): 0 clears to
// the default full-width card.
func (e *Editor) SetEmbedSize(ids []int64, width, height int) {
	set := func(id int64, key, value string) {
		b := e.Doc.Block(id)
		if b == nil {
			return
		}
		var keep []string
		for _, tok := range strings.Fields(b.Attrs) {
			if k, _, _ := strings.Cut(tok, "="); k != key {
				keep = append(keep, tok)
			}
		}
		if value != "" {
			keep = append(keep, key+"="+value)
		}
		b.Attrs = canonicalAttrs(strings.Join(keep, " "))
	}
	e.Doc.Edit("attributes", func() {
		for _, id := range ids {
			if width <= 0 {
				set(id, "width", "")
			} else {
				set(id, "width", strconv.Itoa(width))
			}
			if height <= 0 {
				set(id, "height", "")
			} else {
				set(id, "height", strconv.Itoa(height))
			}
		}
	})
	e.touched()
	e.changed()
}

// videoHosts are the video addresses an embed card draws with a play
// badge, opening externally (EmbedBlock.qml isVideo).
var videoHosts = []string{
	"youtube.com", "youtu.be", "vimeo.com", "dailymotion.com",
	"dai.ly", "tiktok.com", "twitch.tv", "peertube",
}

// isVideoHost reports whether an address is a video host's page.
func isVideoHost(address string) bool {
	u, err := url.Parse(strings.TrimSpace(address))
	if err != nil {
		return false
	}
	host := strings.ToLower(strings.TrimPrefix(u.Host, "www."))
	for _, h := range videoHosts {
		if host == h || strings.HasSuffix(host, "."+h) || strings.Contains(host, h) {
			return true
		}
	}
	return false
}

// The card in design pixels (EmbedBlock.qml): its height, padding and
// corner, and the picture's size.
const (
	embedHeight = 94
	embedPad    = 10
	embedRadius = 6
	embedThumbW = 120
	embedThumbH = 74
	embedGap    = 12
)

// embedParts are where a card's parts are: its title, which opens the
// page, and its Load preview button.
func (e *Editor) embedParts(card geom.Rect) (title, load geom.Rect) {
	x := card.X + e.px(embedPad+embedThumbW+embedGap)
	title = geom.NewRect(x, card.Y+e.px(embedPad), card.Right()-e.px(embedPad)-x, e.px(20))
	load = geom.NewRect(x, card.Y+e.px(embedPad+22), e.px(90), e.px(22))
	return
}

// siteOf is an address's host, without "www.".
func siteOf(address string) string {
	u, err := url.Parse(address)
	if err != nil {
		return address
	}
	return strings.TrimPrefix(u.Host, "www.")
}

func (e *Editor) drawEmbed(gc *unison.Canvas, ref ImageRef, card geom.Rect) {
	t := e.tok()
	r := e.px(embedRadius)
	gc.DrawRoundedRect(card, geom.NewSize(r, r), kvitui.Color(t.PanelBackground).Paint(gc, card, paintstyle.Fill))
	e.stroke(gc, card, r, e.px(1), t.Border)
	title, load := e.embedParts(card)
	// A drawn selection of its own highlights its lines behind the text.
	if id := e.drawnBlockIn(card); id != 0 {
		if from, to, ok := e.drawnLineRange(id); ok {
			lines := e.drawnLines(id)
			band := func(line int, rect geom.Rect) {
				if line >= from && line <= to && line < len(lines) {
					e.fill(gc, rect, t.SelectionTint)
				}
			}
			band(0, title)
			if len(lines) == 3 {
				band(1, geom.NewRect(load.X, load.Y, title.Width, e.px(22)))
			}
			band(len(lines)-1, geom.NewRect(title.X, load.Bottom()+e.px(4), title.Width, e.px(16)))
		}
	}
	thumb := geom.NewRect(card.X+e.px(embedPad), card.Y+e.px(embedPad), e.px(embedThumbW), e.px(embedThumbH))
	p := e.previews[ref.Path]
	if p != nil && p.Image != nil {
		p.Image.DrawInRect(gc, thumb, nil, nil)
	} else {
		e.fillRound(gc, thumb, e.px(4), t.ChipBackground)
		if g, ok := icons.Glyph("link"); ok {
			l := e.label(string(g), e.ui.Icon(int(e.px(18)), t.TextMuted))
			w, h := l.Size()
			l.Draw(gc, thumb.X+(thumb.Width-w)/2, thumb.Y+(thumb.Height-h)/2)
		}
	}
	if isVideoHost(ref.Path) {
		// The play affordance over a video host's thumbnail, as
		// EmbedBlock.qml draws it; the card opens externally.
		cx, cy := thumb.X+thumb.Width/2, thumb.Y+thumb.Height/2
		rad := e.px(15)
		gc.DrawOval(geom.NewRect(cx-rad, cy-rad, 2*rad, 2*rad), kvitui.Color(t.TextPrimary).Paint(gc, thumb, paintstyle.Fill))
		l := e.label("▶", e.chrome(kvitui.RoleSmall, text.Bold, t.PanelBackground))
		w, h := l.Size()
		l.Draw(gc, cx-w/2, cy-h/2)
	}
	words := ref.Path
	if ref.Alt != "" {
		words = ref.Alt
	}
	if p != nil && p.Title != "" {
		words = p.Title
	}
	tl := e.ui.Fonts.Layout([]text.Span{{Text: words, Style: e.chrome(kvitui.RoleStrong, text.Bold, t.TextPrimary)}},
		text.Options{MaxWidth: max(1, title.Width), Elide: true})
	tl.Draw(gc, title.X, title.Y)
	small := e.chrome(kvitui.RoleSmall, text.Regular, t.TextFaint)
	y := load.Y
	switch {
	case p == nil:
		e.fillRound(gc, load, e.px(3), t.ChipBackground)
		e.stroke(gc, load, e.px(3), e.px(1), t.Border)
		l := e.label("Load preview", e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary))
		w, h := l.Size()
		l.Draw(gc, load.X+(load.Width-w)/2, load.Y+(load.Height-h)/2)
		n := e.label("· not loaded", small)
		_, nh := n.Size()
		n.Draw(gc, load.Right()+e.px(8), load.Y+(load.Height-nh)/2)
	case p.Failed != "":
		e.label(p.Failed, small).Draw(gc, load.X, y+e.px(3))
	case p.Description != "":
		dl := e.ui.Fonts.Layout([]text.Span{{Text: p.Description, Style: e.chrome(kvitui.RoleSmall, text.Regular, t.TextSecondary)}},
			text.Options{MaxWidth: max(1, title.Width), Elide: true})
		dl.Draw(gc, load.X, y+e.px(3))
	}
	e.label(siteOf(ref.Path), small).Draw(gc, title.X, load.Bottom()+e.px(4))
}

// embedCard is where the embed card of row i is, when the row is an embed
// drawn as its card: the configured width and height from its attributes,
// or the default full-width card (EmbedBlock.qml effectiveWidth).
func (e *Editor) embedCard(i int) (geom.Rect, ImageRef, bool) {
	ref, ok, shows := e.pictureBlock(i)
	if !ok || !isEmbed(ref) {
		return geom.Rect{}, ref, false
	}
	o := e.textOrigin(i)
	if shows {
		o.Y += e.layout(i).height() + e.px(pictureGap)
	}
	body := e.bodyRect(i)
	w := body.Width - 2*e.px(tocInset)
	h := e.px(embedHeight)
	b := &e.Doc.Blocks[i]
	if v, ok := b.Attr("width"); ok {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			w = min(w, e.px(float32(n)))
		}
	}
	if v, ok := b.Attr("height"); ok {
		if n, err := strconv.Atoi(v); err == nil && n > 0 {
			h = e.px(float32(n))
		}
	}
	return geom.NewRect(body.X+e.px(tocInset), o.Y, w, h), ref, true
}
