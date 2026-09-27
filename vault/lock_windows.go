//go:build windows

package vault

import (
	"errors"
	"os"

	"golang.org/x/sys/windows"
)

// lockOffset is the byte the Qt app locks: one byte far past the end of the
// file, so the lock never covers what the file says.
const lockOffset = 0x40000000

// lockFile takes the same exclusive one-byte lock the Qt app takes, without
// waiting. held reports that another process has it.
func lockFile(f *os.File) (held bool, err error) {
	ol := &windows.Overlapped{Offset: lockOffset}
	err = windows.LockFileEx(windows.Handle(f.Fd()), windows.LOCKFILE_EXCLUSIVE_LOCK|windows.LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, ol)
	if errors.Is(err, windows.ERROR_LOCK_VIOLATION) || errors.Is(err, windows.ERROR_IO_PENDING) {
		return true, nil
	}
	return false, err
}
