// Package query runs collection query blocks.
//
// A query block is a fenced code block whose language is "query". Its body is
// a spec, one "key: value" line per setting:
//
//	from: projects/
//	where: status = active
//	view: table
//	columns: title, status, due, priority
//	sort: due asc
//
// The block shows the notes of the open collection that the spec selects,
// either as a table with one row per note or, with "view: board" and
// "group-by: FIELD", as columns of cards, one column per value of that field.
// The note stores only the spec; the rows are worked out again whenever the
// block is shown. A field is one of the built-in properties title, path,
// folder, modified, created, words and tags, or any first-level key of the
// note's front matter (the "key: value" block between "---" lines at the top
// of a note).
//
// Parse turns a block body into a Spec, or into the error message the block
// shows instead of results. Evaluate applies a Spec to a list of notes and
// returns the rows, with each cell formatted for display. Tools adds the
// cache and the background evaluation the editor uses, so that several blocks
// showing the same query cost one evaluation after each change to the
// collection.
package query

import (
	"errors"
	"fmt"
	"strconv"
	"strings"
)

// Op is the comparison in one where condition.
type Op int

const (
	OpEq       Op = iota // field = value
	OpNe                 // field != value
	OpLt                 // field < value
	OpGt                 // field > value
	OpLe                 // field <= value
	OpGe                 // field >= value
	OpContains           // field contains value: case-insensitive substring
	OpHas                // field has value: one item of a list equals value
	OpExists             // field exists: the note has the field at all
)

// View is how the block shows its notes.
type View int

const (
	ViewTable View = iota // one row per note
	ViewBoard             // columns of cards grouped by Spec.GroupBy
)

// String is the view's name as written in a spec: "table" or "board".
func (v View) String() string {
	if v == ViewBoard {
		return "board"
	}
	return "table"
}

// Condition is one "field op value" test from a where line.
type Condition struct {
	Field string
	Op    Op
	Value string // empty for OpExists
}

// SortKey is one field of a sort line with its direction.
type SortKey struct {
	Field     string
	Ascending bool
}

// Spec is a parsed query block body.
type Spec struct {
	// From is the folder the query is limited to, without leading or
	// trailing slashes; notes in its subfolders are included. "" means the
	// whole collection.
	From string
	// Where holds every condition of every where line; a note must pass all
	// of them.
	Where []Condition
	View  View
	// Columns are the fields shown, in order. Evaluate uses title and
	// modified when it is empty.
	Columns []string
	// GroupBy is the field whose values name the board's columns.
	GroupBy string
	// Sort keys apply in order: the first decides, the next breaks its ties.
	Sort []SortKey
	// Limit caps the number of notes shown; 0 means no limit.
	Limit int
}

// Parse reads a query block body. Blank lines and lines starting with "#"
// are skipped. The error, when there is one, is the message the block shows
// in place of results, word for word; unknown keys, views, operators and
// sort directions are errors rather than being ignored, because the spec is
// written by hand.
func Parse(body string) (Spec, error) {
	var spec Spec
	viewSet := false

	for _, rawLine := range strings.Split(body, "\n") {
		line := strings.TrimSpace(rawLine)
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}

		colon := strings.IndexByte(line, ':')
		if colon <= 0 {
			return Spec{}, fmt.Errorf(`expected "key: value", got "%s"`, line)
		}
		key := qtToLower(strings.TrimSpace(line[:colon]))
		value := strings.TrimSpace(line[colon+1:])

		switch key {
		case "from":
			spec.From = strings.TrimLeft(strings.TrimRight(value, "/"), "/")

		case "where":
			parts := splitSkipEmpty(value, ",")
			if len(parts) == 0 {
				return Spec{}, errors.New("empty 'where:' line")
			}
			for _, part := range parts {
				cond, err := parseCondition(part)
				if err != nil {
					return Spec{}, err
				}
				spec.Where = append(spec.Where, cond)
			}

		case "view":
			switch qtToLower(value) {
			case "table":
				spec.View = ViewTable
			case "board":
				spec.View = ViewBoard
			default:
				return Spec{}, fmt.Errorf(`unknown view "%s" — use table or board`, value)
			}
			viewSet = true

		case "columns":
			for _, part := range strings.Split(value, ",") {
				if column := strings.TrimSpace(part); column != "" {
					spec.Columns = append(spec.Columns, column)
				}
			}

		case "group-by":
			if value == "" || strings.Contains(value, " ") {
				return Spec{}, errors.New("'group-by:' needs one field name")
			}
			spec.GroupBy = value

		case "sort":
			for _, part := range splitSkipEmpty(value, ",") {
				words := splitSkipEmpty(strings.TrimSpace(part), " ")
				if len(words) == 0 || len(words) > 2 {
					return Spec{}, fmt.Errorf(`cannot parse sort "%s" — expected "field [asc|desc]"`,
						strings.TrimSpace(part))
				}
				sortKey := SortKey{Field: words[0], Ascending: true}
				if len(words) == 2 {
					switch qtToLower(words[1]) {
					case "asc":
						sortKey.Ascending = true
					case "desc":
						sortKey.Ascending = false
					default:
						return Spec{}, fmt.Errorf(`sort direction must be asc or desc, got "%s"`, words[1])
					}
				}
				spec.Sort = append(spec.Sort, sortKey)
			}

		case "limit":
			// A decimal int with an optional sign.
			limit, err := strconv.ParseInt(value, 10, 32)
			if err != nil || limit < 1 {
				return Spec{}, errors.New("'limit:' needs a positive integer")
			}
			spec.Limit = int(limit)

		default:
			return Spec{}, fmt.Errorf(`unknown key "%s" — known keys: from, where, view, columns, group-by, sort, limit`, key)
		}
	}

	if spec.View == ViewBoard && spec.GroupBy == "" {
		return Spec{}, errors.New("view: board needs a 'group-by:' field")
	}
	if spec.View == ViewTable && spec.GroupBy != "" && viewSet {
		return Spec{}, errors.New("'group-by:' only applies to view: board")
	}
	// "group-by:" without a view line means a board.
	if !viewSet && spec.GroupBy != "" {
		spec.View = ViewBoard
	}
	return spec, nil
}

// splitSkipEmpty is the parts of s between separators, leaving out the
// empty ones. A part made of spaces is not empty.
func splitSkipEmpty(s, sep string) []string {
	var out []string
	for _, part := range strings.Split(s, sep) {
		if part != "" {
			out = append(out, part)
		}
	}
	return out
}

// opTokens are the comparison operators in the order parseCondition tries
// them, longer tokens first so that "<=" is found before "<". The word
// operators include their surrounding spaces.
var opTokens = []struct {
	token string
	op    Op
}{
	{"!=", OpNe}, {"<=", OpLe}, {">=", OpGe},
	{"=", OpEq}, {"<", OpLt}, {">", OpGt},
	{" contains ", OpContains}, {" has ", OpHas},
}

// parseCondition reads one condition, "field op value" or "field exists". The
// field is one word. For each operator in opTokens order it looks at the
// first place the operator appears, ignoring letter case; if the text before
// it is not a single word it tries the next operator, which is why "title
// contains a=b" is a contains test.
func parseCondition(text string) (Condition, error) {
	trimmed := strings.TrimSpace(text)
	if trimmed == "" {
		return Condition{}, errors.New("empty condition in 'where:'")
	}

	if start := suffixFold(trimmed, " exists"); start >= 0 {
		field := strings.TrimSpace(trimmed[:start])
		if field != "" && !strings.Contains(field, " ") {
			return Condition{Field: field, Op: OpExists}, nil
		}
	}

	for _, op := range opTokens {
		at := indexASCIIFold(trimmed, op.token)
		if at <= 0 {
			continue
		}
		field := strings.TrimSpace(trimmed[:at])
		value := strings.TrimSpace(trimmed[at+len(op.token):])
		if field == "" || strings.Contains(field, " ") {
			continue
		}
		if value == "" {
			return Condition{}, fmt.Errorf(`missing value in condition "%s"`, trimmed)
		}
		return Condition{Field: field, Op: op.op, Value: value}, nil
	}
	return Condition{}, fmt.Errorf(`cannot parse condition "%s" — expected "field op value" or "field exists"`, trimmed)
}
