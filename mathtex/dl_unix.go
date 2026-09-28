//go:build !windows

package mathtex

import (
	"github.com/ebitengine/purego"
)

// openLibrary loads the shared library at path through the system's dynamic
// loader, which purego reaches without cgo.
func openLibrary(path string) (uintptr, error) {
	return purego.Dlopen(path, purego.RTLD_NOW|purego.RTLD_LOCAL)
}

// symbol is the address of a function the library exports.
func symbol(lib uintptr, name string) (uintptr, error) {
	return purego.Dlsym(lib, name)
}

// call calls a C function with word-sized arguments. The directive makes any
// pointer converted to uintptr in the arguments stay on the heap and alive
// until the call returns, as purego.SyscallN does for its own arguments.
//
//go:uintptrescapes
func call(fn uintptr, args ...uintptr) uintptr {
	r, _, _ := purego.SyscallN(fn, args...)
	return r
}

// newCallback makes a C function pointer that calls fn.
func newCallback(fn func(*textRequest) uintptr) uintptr {
	return purego.NewCallback(fn)
}
