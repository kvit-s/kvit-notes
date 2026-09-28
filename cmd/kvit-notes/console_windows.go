package main

// A release build for Windows is a GUI program, linked with -H windowsgui
// (packaging/lib.sh), which Windows starts without a console: what
// --version, --math-selftest and --help print would go nowhere when the
// program is run from a terminal. useParentConsole attaches the program to
// the console of the terminal it was started from, as Go GUI programs do,
// and points the standard output and error that go nowhere at it. Output
// that already goes somewhere, to a console, a file or a pipe (as from WSL),
// is left alone.
//
// The terminal does not wait for a GUI program, so its prompt can come back
// before the lines are printed; `kvit-notes --version | more` in cmd, or
// piping to Out-Host in PowerShell, waits.

import (
	"os"

	"golang.org/x/sys/windows"
)

var procAttachConsole = windows.NewLazySystemDLL("kernel32.dll").NewProc("AttachConsole")

// attachParentProcess is ATTACH_PARENT_PROCESS, (DWORD)-1.
const attachParentProcess = 0xFFFFFFFF

// goesNowhere reports whether a standard handle has nothing behind it.
func goesNowhere(which uint32) bool {
	h, err := windows.GetStdHandle(which)
	if err != nil || h == 0 || h == windows.InvalidHandle {
		return true
	}
	_, err = windows.GetFileType(h)
	return err != nil
}

func useParentConsole() {
	out, errs := goesNowhere(windows.STD_OUTPUT_HANDLE), goesNowhere(windows.STD_ERROR_HANDLE)
	if !out && !errs {
		return
	}
	if r, _, _ := procAttachConsole.Call(attachParentProcess); r == 0 {
		return
	}
	name, _ := windows.UTF16PtrFromString("CONOUT$")
	h, err := windows.CreateFile(name, windows.GENERIC_READ|windows.GENERIC_WRITE,
		windows.FILE_SHARE_READ|windows.FILE_SHARE_WRITE, nil, windows.OPEN_EXISTING, 0, 0)
	if err != nil {
		return
	}
	console := os.NewFile(uintptr(h), "CONOUT$")
	if out {
		os.Stdout = console
		_ = windows.SetStdHandle(windows.STD_OUTPUT_HANDLE, h)
	}
	if errs {
		os.Stderr = console
		_ = windows.SetStdHandle(windows.STD_ERROR_HANDLE, h)
	}
}
