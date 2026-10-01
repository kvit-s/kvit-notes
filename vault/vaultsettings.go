package vault

// A vault's own settings: where a picture path starting with "/" is read from
// (the site folder, found for a Hugo site and otherwise the vault's folder)
// and where new pictures are saved (assets/, or images/ in the site folder),
// each changeable in Settings, This vault, and kept in .kvit/settings.json.

import (
	"encoding/json"
	"os"
	"path"
	"path/filepath"
	"strings"
)

const vaultSettingsFile = "settings.json"

// PictureSettings are a vault's picture folders.
type PictureSettings struct {
	// Site and Image are the folders set in the vault's settings, relative
	// to its top; HasSite and HasImage say whether they are set at all.
	Site, Image       string
	HasSite, HasImage bool
	// Detected is the site folder the vault's files suggest, and
	// DetectedFrom the file that suggested it, such as hugo.toml.
	Detected, DetectedFrom string
}

// loadPictureSettings reads .kvit/settings.json and looks for a site.
func (v *Vault) loadPictureSettings() {
	ps := PictureSettings{}
	ps.Detected, ps.DetectedFrom = detectSiteFolder(v.Root)
	data, err := os.ReadFile(filepath.Join(v.Root, ".kvit", vaultSettingsFile))
	if err == nil && len(data) <= 64<<10 {
		var obj map[string]any
		if json.Unmarshal(data, &obj) == nil {
			if s, ok := obj["siteFolder"].(string); ok {
				if n, ok := NormalizeFolder(s); ok {
					ps.Site, ps.HasSite = n, true
				}
			}
			if s, ok := obj["imageFolder"].(string); ok {
				if n, ok := NormalizeFolder(s); ok && n != "" {
					ps.Image, ps.HasImage = n, true
				}
			}
		}
	}
	v.Pictures = ps
}

// detectSiteFolder is "static" for a Hugo site: a hugo.* file, or a
// config.* file beside a content folder.
func detectSiteFolder(root string) (folder, marker string) {
	isFile := func(p string) bool {
		info, err := os.Stat(filepath.Join(root, p))
		return err == nil && info.Mode().IsRegular()
	}
	for _, name := range []string{"hugo.toml", "hugo.yaml", "hugo.yml", "hugo.json"} {
		if isFile(name) {
			return "static", name
		}
	}
	if info, err := os.Stat(filepath.Join(root, "content")); err == nil && info.IsDir() {
		for _, name := range []string{"config.toml", "config.yaml", "config.yml", "config.json"} {
			if isFile(name) {
				return "static", name
			}
		}
	}
	return "", ""
}

// SiteFolder is the folder a picture path starting with "/" is read from,
// relative to the vault's top; "" is the top itself.
func (v *Vault) SiteFolder() string {
	if v.Pictures.HasSite {
		return v.Pictures.Site
	}
	return v.Pictures.Detected
}

// SiteRoot is the site folder's path.
func (v *Vault) SiteRoot() string { return filepath.Join(v.Root, filepath.FromSlash(v.SiteFolder())) }

// ImageFolder is the folder new pictures are saved in, relative to the
// vault's top.
func (v *Vault) ImageFolder() string {
	if v.Pictures.HasImage {
		return v.Pictures.Image
	}
	if site := v.SiteFolder(); site != "" {
		return site + "/images"
	}
	return "assets"
}

// NormalizeFolder reads a folder typed in the settings: slashes either
// way, no "./" in front or "/" behind, and "." for the top. ok is false
// for a folder outside the vault or inside .kvit.
func NormalizeFolder(typed string) (string, bool) {
	s := strings.ReplaceAll(strings.TrimSpace(typed), `\`, "/")
	for strings.HasPrefix(s, "./") {
		s = s[2:]
	}
	s = strings.TrimRight(s, "/")
	if s == "." {
		s = ""
	}
	if s == "" {
		return "", true
	}
	if path.IsAbs(s) || filepath.IsAbs(s) || strings.Contains(s, ":") {
		return "", false
	}
	for _, seg := range strings.Split(s, "/") {
		if seg == "" || seg == "." || seg == ".." {
			return "", false
		}
	}
	if strings.Split(s, "/")[0] == ".kvit" {
		return "", false
	}
	return s, true
}

// SetPictureFolders sets the site folder and the picture folder; a nil
// value goes back to what the vault's files suggest.
func (v *Vault) SetPictureFolders(site, image *string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	ps := v.Pictures
	ps.HasSite, ps.Site = false, ""
	if site != nil {
		n, ok := NormalizeFolder(*site)
		if !ok {
			return ErrName
		}
		ps.HasSite, ps.Site = true, n
	}
	ps.HasImage, ps.Image = false, ""
	if image != nil {
		n, ok := NormalizeFolder(*image)
		if !ok {
			return ErrName
		}
		if n != "" {
			ps.HasImage, ps.Image = true, n
		}
	}
	obj := map[string]string{}
	if ps.HasSite {
		obj["siteFolder"] = ps.Site
	}
	if ps.HasImage {
		obj["imageFolder"] = ps.Image
	}
	data, err := json.MarshalIndent(obj, "", "    ")
	if err != nil {
		return err
	}
	if err := os.MkdirAll(filepath.Join(v.Root, ".kvit"), 0o755); err != nil {
		return err
	}
	if err := writeAtomic(filepath.Join(v.Root, ".kvit", vaultSettingsFile), append(data, '\n')); err != nil {
		return err
	}
	v.Pictures = ps
	return nil
}
