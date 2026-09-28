package mathtex

import (
	"errors"
	"fmt"
	"image"
	"io"
	"os"
	"path/filepath"
	"runtime"
	"strings"

	"github.com/richardwilkes/unison"
)

// libraryName is the library's file name on this system.
func libraryName() string {
	switch runtime.GOOS {
	case "windows":
		return "kvitmath.dll"
	case "darwin":
		return "libkvitmath.dylib"
	}
	return "libkvitmath.so"
}

// errLibraryMissing is the error when no library file is found at all, as
// against one that is found and fails to load.
var errLibraryMissing = errors.New("the math library was not found")

// locate finds the math library file and its resource directory, the fonts
// and XML files MicroTeX reads (a copy of third_party/microtex/res), as the Qt
// app's MathRenderer::resourceRoot finds the resources. Each is the first
// that exists of, in this order:
//
//  1. The environment: KVIT_MATH_LIB names the library file and
//     KVIT_MATH_RES the resource directory. A name that does not exist is an
//     error, not skipped.
//  2. Beside the executable: kvitmath.dll, libkvitmath.dylib or
//     libkvitmath.so, and math-res/. The Windows installer, the portable zip,
//     and build.sh's build/<os>-<arch>/ and D: copies.
//  3. A macOS bundle, with the executable in Contents/MacOS:
//     Contents/Frameworks/libkvitmath.dylib and Contents/Resources/math-res.
//  4. The Linux FHS layout of the AppImage, Flatpak and AUR packages, with
//     the executable in <prefix>/bin: <prefix>/lib/kvit-notes/libkvitmath.so
//     and <prefix>/share/kvit-notes/math-res.
//  5. This repository, for tests and development: build/<library> and
//     third_party/microtex/res of the checkout holding the working directory
//     (go test, go run), or of the one whose build/ holds the executable
//     (build/kvit-notes).
//
// A resource directory counts only when it holds a fonts folder. Links in
// the executable's path are resolved first, so a link to the program from
// /usr/bin finds the layout of the real one.
func locate() (lib, res string, err error) {
	wd, _ := os.Getwd()
	return locateFrom(exeDir(), wd, os.Getenv)
}

// locateFrom is locate for a program in the folder exe, run in the folder
// wd, with the environment getenv.
func locateFrom(exe, wd string, getenv func(string) string) (lib, res string, err error) {
	name := libraryName()
	type place struct{ lib, res string }
	var places []place
	if exe != "" {
		places = append(places,
			place{filepath.Join(exe, name), filepath.Join(exe, "math-res")},
			place{filepath.Join(exe, "..", "Frameworks", name), filepath.Join(exe, "..", "Resources", "math-res")},
			place{filepath.Join(exe, "..", "lib", "kvit-notes", name), filepath.Join(exe, "..", "share", "kvit-notes", "math-res")})
	}
	if root := checkout(exe, wd); root != "" {
		places = append(places, place{filepath.Join(root, "build", name), filepath.Join(root, "third_party", "microtex", "res")})
	}

	var libErr, resErr error
	if lib = getenv("KVIT_MATH_LIB"); lib != "" && !isFile(lib) {
		libErr = fmt.Errorf("KVIT_MATH_LIB names %s, which is not a file", lib)
	}
	// MicroTeX's metrics are compiled in, so it starts on any folder, and one
	// without the fonts would lay formulas out and draw nothing.
	if res = getenv("KVIT_MATH_RES"); res != "" && !isDir(filepath.Join(res, "fonts")) {
		resErr = fmt.Errorf("KVIT_MATH_RES names %s, which has no fonts folder", res)
	}
	var libsTried, resTried []string
	for _, p := range places {
		if lib == "" {
			if isFile(p.lib) {
				lib = filepath.Clean(p.lib)
			}
			libsTried = append(libsTried, filepath.Clean(p.lib))
		}
		if res == "" {
			if isDir(filepath.Join(p.res, "fonts")) {
				res = filepath.Clean(p.res)
			}
			resTried = append(resTried, filepath.Clean(p.res))
		}
	}
	if res != "" {
		if abs, err := filepath.Abs(res); err == nil {
			res = abs
		}
	}
	if lib == "" && libErr == nil {
		libErr = fmt.Errorf("%w: looked for %s", errLibraryMissing, strings.Join(libsTried, ", "))
	}
	if res == "" && resErr == nil {
		resErr = fmt.Errorf("the math resources were not found: looked for %s", strings.Join(resTried, ", "))
	}
	if libErr != nil {
		return "", res, libErr
	}
	if resErr != nil {
		return lib, "", resErr
	}
	return lib, res, nil
}

// exeDir is the folder holding the running program, with links resolved.
func exeDir() string {
	exe, err := os.Executable()
	if err != nil {
		return ""
	}
	if resolved, err := filepath.EvalSymlinks(exe); err == nil {
		exe = resolved
	}
	return filepath.Dir(exe)
}

// checkout is the kvit-notes-go checkout holding wd, or whose build/ holds
// the executable, or "". Only the folder above the executable is looked at,
// so a package staged somewhere inside the checkout does not find it.
func checkout(exe, wd string) string {
	isCheckout := func(dir string) bool {
		return isDir(filepath.Join(dir, "third_party", "microtex", "res", "fonts")) &&
			isFile(filepath.Join(dir, "mathtex", "native", "kvitmath.h"))
	}
	for dir := wd; dir != ""; {
		if isCheckout(dir) {
			return dir
		}
		parent := filepath.Dir(dir)
		if parent == dir {
			break
		}
		dir = parent
	}
	if exe != "" && isCheckout(filepath.Dir(exe)) {
		return filepath.Dir(exe)
	}
	return ""
}

func isFile(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.Mode().IsRegular()
}

func isDir(path string) bool {
	st, err := os.Stat(path)
	return err == nil && st.IsDir()
}

// SelfTest is the check a package's build runs on the layout it made, as
// the Qt app's `kvit-notes --math-selftest` (MathRenderer::runSelfTest) was:
// it finds the library and the resources, draws one formula into an image,
// and writes where they were found and the outcome to w:
//
//	math-lib: <library file>
//	math-res: <resource directory>
//	selftest: OK (<width>x<height>)
//
// or "selftest: FAIL (<why>)". KVIT_MATH_SELFTEST_TEX replaces the formula,
// to check a particular alphabet. It returns the exit code: 0 when the
// formula was drawn. Run it from outside the checkout, so the repository's
// own library cannot stand in for a missing one.
func SelfTest(w io.Writer) int {
	lib, res, _ := locate()
	fmt.Fprintf(w, "math-lib: %s\nmath-res: %s\n", lib, res)
	tex := os.Getenv("KVIT_MATH_SELFTEST_TEX")
	if tex == "" {
		tex = `\frac{a}{b} + \sqrt{x^2}`
	}
	fail := func(err error) int {
		fmt.Fprintf(w, "selftest: FAIL (%v)\n", err)
		return 1
	}
	f, err := Render(tex, 20, true)
	if err != nil {
		return fail(err)
	}
	for _, c := range f.cmds {
		if c.op == opGlyph && (c.font == nil || c.font.tf == nil) {
			path := ""
			if c.font != nil {
				path = c.font.path
			}
			return fail(fmt.Errorf("the font file %s could not be read", path))
		}
	}
	img, err := Image(tex, 20, unison.Black, 1, 2, 0, true)
	if err != nil {
		return fail(err)
	}
	if img == nil || !anyInk(img) {
		return fail(errors.New("the formula drew nothing"))
	}
	fmt.Fprintf(w, "selftest: OK (%dx%d)\n", img.Bounds().Dx(), img.Bounds().Dy())
	return 0
}

func anyInk(img *image.NRGBA) bool {
	for i := 3; i < len(img.Pix); i += 4 {
		if img.Pix[i] > 0 {
			return true
		}
	}
	return false
}
