package vault

import (
	"io/fs"
	"syscall"
)

// hiddenOnDisk reports whether Windows marks a file or folder hidden, which
// the scan leaves out as the app's does (directory without directory::Hidden).
func hiddenOnDisk(d fs.DirEntry) bool {
	info, err := d.Info()
	if err != nil {
		return false
	}
	if a, ok := info.Sys().(*syscall.Win32FileAttributeData); ok {
		return a.FileAttributes&syscall.FILE_ATTRIBUTE_HIDDEN != 0
	}
	return false
}
