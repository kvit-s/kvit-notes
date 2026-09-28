//go:build !windows

package main

// useParentConsole has nothing to do outside Windows: a program started
// from a terminal writes to it.
func useParentConsole() {}
