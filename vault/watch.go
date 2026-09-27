package vault

// Noticing changes made by other programs: the vault's folders are watched,
// and what changed is reported in batches, a moment after the changes stop,
// as the Qt app's FileWatcher does (src/platform/filewatcher.cpp). The
// vault's own writes are told apart by their content rather than by
// timing: a note whose text is what this app last wrote or read is
// unchanged.

import (
	"os"
	"path/filepath"
	"slices"
	"strings"
	"sync"
	"time"

	"github.com/fsnotify/fsnotify"
)

// batchDelay is how long after the last change a batch is reported.
const batchDelay = 400 * time.Millisecond

// Watch reports changed vault paths to changed, from another goroutine,
// until stop is called.
func (v *Vault) Watch(changed func(paths []string)) (stop func(), err error) {
	w, err := fsnotify.NewWatcher()
	if err != nil {
		return nil, err
	}
	add := func(dir string) { _ = w.Add(dir) }
	add(v.Root)
	for _, f := range v.Folders {
		add(v.abs(f.Path))
	}
	var mu sync.Mutex
	pending := map[string]bool{}
	var timer *time.Timer
	flush := func() {
		mu.Lock()
		paths := make([]string, 0, len(pending))
		for p := range pending {
			paths = append(paths, p)
		}
		clear(pending)
		mu.Unlock()
		slices.Sort(paths)
		if len(paths) > 0 {
			changed(paths)
		}
	}
	done := make(chan struct{})
	go func() {
		for {
			select {
			case <-done:
				return
			case ev, ok := <-w.Events:
				if !ok {
					return
				}
				rel, err := filepath.Rel(v.Root, ev.Name)
				if err != nil {
					continue
				}
				rel = filepath.ToSlash(rel)
				if hiddenPath(rel) {
					continue
				}
				if ev.Has(fsnotify.Create) {
					if info, err := os.Lstat(ev.Name); err == nil && info.IsDir() {
						add(ev.Name)
					}
				}
				mu.Lock()
				pending[rel] = true
				if timer == nil {
					timer = time.AfterFunc(batchDelay, flush)
				} else {
					timer.Reset(batchDelay)
				}
				mu.Unlock()
			case <-w.Errors:
			}
		}
	}()
	return func() {
		close(done)
		_ = w.Close()
	}, nil
}

// hiddenPath reports whether a vault path is inside something the scan
// leaves out: a hidden entry or the vault's own directories.
func hiddenPath(rel string) bool {
	parts := strings.Split(rel, "/")
	for _, p := range parts {
		if strings.HasPrefix(p, ".") {
			return true
		}
	}
	return controlDirs[parts[0]]
}

// Refresh brings the vault up to date after other programs changed paths
// in it, and reports which notes' texts changed (not counting ones this app
// wrote). A changed folder reads the tree again; a changed note is read
// again.
func (v *Vault) Refresh(paths []string) (changedNotes []string) {
	structural := false
	for _, rel := range paths {
		info, err := os.Lstat(v.abs(rel))
		isNote := strings.EqualFold(filepath.Ext(rel), ".md")
		switch {
		case err != nil && isNote:
			// A note that is gone.
			if e := v.Find(rel); e != nil {
				v.Entries = slices.DeleteFunc(v.Entries, func(x *Entry) bool { return x == e })
				changedNotes = append(changedNotes, rel)
			}
		case err != nil || info.IsDir():
			structural = true
		case isNote && info.Mode().IsRegular():
			data, err := os.ReadFile(v.abs(rel))
			if err != nil {
				continue
			}
			if v.written[rel] == string(data) {
				continue
			}
			v.written[rel] = string(data)
			e := v.Find(rel)
			if e == nil {
				e = &Entry{Note: Note{Path: rel, Title: strings.TrimSuffix(filepath.Base(rel), filepath.Ext(rel)), Folder: Parent(rel)}}
				v.Entries = append(v.Entries, e)
				v.sortEntries()
			}
			e.Modified, e.Size = info.ModTime(), info.Size()
			v.fill(e, parsePage(string(data)))
			changedNotes = append(changedNotes, rel)
		}
	}
	if structural {
		_ = v.Rescan()
	}
	return changedNotes
}
