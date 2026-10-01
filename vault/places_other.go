//go:build !windows

package vault

import (
	"bufio"
	"os"
	"path/filepath"
	"strings"
)

// documentsDir is the reader's Documents folder: on Linux the one named in
// user-dirs.dirs, else ~/Documents.
func documentsDir() string {
	home, _ := os.UserHomeDir()
	config := os.Getenv("XDG_CONFIG_HOME")
	if config == "" {
		config = filepath.Join(home, ".config")
	}
	if f, err := os.Open(filepath.Join(config, "user-dirs.dirs")); err == nil {
		defer f.Close()
		sc := bufio.NewScanner(f)
		for sc.Scan() {
			if v, ok := strings.CutPrefix(strings.TrimSpace(sc.Text()), "XDG_DOCUMENTS_DIR="); ok {
				v = strings.Trim(v, `"`)
				return strings.Replace(v, "$HOME", home, 1)
			}
		}
	}
	return filepath.Join(home, "Documents")
}
