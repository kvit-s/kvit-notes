package vault

// Note templates (features.md 18.1): ordinary notes in .kvit/templates, with
// {{title}}, {{date}}, {{time}}, {{date:FORMAT}} and {{time:FORMAT}}
// filled in when a note is made from one (src/repository/notetemplates.cpp).
// Only the body, the tags and the favourite mark carry over to the new note.

import (
	"os"
	"path/filepath"
	"regexp"
	"sort"
	"strings"
	"time"
	"unicode"
)

func (v *Vault) templatesDir() string { return filepath.Join(v.Root, ".kvit", "templates") }

// badTemplateName holds the characters a template's name may not have.
const badTemplateName = `/\:*?"<>|`

// templatePath is a template's file, or "" for a name a template cannot
// have: empty, "." or "..", or holding a character a file name cannot
// (notetemplates.cpp, isValidTemplateName).
func (v *Vault) templatePath(name string) string {
	name = strings.TrimSpace(name)
	if name == "" || name == "." || name == ".." || strings.ContainsAny(name, badTemplateName) {
		return ""
	}
	for _, r := range name {
		if unicode.IsControl(r) {
			return ""
		}
	}
	return filepath.Join(v.templatesDir(), name+".md")
}

// ReadTemplate is a template's text.
func (v *Vault) ReadTemplate(name string) (string, error) {
	p := v.templatePath(name)
	if p == "" {
		return "", ErrName
	}
	data, err := os.ReadFile(p)
	return string(data), err
}

// WriteTemplate writes a template, making it when it is new.
func (v *Vault) WriteTemplate(name, text string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	p := v.templatePath(name)
	if p == "" {
		return ErrName
	}
	if err := os.MkdirAll(v.templatesDir(), 0o755); err != nil {
		return err
	}
	return writeAtomic(p, []byte(text))
}

// DeleteTemplate deletes a template.
func (v *Vault) DeleteTemplate(name string) error {
	if v.ReadOnly {
		return ErrReadOnly
	}
	p := v.templatePath(name)
	if p == "" {
		return ErrName
	}
	return os.Remove(p)
}

// TemplateNames are the vault's templates, in name order ignoring case.
func (v *Vault) TemplateNames() []string {
	entries, _ := os.ReadDir(v.templatesDir())
	var names []string
	for _, e := range entries {
		if !e.IsDir() && strings.EqualFold(filepath.Ext(e.Name()), ".md") {
			names = append(names, strings.TrimSuffix(e.Name(), filepath.Ext(e.Name())))
		}
	}
	sort.Slice(names, func(a, b int) bool { return strings.ToLower(names[a]) < strings.ToLower(names[b]) })
	return names
}

// SeedTemplates writes Kvit's three built-in templates when the vault has
// none, and never over templates the reader has.
func (v *Vault) SeedTemplates() {
	if v.ReadOnly || len(v.TemplateNames()) > 0 {
		return
	}
	if err := os.MkdirAll(v.templatesDir(), 0o755); err != nil {
		return
	}
	builtins := map[string]string{
		"Meeting Notes": "---\ntags: [meeting]\n---\n# {{title}}\n\n**Date:** {{date}}  \n**Time:** {{time}}\n\n" +
			"## Attendees\n\n- \n\n## Agenda\n\n1. \n\n## Notes\n\n\n\n## Action items\n\n- [ ] \n",
		"Project Plan": "# {{title}}\n\n*Started {{date}}.*\n\n## Goal\n\n\n\n## Milestones\n\n- [ ] \n\n" +
			"## Risks\n\n| Risk | Mitigation |\n| --- | --- |\n|  |  |\n",
		"Daily Journal": "---\ntags: [journal]\n---\n# {{date:dddd, MMMM d, yyyy}}\n\n" +
			"## Three good things\n\n1. \n2. \n3. \n\n## Today\n\n\n\n## Notes\n\n\n",
	}
	for name, text := range builtins {
		_ = writeAtomic(filepath.Join(v.templatesDir(), name+".md"), []byte(text))
	}
}

// Instance is what a template gives a new note.
type Instance struct {
	Body     string
	Tags     []string
	Favorite bool
}

// Instantiate fills a template in for a note of a title at a time.
func (v *Vault) Instantiate(name, title string, now time.Time) (Instance, error) {
	text, err := v.ReadTemplate(name)
	if err != nil {
		return Instance{}, err
	}
	p := parsePage(ExpandTemplate(text, title, now))
	return Instance{Body: p.Body, Tags: p.Tags(), Favorite: p.Favorite()}, nil
}

var reToken = regexp.MustCompile(`\{\{\s*([a-zA-Z]+)(?::([^}]*))?\s*\}\}`)

// ExpandTemplate fills a template's placeholders in; an unknown one is
// left as written.
func ExpandTemplate(text, title string, now time.Time) string {
	return reToken.ReplaceAllStringFunc(text, func(m string) string {
		sm := reToken.FindStringSubmatch(m)
		arg := sm[2]
		switch strings.ToLower(sm[1]) {
		case "date":
			if arg == "" {
				return now.Format("2006-01-02")
			}
			return QtTimeFormat(now, arg)
		case "time":
			if arg == "" {
				return now.Format("15:04")
			}
			return QtTimeFormat(now, arg)
		case "title":
			return title
		}
		return m
	})
}

// qtTokens are the Qt date and time format letters, longest first, and how
// each is written.
var qtTokens = []struct {
	token string
	write func(t time.Time) string
}{
	{"dddd", func(t time.Time) string { return t.Format("Monday") }},
	{"ddd", func(t time.Time) string { return t.Format("Mon") }},
	{"dd", func(t time.Time) string { return t.Format("02") }},
	{"d", func(t time.Time) string { return t.Format("2") }},
	{"MMMM", func(t time.Time) string { return t.Format("January") }},
	{"MMM", func(t time.Time) string { return t.Format("Jan") }},
	{"MM", func(t time.Time) string { return t.Format("01") }},
	{"M", func(t time.Time) string { return t.Format("1") }},
	{"yyyy", func(t time.Time) string { return t.Format("2006") }},
	{"yy", func(t time.Time) string { return t.Format("06") }},
	{"HH", func(t time.Time) string { return t.Format("15") }},
	{"H", func(t time.Time) string { return t.Format("15") }},
	{"hh", func(t time.Time) string { return t.Format("03") }},
	{"h", func(t time.Time) string { return t.Format("3") }},
	{"mm", func(t time.Time) string { return t.Format("04") }},
	{"m", func(t time.Time) string { return t.Format("4") }},
	{"ss", func(t time.Time) string { return t.Format("05") }},
	{"s", func(t time.Time) string { return t.Format("5") }},
	{"AP", func(t time.Time) string { return t.Format("PM") }},
	{"ap", func(t time.Time) string { return t.Format("pm") }},
}

// QtTimeFormat writes a time with a Qt date format string ("dddd, MMMM d,
// yyyy"), quoted text in single quotes kept as it is.
func QtTimeFormat(t time.Time, format string) string {
	var b strings.Builder
	for i := 0; i < len(format); {
		if format[i] == '\'' {
			end := strings.IndexByte(format[i+1:], '\'')
			if end < 0 {
				b.WriteString(format[i+1:])
				break
			}
			if end == 0 {
				b.WriteByte('\'')
			} else {
				b.WriteString(format[i+1 : i+1+end])
			}
			i += end + 2
			continue
		}
		matched := false
		for _, tk := range qtTokens {
			if strings.HasPrefix(format[i:], tk.token) {
				b.WriteString(tk.write(t))
				i += len(tk.token)
				matched = true
				break
			}
		}
		if !matched {
			b.WriteByte(format[i])
			i++
		}
	}
	return b.String()
}
