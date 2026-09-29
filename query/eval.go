package query

import (
	"slices"
	"sort"
	"strconv"
	"strings"
	"time"
)

// Note is one note of the collection as a query reads it: the parts of the
// app's NoteCollection::NoteEntry (src/domain/noteentry.h) that
// querydata.cpp uses. The caller fills it from its own index of the
// collection.
type Note struct {
	// Path is the note's path relative to the collection root with "/"
	// separators, for example "projects/Alpha.md". It is the "path" field.
	Path string
	// Title is the file name without its ".md" extension (vaultscan.cpp).
	Title string
	// Folder is the folder part of Path, "" for a note at the root.
	Folder string
	// Modified is the file's modification time. A cell shows it as
	// "2006-01-02 15:04" in the time's own location, so pass local times,
	// as os.FileInfo.ModTime returns them, to match the app. The zero time
	// means the field does not exist.
	Modified time.Time
	// Created is the front matter "created:" date when it parses, and the
	// file's creation time otherwise (vaultscan.cpp entryFromText). A cell
	// shows it as "2006-01-02". The zero time means the field does not exist.
	Created time.Time
	// Words is the number of words in the note's body.
	Words int
	// Tags are the note's tags as its front matter parser read them.
	Tags []string
	// Fields maps every first-level front matter key to its value as written
	// after the colon, with no unquoting (NoteFrontMatter::Metadata::fields).
	// Known keys such as tags and created are included. A key with nothing
	// after the colon, as when a block list follows it, maps to "". When a
	// key appears twice, the last one counts.
	Fields map[string]string
}

// Row is one note in the result.
type Row struct {
	Path string
	// Cells has one display string per column of Result.Columns.
	Cells []string
}

// Group is one column of a board: the notes sharing one value of the
// group-by field.
type Group struct {
	// Name is the field's value, or "(none)" for the notes without one.
	Name string
	Rows []Row
}

// Result is what a query shows.
type Result struct {
	// Columns is the column list actually used: Spec.Columns, or title and
	// modified when the spec names none.
	Columns []string
	// Rows is every matching note in order. A board uses it as the flat list
	// of its cards.
	Rows []Row
	// Groups is set for a board only, in the order each value first appears
	// in Rows, with the "(none)" group last. Its rows are the same Row values
	// as in Rows.
	Groups []Group
}

// noneGroup names the board column for notes without the group-by field.
const noneGroup = "(none)"

// SortNotes sorts notes by Path the way the collection lists them
// (NoteCollection::noteRelPaths, string list::sort): case-sensitively, by
// UTF-16 code units.
func SortNotes(notes []Note) {
	sort.SliceStable(notes, func(i, j int) bool {
		return compareUTF16(notes[i].Path, notes[j].Path) < 0
	})
}

// Evaluate runs a spec over notes (querydata.cpp QueryData::evaluate).
//
// Notes that tie on every sort key keep the order they have in notes. The
// app always passes the collection sorted by path, which makes the result
// the same every time; sort with SortNotes first for the same order. The
// notes are only read.
func Evaluate(spec Spec, notes []Note) Result {
	result := Result{Columns: slices.Clone(spec.Columns)}
	if len(result.Columns) == 0 {
		result.Columns = []string{"title", "modified"}
	}

	// A condition's value reads as the same type for every note, so it is
	// typed once here rather than once per note as in the C++.
	values := make([]typedValue, len(spec.Where))
	for i, cond := range spec.Where {
		values[i] = typedFromString(cond.Value, true)
	}

	var matches []*Note
	for i := range notes {
		note := &notes[i]
		if spec.From != "" &&
			compareFold(note.Folder, spec.From) != 0 &&
			!hasPrefixFold(note.Folder, spec.From+"/") {
			continue
		}
		holds := true
		for j, cond := range spec.Where {
			if !conditionHolds(cond, values[j], note) {
				holds = false
				break
			}
		}
		if holds {
			matches = append(matches, note)
		}
	}

	// Stable sorts applied from the last key to the first, so the first key
	// decides and later keys break its ties. Each note's value for the key is
	// worked out once before sorting; the C++ works it out in every
	// comparison, with the same outcome.
	for k := len(spec.Sort) - 1; k >= 0; k-- {
		key := spec.Sort[k]
		keyed := make([]struct {
			note  *Note
			value typedValue
		}, len(matches))
		for i, note := range matches {
			keyed[i].note = note
			keyed[i].value = fieldValue(note, key.Field)
		}
		sort.SliceStable(keyed, func(i, j int) bool {
			cmp := compareTyped(keyed[i].value, keyed[j].value)
			if key.Ascending {
				return cmp < 0
			}
			return cmp > 0
		})
		for i := range keyed {
			matches[i] = keyed[i].note
		}
	}

	if spec.Limit > 0 && len(matches) > spec.Limit {
		matches = matches[:spec.Limit]
	}

	result.Rows = make([]Row, 0, len(matches))
	for _, note := range matches {
		row := Row{Path: note.Path, Cells: make([]string, len(result.Columns))}
		for i, column := range result.Columns {
			row.Cells[i] = fieldValue(note, column).text
		}
		result.Rows = append(result.Rows, row)
	}

	if spec.View == ViewBoard {
		var order []string
		byGroup := map[string][]Row{}
		for i, note := range matches {
			name := fieldValue(note, spec.GroupBy).text
			if name == "" {
				name = noneGroup
			}
			if _, seen := byGroup[name]; !seen {
				order = append(order, name)
			}
			byGroup[name] = append(byGroup[name], result.Rows[i])
		}
		if i := slices.Index(order, noneGroup); i >= 0 {
			order = append(slices.Delete(order, i, i+1), noneGroup)
		}
		for _, name := range order {
			result.Groups = append(result.Groups, Group{Name: name, Rows: byGroup[name]})
		}
	}

	return result
}

// fieldValue is one field's value on one note (querydata.cpp fieldValue).
// The built-in properties come first, matched case-sensitively, so a front
// matter key named "title" is never read; every other name is looked up in
// the front matter.
func fieldValue(note *Note, field string) typedValue {
	switch field {
	case "title":
		return typedFromString(note.Title, true)
	case "path":
		return typedFromString(note.Path, true)
	case "folder":
		return typedFromString(note.Folder, true)
	case "modified":
		return timeValue(note.Modified, "2006-01-02 15:04")
	case "created":
		return timeValue(note.Created, "2006-01-02")
	case "words":
		return typedValue{
			text:     strconv.Itoa(note.Words),
			number:   float64(note.Words),
			isNumber: true,
			exists:   true,
		}
	case "tags":
		// Shown and compared as text: tags are never read as dates or
		// numbers.
		return typedValue{
			text:   strings.Join(note.Tags, ", "),
			exists: len(note.Tags) > 0,
		}
	}
	raw, ok := note.Fields[field]
	if !ok {
		return typedFromString("", false)
	}
	return typedFromString(fieldString(raw), true)
}

// timeValue is a built-in date field: always a date, formatted with layout
// in the time's own location, and missing when the time is zero.
func timeValue(t time.Time, layout string) typedValue {
	if t.IsZero() {
		return typedValue{}
	}
	return typedValue{text: t.Format(layout), date: t, isDate: true, exists: true}
}

// fieldListValue is the list a has condition searches: the tags for the
// tags field, the front matter value read as a list for any other field.
func fieldListValue(note *Note, field string) []string {
	if field == "tags" {
		return note.Tags
	}
	return fieldList(note.Fields[field])
}

// conditionHolds is querydata.cpp conditionHolds. value is cond.Value
// already typed.
func conditionHolds(cond Condition, value typedValue, note *Note) bool {
	lhs := fieldValue(note, cond.Field)
	// A note without the field fails every test except !=: it differs from
	// any value. Comparing its empty text would put it below everything.
	if !lhs.exists {
		return cond.Op == OpNe
	}
	switch cond.Op {
	case OpExists:
		return true
	case OpContains:
		return containsFold(lhs.text, cond.Value)
	case OpHas:
		for _, item := range fieldListValue(note, cond.Field) {
			if compareFold(item, cond.Value) == 0 {
				return true
			}
		}
		return false
	}
	cmp := compareTyped(lhs, value)
	switch cond.Op {
	case OpEq:
		return cmp == 0
	case OpNe:
		return cmp != 0
	case OpLt:
		return cmp < 0
	case OpGt:
		return cmp > 0
	case OpLe:
		return cmp <= 0
	case OpGe:
		return cmp >= 0
	}
	return false
}
