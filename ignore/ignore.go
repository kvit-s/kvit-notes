// Package ignore decides which files and folders a walk over a vault leaves
// out. The scan, the file watcher and the file tree all use the same rules,
// so a file excluded from one is excluded from all.
//
// The patterns use gitignore syntax and come from three places, applied in
// this order:
//   - git's exclude file, .git/info/exclude, when the vault folder is a git
//     repository or a git worktree;
//   - the .gitignore file in the vault folder and in each folder below it,
//     each applying to the paths under its own folder;
//   - a list of patterns the user sets for the vault, kept in the app's
//     settings under SettingsKey, for folders that are not repositories or
//     that need exclusions beyond git's.
//
// A path is excluded when the last pattern that matches it is not a
// negation (a line starting with "!"), as in git. Because the settings list
// is applied last, it can exclude what a .gitignore includes again.
//
// A walk takes a Snapshot from Rules. A Snapshot is an immutable value that
// can be handed to another goroutine. The walk tests each entry with
// IsExcluded before entering it, and on entering a folder calls
// WithDirectory, which reads that folder's .gitignore and returns the rules
// its children are tested against. An excluded folder is therefore never
// opened to look for more rules.
//
// Relative paths are relative to the vault folder and use "/"; a backslash
// is also taken as a separator on Windows. Absolute paths returned by this
// package use "/" on every system, since the settings list is keyed by the
// vault folder's path in that form.
package ignore

import (
	"bufio"
	"maps"
	"os"
	"path"
	"path/filepath"
	"slices"
	"strconv"
	"strings"
	"sync"
)

// SettingsKey is the settings key under which the additional patterns are
// kept. Its value is an object mapping each vault folder, in the form
// Rules.RootPath returns, to that vault's list:
//
//	"vault.ignorePatternsByRoot": {"/home/me/Notes": ["node_modules/", "dist/**"]}
const SettingsKey = "vault.ignorePatternsByRoot"

// Rules is one vault's exclusion policy: the vault folder and the additional
// patterns from the settings. The ignore files themselves are read by
// Snapshot, so a changed .gitignore applies from the next Snapshot on. The
// methods may be called from any goroutine. The zero value has no vault
// folder and excludes nothing.
type Rules struct {
	mu                 sync.Mutex
	rootPath           string
	additionalPatterns []string
	revision           int
}

// New returns the rules for the vault folder root with the additional
// patterns from the settings, normalized as SetAdditionalPatterns does.
func New(root string, additional []string) *Rules {
	return &Rules{
		rootPath:           normalizedRoot(root),
		additionalPatterns: normalizedPatterns(additional),
	}
}

// RootPath returns the vault folder: absolute, cleaned, with "/" as the
// separator and, on Windows, an upper-case drive letter.
func (r *Rules) RootPath() string {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.rootPath
}

// SetRootPath moves the rules to the vault folder root, whose additional
// patterns are additional (the caller reads them from the settings with
// LoadAdditionalPatterns). It reports whether the folder changed. When it did
// not, the patterns are left as they are.
//
// Revision does not change: the owner that moves the rules to another vault
// walks the new vault itself.
func (r *Rules) SetRootPath(root string, additional []string) bool {
	normalized := normalizedRoot(root)
	r.mu.Lock()
	defer r.mu.Unlock()
	if r.rootPath == normalized {
		return false
	}
	r.rootPath = normalized
	r.additionalPatterns = normalizedPatterns(additional)
	return true
}

// AdditionalPatterns returns the patterns from the settings.
func (r *Rules) AdditionalPatterns() []string {
	r.mu.Lock()
	defer r.mu.Unlock()
	return slices.Clone(r.additionalPatterns)
}

// SetAdditionalPatterns replaces the patterns from the settings. Blank
// patterns and repeated ones are dropped, and the order is kept. It reports
// whether the list changed; a change adds one to Revision, and the caller
// then saves the list with StoreAdditionalPatterns and walks the vault again.
func (r *Rules) SetAdditionalPatterns(patterns []string) bool {
	normalized := normalizedPatterns(patterns)
	r.mu.Lock()
	defer r.mu.Unlock()
	if slices.Equal(r.additionalPatterns, normalized) {
		return false
	}
	r.additionalPatterns = normalized
	r.revision++
	return true
}

// Revision counts the changes to the policy: each change of the additional
// patterns and each Reload. An owner that keeps the revision its walk used
// can tell whether the walk is out of date.
func (r *Rules) Revision() int {
	r.mu.Lock()
	defer r.mu.Unlock()
	return r.revision
}

// Reload records that a .gitignore or git's exclude file changed on disk.
// Snapshots read those files afresh, so all it does is add one to Revision.
func (r *Rules) Reload() {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.revision++
}

// IsExcluded reports whether relativePath is excluded, reading the .gitignore
// of every folder above it. It is for a single path, such as one a file
// watcher reports; a walk uses a Snapshot, which reads each folder's file
// once.
func (r *Rules) IsExcluded(relativePath string, isDirectory bool) bool {
	parent := path.Dir(cleanRelative(relativePath))
	rules := r.Snapshot()
	if parent != "." {
		rules = rules.ThroughDirectory(parent)
	}
	return rules.IsExcluded(relativePath, isDirectory)
}

// Snapshot reads git's exclude file and the vault folder's .gitignore and
// returns the policy for the entries of the vault folder.
func (r *Rules) Snapshot() Snapshot {
	r.mu.Lock()
	root, patterns := r.rootPath, r.additionalPatterns
	r.mu.Unlock()

	result := Snapshot{rootPath: root, gitInfoExcludePath: gitInfoExcludeForRoot(root)}
	if result.gitInfoExcludePath != "" {
		if info := readRuleFile(result.gitInfoExcludePath, ""); len(info.rules) > 0 {
			result.groups = append(result.groups, info)
		}
	}
	if rootIgnore := readRuleFile(result.IgnoreFileForDirectory(""), ""); len(rootIgnore.rules) > 0 {
		result.groups = append(result.groups, rootIgnore)
	}
	result.settings = compilePatterns(patterns, "")
	return result
}

// IsRulesFile reports whether absolutePath is git's exclude file for the
// vault or a .gitignore file anywhere below the vault folder, so that a
// change to it calls for Reload.
func (r *Rules) IsRulesFile(absolutePath string) bool {
	root := r.RootPath()
	if root == "" || absolutePath == "" {
		return false
	}
	clean := absoluteFilePath(absolutePath)
	if infoExclude := gitInfoExcludeForRoot(root); infoExclude != "" && clean == path.Clean(infoExclude) {
		return true
	}
	return strings.HasPrefix(clean, root+"/") && path.Base(clean) == ".gitignore"
}

// LoadAdditionalPatterns returns the patterns kept for the vault folder root
// in value, the settings value under SettingsKey. value is the object as
// decoded from JSON, or as returned by StoreAdditionalPatterns; anything else
// holds no patterns.
func LoadAdditionalPatterns(value any, root string) []string {
	root = normalizedRoot(root)
	if root == "" {
		return nil
	}
	roots, _ := value.(map[string]any)
	return normalizedPatterns(stringList(roots[root]))
}

// StoreAdditionalPatterns returns a copy of value, the settings value under
// SettingsKey, with the list for the vault folder root replaced by patterns,
// or removed when there are none. The lists of other vaults are kept. With an
// empty root the copy is returned unchanged.
func StoreAdditionalPatterns(value any, root string, patterns []string) map[string]any {
	roots := map[string]any{}
	if old, ok := value.(map[string]any); ok {
		maps.Copy(roots, old)
	}
	root = normalizedRoot(root)
	if root == "" {
		return roots
	}
	patterns = normalizedPatterns(patterns)
	if len(patterns) == 0 {
		delete(roots, root)
		return roots
	}
	list := make([]any, len(patterns))
	for i, p := range patterns {
		list[i] = p
	}
	roots[root] = list
	return roots
}

// stringList reads a settings value as a list of strings: a list gives its
// elements, with numbers and booleans written as text, and a single string
// gives a list of one.
func stringList(v any) []string {
	switch v := v.(type) {
	case string:
		return []string{v}
	case []string:
		return slices.Clone(v)
	case []any:
		out := make([]string, 0, len(v))
		for _, x := range v {
			switch x := x.(type) {
			case string:
				out = append(out, x)
			case float64:
				out = append(out, strconv.FormatFloat(x, 'g', -1, 64))
			case bool:
				out = append(out, strconv.FormatBool(x))
			}
		}
		return out
	}
	return nil
}

// Snapshot is the policy at one folder of a walk: the rules of git's exclude
// file, of every .gitignore from the vault folder down to this folder, and of
// the settings. It is an immutable value; WithDirectory and ThroughDirectory
// return new ones. The zero value excludes nothing.
type Snapshot struct {
	rootPath           string
	gitInfoExcludePath string
	groups             []ruleGroup
	settings           ruleGroup
}

// RootPath returns the vault folder the snapshot was taken for.
func (s Snapshot) RootPath() string { return s.rootPath }

// GitInfoExcludePath returns the path of git's exclude file for the vault,
// whether or not the file exists, or "" when the vault folder is not a git
// repository or worktree. A watcher watches it for changes.
func (s Snapshot) GitInfoExcludePath() string { return s.gitInfoExcludePath }

// IsExcluded reports whether relativePath is excluded. isDirectory says
// whether the entry is a folder, which a pattern ending in "/" needs to match
// the entry itself. The vault folder and paths outside it are never excluded.
func (s Snapshot) IsExcluded(relativePath string, isDirectory bool) bool {
	cleaned := cleanRelative(relativePath)
	if cleaned == "" || cleaned == ".." || strings.HasPrefix(cleaned, "../") {
		return false
	}

	excluded := false
	apply := func(group *ruleGroup) {
		below := pathBelowBase(cleaned, group.baseDir)
		if below == "" {
			return
		}
		for _, r := range group.rules {
			if r.matches(below, isDirectory) {
				excluded = !r.negated
			}
		}
	}
	for i := range s.groups {
		apply(&s.groups[i])
	}
	// The settings are exclusions the user added, so they have the final
	// say after every ignore file of the project, including nested ones.
	apply(&s.settings)
	return excluded
}

// WithDirectory adds the .gitignore of relativeDir and returns the policy for
// that folder's children. A walk calls it on entering each folder, one level
// at a time. The vault folder's own .gitignore is already in every snapshot,
// so "" returns s unchanged.
func (s Snapshot) WithDirectory(relativeDir string) Snapshot {
	cleaned := cleanRelative(relativeDir)
	if cleaned == "" {
		return s
	}
	group := readRuleFile(s.IgnoreFileForDirectory(cleaned), cleaned)
	if len(group.rules) > 0 {
		// Clip so that two folders entered from the same snapshot never
		// append into one shared array.
		s.groups = append(slices.Clip(s.groups), group)
	}
	return s
}

// ThroughDirectory adds the .gitignore of every folder from the vault folder
// down to relativeDir, for a walk that starts below the vault folder, such as
// a rescan of one changed folder.
func (s Snapshot) ThroughDirectory(relativeDir string) Snapshot {
	result := s
	accumulated := ""
	for _, part := range strings.Split(cleanRelative(relativeDir), "/") {
		if part == "" {
			continue
		}
		if accumulated == "" {
			accumulated = part
		} else {
			accumulated += "/" + part
		}
		result = result.WithDirectory(accumulated)
	}
	return result
}

// IgnoreFileForDirectory returns the path of the .gitignore of relativeDir,
// whether or not it exists, or "" when the snapshot has no vault folder.
func (s Snapshot) IgnoreFileForDirectory(relativeDir string) string {
	if s.rootPath == "" {
		return ""
	}
	dir := s.rootPath
	if cleaned := cleanRelative(relativeDir); cleaned != "" {
		dir = joinPath(s.rootPath, cleaned)
	}
	return joinPath(dir, ".gitignore")
}

// readRuleFile reads an ignore file whose rules apply below baseDir. A file
// that is missing or cannot be read has no rules.
func readRuleFile(file, baseDir string) ruleGroup {
	if file == "" {
		return ruleGroup{baseDir: baseDir}
	}
	data, err := os.ReadFile(filepath.FromSlash(file))
	if err != nil {
		return ruleGroup{baseDir: baseDir}
	}
	// The file is read as UTF-8: a byte order mark is skipped, invalid
	// bytes are decoded as U+FFFD, and the "\r" of a "\r\n" line end is
	// dropped (compileRule removes it).
	text := strings.ToValidUTF8(strings.TrimPrefix(string(data), "\uFEFF"), "\uFFFD")
	text = strings.TrimSuffix(text, "\n")
	return compilePatterns(strings.Split(text, "\n"), baseDir)
}

// gitInfoExcludeForRoot returns the path of git's exclude file for the vault
// folder root, or "" when root has no .git. A .git folder holds it directly.
// A .git file, which git writes for a worktree or a submodule, names the
// folder that holds it on a first line of the form "gitdir: <path>".
func gitInfoExcludeForRoot(root string) string {
	if root == "" {
		return ""
	}
	dotGit := joinPath(root, ".git")
	info, err := os.Stat(filepath.FromSlash(dotGit))
	if err != nil {
		return ""
	}
	gitDir := ""
	if info.IsDir() {
		gitDir = absoluteFilePath(dotGit)
	} else if info.Mode().IsRegular() {
		if first := firstLine(dotGit); len(first) >= 7 && strings.EqualFold(first[:7], "gitdir:") {
			named := strings.TrimSpace(first[7:])
			if !isAbsolute(named) {
				named = joinPath(root, named)
			}
			gitDir = absoluteFilePath(named)
		}
	}
	if gitDir == "" {
		return ""
	}
	return joinPath(gitDir, "info/exclude")
}

// firstLine returns the first line of a file with the white space around
// it removed, or "" when the file cannot be read.
func firstLine(file string) string {
	f, err := os.Open(filepath.FromSlash(file))
	if err != nil {
		return ""
	}
	defer f.Close()
	line, _ := bufio.NewReader(f).ReadString('\n')
	return strings.TrimSpace(strings.ToValidUTF8(line, "\uFFFD"))
}

// cleanRelative puts a relative path in the form the rules match against: "/"
// as the separator, no leading "./", no "." or empty segments, and "" for the
// vault folder itself.
func cleanRelative(p string) string {
	p = filepath.ToSlash(p)
	for strings.HasPrefix(p, "./") {
		p = p[2:]
	}
	if p == "" {
		return ""
	}
	p = path.Clean(p)
	if p == "." {
		return ""
	}
	return p
}

// normalizedRoot returns root as an absolute, cleaned path with "/" as the
// separator, or "" for "".
func normalizedRoot(root string) string {
	if root == "" {
		return ""
	}
	return absoluteFilePath(root)
}

// normalizedPatterns drops blank patterns and repeated ones and keeps the
// order.
func normalizedPatterns(patterns []string) []string {
	var result []string
	for _, p := range patterns {
		if strings.TrimSpace(p) != "" && !slices.Contains(result, p) {
			result = append(result, p)
		}
	}
	return result
}

// absoluteFilePath returns p made absolute against the working folder and
// cleaned, with "/" as the separator. On Windows the drive letter is written
// in upper case, as Kvit Notes versions built with Qt wrote it, so that a
// settings list they saved is found under the same key.
func absoluteFilePath(p string) string {
	abs, err := filepath.Abs(filepath.FromSlash(p))
	if err != nil {
		abs = filepath.Clean(filepath.FromSlash(p))
	}
	if v := filepath.VolumeName(abs); len(v) == 2 && v[1] == ':' {
		abs = strings.ToUpper(v[:1]) + abs[1:]
	}
	return filepath.ToSlash(abs)
}

// isAbsolute reports whether p is absolute: it starts with "/", or it is
// absolute on this system, such as "C:/Notes" on Windows.
func isAbsolute(p string) bool {
	return strings.HasPrefix(p, "/") || filepath.IsAbs(filepath.FromSlash(p))
}

// joinPath returns name inside the folder dir, or name itself when it is
// absolute. The result is not cleaned.
func joinPath(dir, name string) string {
	if isAbsolute(name) {
		return name
	}
	if !strings.HasSuffix(dir, "/") {
		dir += "/"
	}
	return dir + name
}
