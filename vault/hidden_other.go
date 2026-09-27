//go:build !windows

package vault

import "io/fs"

// hiddenOnDisk is false where only a leading dot hides a file.
func hiddenOnDisk(fs.DirEntry) bool { return false }
