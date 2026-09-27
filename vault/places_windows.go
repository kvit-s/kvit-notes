//go:build windows

package vault

import (
	"os"
	"path/filepath"

	"golang.org/x/sys/windows"
)

// documentsDir is the reader's Documents folder, wherever Windows keeps it
// (it may be moved into OneDrive).
func documentsDir() string {
	if p, err := windows.KnownFolderPath(windows.FOLDERID_Documents, 0); err == nil {
		return p
	}
	home, _ := os.UserHomeDir()
	return filepath.Join(home, "Documents")
}
