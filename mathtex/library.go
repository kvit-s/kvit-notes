package mathtex

import (
	"errors"
	"fmt"
	"sort"
	"strings"
	"sync"
	"time"
	"unsafe"
)

// libraryVersion is the KVITMATH_VERSION of mathtex/native/kvitmath.h this
// package speaks. A library of another version is not used.
const libraryVersion = 1

// The flags of kvitmath_render.
const (
	flagDisplay = 1
	flagDraw    = 2
)

// result is struct KvitMathResult.
type result struct {
	width, height, depth, baseline float32
	data                           *byte
	len                            int64
}

// bytes copies what the library returned; its buffer is reused by the next call.
func (r *result) bytes() []byte {
	if r.data == nil || r.len <= 0 {
		return nil
	}
	return append([]byte(nil), unsafe.Slice(r.data, r.len)...)
}

// textRequest is struct KvitMathText: text the library has no font for, which
// the measure callback measures.
type textRequest struct {
	text       *byte
	textLen    int32
	style      int32
	family     *byte
	familyLen  int32
	size       float32
	x, y, w, h float32
}

// engine is the loaded library. MicroTeX is not thread-safe, so every call
// into it holds mu.
type engine struct {
	mu       sync.Mutex
	lib      string // the library file
	res      string // the resource directory
	render   uintptr
	commands uintptr
}

var (
	loadOnce sync.Once
	loaded   *engine
	loadErr  error
	loadTime time.Duration // how long finding, loading and initialising took
)

// load finds, loads and initialises the library once.
func load() (*engine, error) {
	loadOnce.Do(func() {
		start := time.Now()
		loaded, loadErr = open()
		loadTime = time.Since(start)
	})
	return loaded, loadErr
}

// Available reports whether formulas can be rendered: the library and its
// resources were found and the engine initialised. When they were not, math
// is off, the editor shows the TeX source, and LoadError says why.
func Available() bool {
	_, err := load()
	return err == nil
}

// LoadError is why math is off, or nil when it is available.
func LoadError() error {
	_, err := load()
	return err
}

// LibraryPath is the library file in use, or "" when math is off.
func LibraryPath() string {
	if e, err := load(); err == nil {
		return e.lib
	}
	return ""
}

// ResourceRoot is the resource directory the engine was initialised with:
// the fonts and the XML files MicroTeX reads. It is "" when none was found.
func ResourceRoot() string {
	if e, err := load(); err == nil {
		return e.res
	}
	_, root, _ := locate()
	return root
}

func open() (*engine, error) {
	libPath, res, err := locate()
	if err != nil {
		return nil, err
	}
	lib, err := openLibrary(libPath)
	if err != nil {
		return nil, fmt.Errorf("math library %s could not be loaded: %w", libPath, err)
	}
	syms := map[string]uintptr{}
	for _, name := range []string{"kvitmath_version", "kvitmath_init", "kvitmath_set_measure",
		"kvitmath_render", "kvitmath_commands"} {
		if syms[name], err = symbol(lib, name); err != nil {
			return nil, fmt.Errorf("math library %s has no %s: %w", libPath, name, err)
		}
	}
	if v := int32(call(syms["kvitmath_version"])); v != libraryVersion {
		return nil, fmt.Errorf("math library %s is version %d; this program needs version %d", libPath, v, libraryVersion)
	}
	var r result
	rootBytes := []byte(res)
	// The functions return a 32-bit int; the rest of the returned word is
	// not defined, so each result is read through int32.
	if int32(call(syms["kvitmath_init"], uintptr(unsafe.Pointer(unsafe.SliceData(rootBytes))), uintptr(len(rootBytes)),
		uintptr(unsafe.Pointer(&r)))) == 0 {
		return nil, fmt.Errorf("math engine could not start on %s: %s", res, r.bytes())
	}
	call(syms["kvitmath_set_measure"], measureCallback())
	return &engine{lib: libPath, res: res, render: syms["kvitmath_render"], commands: syms["kvitmath_commands"]}, nil
}

// layout lays tex out at size pixels per em, with the drawing commands when
// draw is set. The caller has normalised tex and bounded size.
func (e *engine) layout(tex string, size int, display, draw bool) (r result, data []byte, err error) {
	flags := 0
	if display {
		flags |= flagDisplay
	}
	if draw {
		flags |= flagDraw
	}
	src := []byte(tex)
	e.mu.Lock()
	defer e.mu.Unlock()
	ok := call(e.render, uintptr(unsafe.Pointer(unsafe.SliceData(src))), uintptr(len(src)), uintptr(size),
		uintptr(flags), uintptr(unsafe.Pointer(&r)))
	data = r.bytes()
	if int32(ok) == 0 {
		msg := string(data)
		if msg == "" {
			msg = "Unrenderable expression"
		}
		return r, nil, errors.New(msg)
	}
	return r, data, nil
}

// commandNames asks the library for every command a user can type.
func (e *engine) commandNames() ([]string, error) {
	var r result
	e.mu.Lock()
	ok := call(e.commands, uintptr(unsafe.Pointer(&r)))
	data := r.bytes()
	e.mu.Unlock()
	if int32(ok) == 0 {
		return nil, errors.New(string(data))
	}
	names := strings.Fields(string(data))
	sort.Strings(names)
	return names, nil
}
