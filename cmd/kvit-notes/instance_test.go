package main

import (
	"path/filepath"
	"testing"
	"time"
)

func TestASecondCopyHandsOverToTheFirst(t *testing.T) {
	sock := filepath.Join(t.TempDir(), "instance.sock")
	if handOff(sock, "/x") {
		t.Fatalf("nothing is running yet")
	}
	got := make(chan string, 1)
	stop, err := listen(sock, func(r instanceRequest) { got <- r.Open })
	if err != nil {
		t.Fatal(err)
	}
	if !handOff(sock, "/notes/vault") {
		t.Fatalf("the running copy should answer")
	}
	select {
	case p := <-got:
		if p != "/notes/vault" {
			t.Errorf("handed over %q", p)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("no request arrived")
	}
	stop()
	if handOff(sock, "/x") {
		t.Errorf("a stopped copy must not answer")
	}
	// A socket file left behind is replaced.
	stop, err = listen(sock, func(instanceRequest) {})
	if err != nil {
		t.Fatal(err)
	}
	stop()
}
