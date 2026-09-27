package vault

// Where a vault is found when none is named: the vaults the Qt app had open
// last (session.openVaults in its settings.json), else Documents/Kvit, as the
// Qt app does (src/qml/windowregistry.cpp, src/qml/processservices.cpp).

import (
	"encoding/json"
	"os"
	"path/filepath"
	"runtime"
)

// SettingsPath is the Qt app's own settings file: settings.json in
// Qt's application configuration folder for organisation "Kvit" and
// application "Kvit Notes".
func SettingsPath() string {
	var base string
	switch runtime.GOOS {
	case "windows":
		base = os.Getenv("LOCALAPPDATA")
	case "darwin":
		home, _ := os.UserHomeDir()
		base = filepath.Join(home, "Library", "Preferences")
	default:
		base = os.Getenv("XDG_CONFIG_HOME")
		if base == "" {
			home, _ := os.UserHomeDir()
			base = filepath.Join(home, ".config")
		}
	}
	return filepath.Join(base, "Kvit", "Kvit Notes", "settings.json")
}

// OpenVaults are the vaults the Qt app had open when it last closed, those
// that still exist.
func OpenVaults() []string {
	data, err := os.ReadFile(SettingsPath())
	if err != nil {
		return nil
	}
	var s struct {
		OpenVaults []string `json:"session.openVaults"`
	}
	if json.Unmarshal(data, &s) != nil {
		return nil
	}
	var out []string
	for _, p := range s.OpenVaults {
		if info, err := os.Stat(p); err == nil && info.IsDir() {
			out = append(out, p)
		}
	}
	return out
}

// DefaultRoot is the vault opened when none is named and none was open:
// Kvit in the reader's Documents folder.
func DefaultRoot() string {
	return filepath.Join(documentsDir(), "Kvit")
}
