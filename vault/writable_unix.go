//go:build !windows

package vault

import "golang.org/x/sys/unix"

// writable reports whether the vault's folder can be written.
func writable(root string) bool { return unix.Access(root, unix.W_OK) == nil }
