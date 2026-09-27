package vault

// The vault lock, shared with the Qt app: an exclusive lock on
// .kvit/vault.lock held for as long as the vault is open, so the Qt and Go
// apps never have one vault open at once (src/repository/vaultlock.cpp).
// While it holds the lock the holder writes who it is into the file, which is
// what the refusal the other app gives says.

import (
	"encoding/json"
	"errors"
	"fmt"
	"os"
	"path/filepath"
	"time"
)

// ErrLocked is returned by Open when another program has the vault open.
var ErrLocked = errors.New("the vault is open in another program")

// lockHolder is what the lock file says about its holder.
type lockHolder struct {
	Application string `json:"application"`
	Host        string `json:"host"`
	PID         int    `json:"pid"`
	Since       string `json:"since"`
}

// takeLock locks .kvit/vault.lock, returning the open file that holds the
// lock, or ErrLocked wrapped with what the file says about the holder.
func takeLock(root string) (*os.File, error) {
	path := filepath.Join(root, ".kvit", "vault.lock")
	f, err := os.OpenFile(path, os.O_RDWR|os.O_CREATE, 0o644)
	if err != nil {
		return nil, err
	}
	held, err := lockFile(f)
	if err != nil {
		f.Close()
		return nil, err
	}
	if held {
		var who lockHolder
		data, _ := os.ReadFile(path)
		f.Close()
		if json.Unmarshal(data, &who) == nil && who.Application != "" {
			return nil, fmt.Errorf("%w: %s, process %d on %s, since %s", ErrLocked, who.Application, who.PID, who.Host, who.Since)
		}
		return nil, ErrLocked
	}
	host, _ := os.Hostname()
	data, _ := json.Marshal(lockHolder{Application: "Kvit Notes", Host: host, PID: os.Getpid(),
		Since: time.Now().Format("2006-01-02T15:04:05")})
	if err := f.Truncate(0); err == nil {
		_, _ = f.WriteAt(data, 0)
	}
	return f, nil
}
