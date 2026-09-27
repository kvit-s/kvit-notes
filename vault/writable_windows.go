//go:build windows

package vault

import (
	"os"
	"path/filepath"
)

// writable reports whether the vault's folder can be written. Windows says
// little through permissions alone, so, as the Qt app does, it tries: it
// makes .kvit and a short-lived file in it.
func writable(root string) bool {
	dir := filepath.Join(root, ".kvit")
	if err := os.MkdirAll(dir, 0o755); err != nil {
		return false
	}
	f, err := os.CreateTemp(dir, "write-probe-")
	if err != nil {
		return false
	}
	name := f.Name()
	_ = f.Close()
	_ = os.Remove(name)
	return true
}
