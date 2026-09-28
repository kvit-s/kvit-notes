package mathtex

import (
	"syscall"

	"golang.org/x/sys/windows"
)

// openLibrary loads the DLL at path. LOAD_WITH_ALTERED_SEARCH_PATH looks for
// the DLL's own dependencies beside it rather than beside the program; it
// needs only Windows' own DLLs, but a copy of one of them in the program's
// folder is then never picked up by mistake.
func openLibrary(path string) (uintptr, error) {
	h, err := windows.LoadLibraryEx(path, 0, windows.LOAD_WITH_ALTERED_SEARCH_PATH)
	return uintptr(h), err
}

// symbol is the address of a function the DLL exports.
func symbol(lib uintptr, name string) (uintptr, error) {
	return windows.GetProcAddress(windows.Handle(lib), name)
}

// call calls a C function with word-sized arguments. The directive makes any
// pointer converted to uintptr in the arguments stay on the heap and alive
// until the call returns, as syscall.SyscallN does for its own arguments.
//
//go:uintptrescapes
func call(fn uintptr, args ...uintptr) uintptr {
	r, _, _ := syscall.SyscallN(fn, args...)
	return r
}

// newCallback makes a C function pointer that calls fn.
func newCallback(fn func(*textRequest) uintptr) uintptr {
	return syscall.NewCallback(fn)
}
