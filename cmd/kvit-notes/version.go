package main

import "runtime/debug"

// baseVersion is the version this source is being released as: three
// numbers, without a pre-release suffix. The packaging scripts read it from
// this line (packaging/lib.sh), and a release tag must start with it: v2.0.0
// or v2.0.0-rc1 for 2.0.0.
const baseVersion = "2.0.0"

// version is the full release version, such as 2.0.0 or 2.0.0-rc1. The
// packaging scripts set it when they build a release:
//
//	go build -ldflags "-X main.version=2.0.0-rc1" ./cmd/kvit-notes
//
// It is empty in every other build (./build.sh, go build, the tests).
var version string

// appVersion is the version the program reports: the release version it was
// packaged as, or for any other build the base version marked as a
// development build, with the commit Go recorded when there is one
// (2.0.0-dev+5e1c1ff).
func appVersion() string {
	if version != "" {
		return version
	}
	v := baseVersion + "-dev"
	if info, ok := debug.ReadBuildInfo(); ok {
		for _, s := range info.Settings {
			if s.Key == "vcs.revision" && len(s.Value) >= 7 {
				v += "+" + s.Value[:7]
			}
		}
	}
	return v
}
