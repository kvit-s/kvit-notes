package main

// One running copy: the first copy listens on a socket in the settings
// folder; a copy started after it hands over what it was asked to open, a
// folder, a file or nothing, and exits, and the first copy opens it in a
// window of its own, or brings forward the window it is already open in.

import (
	"bufio"
	"encoding/json"
	"errors"
	"net"
	"os"
	"path/filepath"
	"time"
)

// instanceRequest is what a later copy asks the running one to do.
type instanceRequest struct {
	// Open is a vault folder or a note file, as an absolute path, or ""
	// to bring the app forward.
	Open string `json:"open"`
}

// socketPath is where the running copy listens.
func socketPath() string {
	return filepath.Join(filepath.Dir(settingsPath()), "instance.sock")
}

// handOff asks a running copy to open a path, and reports whether one did.
func handOff(sock, open string) bool {
	conn, err := net.DialTimeout("unix", sock, time.Second)
	if err != nil {
		return false
	}
	defer conn.Close()
	_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
	if err := json.NewEncoder(conn).Encode(instanceRequest{Open: open}); err != nil {
		return false
	}
	reply, err := bufio.NewReader(conn).ReadString('\n')
	return err == nil && reply == "ok\n"
}

// listen makes this copy the running one: requests from later copies are
// given to serve, one at a time. A socket left by a copy that ended without
// removing it is replaced. The returned function stops listening.
func listen(sock string, serve func(instanceRequest)) (func(), error) {
	_ = os.MkdirAll(filepath.Dir(sock), 0o755)
	l, err := net.Listen("unix", sock)
	if err != nil {
		// Nothing answered handOff, so the socket is stale.
		_ = os.Remove(sock)
		if l, err = net.Listen("unix", sock); err != nil {
			return nil, err
		}
	}
	go func() {
		for {
			conn, err := l.Accept()
			if errors.Is(err, net.ErrClosed) {
				return
			}
			if err != nil {
				continue
			}
			go func() {
				defer conn.Close()
				_ = conn.SetDeadline(time.Now().Add(5 * time.Second))
				var req instanceRequest
				if json.NewDecoder(conn).Decode(&req) != nil {
					return
				}
				serve(req)
				_, _ = conn.Write([]byte("ok\n"))
			}()
		}
	}()
	return func() { l.Close(); os.Remove(sock) }, nil
}
