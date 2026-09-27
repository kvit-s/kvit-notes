//go:build !windows

package vault

import (
	"errors"
	"os"

	"golang.org/x/sys/unix"
)

// lockFile takes flock's exclusive lock without waiting, as the Qt app
// does; fcntl locks would not exclude it. held reports that another process
// has it.
func lockFile(f *os.File) (held bool, err error) {
	err = unix.Flock(int(f.Fd()), unix.LOCK_EX|unix.LOCK_NB)
	if errors.Is(err, unix.EWOULDBLOCK) {
		return true, nil
	}
	return false, err
}
