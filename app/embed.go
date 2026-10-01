package app

// Reading a web page for its embed card, when the reader presses Load
// preview: its title, description and picture from its <title> and Open
// Graph tags. Nothing is fetched otherwise, and nothing is written to disk.

import (
	"context"
	"errors"
	"html"
	"io"
	"net/http"
	"net/url"
	"regexp"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
)

// The most a page, and its picture, is read of.
const (
	maxPage        = 1 << 20
	maxPreviewPict = 8 << 20
	previewTimeout = 10 * time.Second
)

var (
	reTitle = regexp.MustCompile(`(?is)<title[^>]*>(.*?)</title>`)
	reMeta  = regexp.MustCompile(`(?is)<meta\s[^>]*>`)
	reAttr  = regexp.MustCompile(`(?is)(property|name|content)\s*=\s*("([^"]*)"|'([^']*)')`)
)

// pageMeta reads a page's title, description and picture address.
func pageMeta(page string) (title, description, image string) {
	meta := map[string]string{}
	for _, tag := range reMeta.FindAllString(page, -1) {
		var key, content string
		for _, a := range reAttr.FindAllStringSubmatch(tag, -1) {
			v := a[3] + a[4]
			switch strings.ToLower(a[1]) {
			case "property", "name":
				key = strings.ToLower(v)
			case "content":
				content = v
			}
		}
		if key != "" && meta[key] == "" {
			meta[key] = html.UnescapeString(strings.TrimSpace(content))
		}
	}
	title = meta["og:title"]
	if title == "" {
		if m := reTitle.FindStringSubmatch(page); m != nil {
			title = html.UnescapeString(strings.Join(strings.Fields(m[1]), " "))
		}
	}
	description = meta["og:description"]
	if description == "" {
		description = meta["description"]
	}
	return title, description, meta["og:image"]
}

// fetch reads at most limit bytes of an address.
func fetch(ctx context.Context, address string, limit int64) ([]byte, error) {
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, address, nil)
	if err != nil {
		return nil, err
	}
	req.Header.Set("User-Agent", "Kvit Notes")
	resp, err := http.DefaultClient.Do(req)
	if err != nil {
		return nil, err
	}
	defer resp.Body.Close()
	if resp.StatusCode/100 != 2 {
		return nil, errors.New(resp.Status)
	}
	return io.ReadAll(io.LimitReader(resp.Body, limit))
}

// loadPreview reads a page in the background and gives the editor what it
// says. Pressing Load preview approves the page's origin for later loads,
// as in Kvit: the button is the consent. Nothing fetches without it unless
// automatic loading is on, and an unfetchable address fails with its reason.
func (w *Window) loadPreview(address string) {
	ed := w.Editor
	if w.egress != nil {
		if reason := refusalReason(address); reason != "" {
			ed.SetPreview(address, editor.Preview{Failed: reason + ": " + address})
			return
		}
		if !w.egress.isAllowed(address) && !canRequestConsent(address) {
			ed.SetPreview(address, editor.Preview{Failed: refusalReason(address) + ": " + address})
			return
		}
		// The press is the consent: approve the origin for later loads.
		w.egress.allowOrigin(address)
		ed.ForgetPicture(address)
	}
	go func() {
		ctx, cancel := context.WithTimeout(context.Background(), previewTimeout)
		defer cancel()
		var p editor.Preview
		page, err := fetchGuarded(ctx, address, maxPage)
		if err != nil {
			p.Failed = "Could not load the preview: " + err.Error()
		} else {
			var image string
			p.Title, p.Description, image = pageMeta(string(page))
			if image != "" {
				if base, err := url.Parse(address); err == nil {
					if ref, err := base.Parse(image); err == nil {
						if data, err := fetchGuarded(ctx, ref.String(), maxPreviewPict); err == nil {
							if img, err := unison.NewImageFromBytes(data, geom.NewPoint(1, 1)); err == nil {
								p.Image = img
							}
						}
					}
				}
			}
		}
		unison.InvokeTask(func() { ed.SetPreview(address, p) })
	}()
}

// editEmbed rewrites an embed block's address when Edit URL… is chosen in
// the block menu.
func (w *Window) editEmbed(id int64, current string) {
	ed := w.Editor
	if ed.Doc.ReadOnly {
		return
	}
	ui := w.ui
	field := kvitui.NewField(ui)
	field.Label = "URL"
	field.SetText(current)
	d := kvitui.NewDialog(ui, "Edit Embed", settingRow(ui, "URL", field))
	d.ConfirmText = "OK"
	d.OnAccept = func() {
		if u := editor.NormalizeEmbedURL(field.Text()); u != "" {
			ed.SetEmbedURL(id, u)
		}
	}
	d.Open(w.Win)
	unison.InvokeTask(func() {
		field.Focus()
		field.Edit().SelectAll()
	})
}
