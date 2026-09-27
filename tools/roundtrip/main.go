// Command roundtrip reads Markdown files through the editor's parser and
// serializer and reports which come back changed, and how: the number of
// lines that differ and the first few of them. Front matter is set aside
// first, as the app does before a note reaches the editor. It never writes
// the files.
//
//	go run ./tools/roundtrip FILE-OR-DIRECTORY...
package main

import (
	"fmt"
	"io/fs"
	"os"
	"path/filepath"
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
)

func main() {
	same, changed := 0, 0
	var paths []string
	for _, arg := range os.Args[1:] {
		_ = filepath.WalkDir(arg, func(p string, d fs.DirEntry, err error) error {
			if err == nil && !d.IsDir() && strings.HasSuffix(p, ".md") {
				paths = append(paths, p)
			}
			return nil
		})
	}
	for _, path := range paths {
		data, err := os.ReadFile(path)
		if err != nil {
			fmt.Fprintln(os.Stderr, err)
			continue
		}
		src := strings.ReplaceAll(string(data), "\r\n", "\n")
		if strings.HasPrefix(src, "---\n") {
			if end := strings.Index(src[4:], "\n---\n"); end >= 0 {
				src = strings.TrimLeft(src[4+end+5:], "\n")
			}
		}
		out := editor.Serialize(editor.ParseMarkdown(src))
		if out == src {
			same++
			continue
		}
		changed++
		a, b := strings.Split(src, "\n"), strings.Split(out, "\n")
		fmt.Printf("%s: %d lines in, %d out\n", path, len(a), len(b))
		shown := 0
		for i := 0; i < max(len(a), len(b)) && shown < 3; i++ {
			var la, lb string
			if i < len(a) {
				la = a[i]
			}
			if i < len(b) {
				lb = b[i]
			}
			if la != lb {
				fmt.Printf("    line %d: %q\n         -> %q\n", i+1, trim(la), trim(lb))
				shown++
			}
		}
	}
	fmt.Printf("%d unchanged, %d changed\n", same, changed)
}

func trim(s string) string {
	if len(s) > 70 {
		return s[:70] + "…"
	}
	return s
}
