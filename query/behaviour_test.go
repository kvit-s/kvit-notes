package query

// Checks of the cases query_test.go does not reach: how dates, numbers and
// text are read, compared and matched, and the parse error messages. The
// expected values in these tables were recorded from the earlier Qt version
// of Kvit Notes, given the same inputs.

import (
	"math"
	"slices"
	"strconv"
	"strings"
	"testing"
	"time"
)

func TestParseErrorMessages(t *testing.T) {
	cases := []struct{ body, message string }{
		{"sortby: x", `unknown key "sortby" — known keys: from, where, view, columns, group-by, sort, limit`},
		{"Sort By: x", `unknown key "sort by" — known keys: from, where, view, columns, group-by, sort, limit`},
		{"just words", `expected "key: value", got "just words"`},
		{": value", `expected "key: value", got ": value"`},
		{"view: Cards", `unknown view "Cards" — use table or board`},
		{"where: status active", `cannot parse condition "status active" — expected "field op value" or "field exists"`},
		{"where: status =", `missing value in condition "status ="`},
		{"where: ,", `empty 'where:' line`},
		{"where:", `empty 'where:' line`},
		{"where: a = b, , c = d", `empty condition in 'where:'`},
		{"sort: due sideways", `sort direction must be asc or desc, got "sideways"`},
		{"sort: due asc extra", `cannot parse sort "due asc extra" — expected "field [asc|desc]"`},
		{"sort: due, ,title", `cannot parse sort "" — expected "field [asc|desc]"`},
		{"limit: many", `'limit:' needs a positive integer`},
		{"limit: 0", `'limit:' needs a positive integer`},
		{"limit: -3", `'limit:' needs a positive integer`},
		{"limit: 2147483648", `'limit:' needs a positive integer`},
		{"view: board", `view: board needs a 'group-by:' field`},
		{"view: table\ngroup-by: status", `'group-by:' only applies to view: board`},
		{"group-by: two words", `'group-by:' needs one field name`},
		{"group-by:", `'group-by:' needs one field name`},
	}
	for _, c := range cases {
		_, err := Parse(c.body)
		if err == nil {
			t.Errorf("Parse(%q) succeeded, want %q", c.body, c.message)
			continue
		}
		if err.Error() != c.message {
			t.Errorf("Parse(%q) error\n got %q\nwant %q", c.body, err.Error(), c.message)
		}
	}
}

func TestParseAccepts(t *testing.T) {
	spec, err := Parse("FROM: //projects/sub//\nLimit: +5\nSort: title DESC\nView: Board\nGroup-By: status\ncolumns: ,title,, due ,")
	if err != nil {
		t.Fatal(err)
	}
	if spec.From != "projects/sub" || spec.Limit != 5 || spec.View != ViewBoard || spec.GroupBy != "status" {
		t.Errorf("spec = %+v", spec)
	}
	if len(spec.Sort) != 1 || spec.Sort[0] != (SortKey{Field: "title", Ascending: false}) {
		t.Errorf("Sort = %+v", spec.Sort)
	}
	if want := []string{"title", "due"}; !slices.Equal(spec.Columns, want) {
		t.Errorf("Columns = %q, want %q", spec.Columns, want)
	}
	if spec, err := Parse("limit: 2147483647"); err != nil || spec.Limit != 2147483647 {
		t.Errorf("limit: 2147483647 gave %d, %v", spec.Limit, err)
	}
}

func TestParseCondition(t *testing.T) {
	cases := []struct {
		text string
		want Condition
	}{
		{"status EXISTS", Condition{Field: "status", Op: OpExists}},
		// "exists" preceded by more than one word is not the exists form.
		{"title contains exists", Condition{Field: "title", Op: OpContains, Value: "exists"}},
		{"a != b", Condition{Field: "a", Op: OpNe, Value: "b"}},
		{"a<=b", Condition{Field: "a", Op: OpLe, Value: "b"}},
		{"a >= b", Condition{Field: "a", Op: OpGe, Value: "b"}},
		// "=" is found before ">", so this compares a with "> b".
		{"a => b", Condition{Field: "a", Op: OpEq, Value: "> b"}},
		// The text before "=" is not one word, so the next operators are tried.
		{"title contains a=b", Condition{Field: "title", Op: OpContains, Value: "a=b"}},
		{"tags HAS Work", Condition{Field: "tags", Op: OpHas, Value: "Work"}},
		{"status = active exists", Condition{Field: "status", Op: OpEq, Value: "active exists"}},
	}
	for _, c := range cases {
		got, err := parseCondition(c.text)
		if err != nil {
			t.Errorf("parseCondition(%q): %v", c.text, err)
			continue
		}
		if got != c.want {
			t.Errorf("parseCondition(%q) = %+v, want %+v", c.text, got, c.want)
		}
	}
	// The word operators are matched ignoring ASCII case only, so a long s
	// does not stand for "s" there; it does in the " exists" suffix, which
	// is matched with full case folding.
	if _, err := parseCondition("title ſontainſ x"); err == nil {
		t.Error("long s matched the contains operator")
	}
	if got, err := parseCondition("status existſ"); err != nil || got.Op != OpExists {
		t.Errorf("long s in exists: %+v, %v", got, err)
	}
}

func TestStarterSpecParses(t *testing.T) {
	spec, err := Parse(StarterSpec)
	if err != nil {
		t.Fatal(err)
	}
	if spec.From != "" || len(spec.Where) != 0 || spec.View != ViewTable {
		t.Errorf("spec = %+v", spec)
	}
	if want := []string{"title", "tags", "modified"}; !slices.Equal(spec.Columns, want) {
		t.Errorf("Columns = %q, want %q", spec.Columns, want)
	}
	if len(spec.Sort) != 1 || spec.Sort[0] != (SortKey{Field: "modified", Ascending: false}) {
		t.Errorf("Sort = %+v", spec.Sort)
	}
}

func TestFieldValues(t *testing.T) {
	loc := time.FixedZone("", 2*3600)
	note := &Note{
		Path:     "a/b/ 2026-01-05 .md",
		Title:    " 2026-01-05 ",
		Folder:   "a/b",
		Modified: time.Date(5, 3, 4, 7, 8, 0, 0, loc),
		Words:    12,
		Tags:     []string{"x", "y, z"},
		Fields: map[string]string{
			"quoted": `  "say \"hi\" \\ \n"  `,
			"single": "'2026-02-01'",
			"empty":  "",
			"list":   `[a, "b, c", 'd', ]`,
			"csv":    `a, "b" ,, c`,
			"open":   "[a, b",
			"Title":  "from front matter",
		},
	}
	cases := []struct {
		field, text      string
		exists, date, nb bool
	}{
		{"title", "2026-01-05", true, true, false},
		{"folder", "a/b", true, false, false},
		{"modified", "0005-03-04 07:08", true, true, false},
		{"created", "", false, false, false},
		{"words", "12", true, false, true},
		{"tags", "x, y, z", true, false, false},
		{"quoted", `say "hi" \ \n`, true, false, false},
		{"single", "2026-02-01", true, true, false},
		{"empty", "", true, false, false},
		{"missing", "", false, false, false},
		// Built-in names match case-sensitively; "Title" is a front matter key.
		{"Title", "from front matter", true, false, false},
	}
	for _, c := range cases {
		v := fieldValue(note, c.field)
		if v.text != c.text || v.exists != c.exists || v.isDate != c.date || v.isNumber != c.nb {
			t.Errorf("%s = %+v, want text %q exists %v date %v number %v", c.field, v, c.text, c.exists, c.date, c.nb)
		}
	}
	lists := []struct {
		field string
		want  []string
	}{
		{"tags", []string{"x", "y, z"}},
		{"list", []string{"a", "b, c", "d"}},
		{"csv", []string{"a", "b", "c"}},
		{"open", nil},
		{"missing", nil},
	}
	for _, c := range lists {
		if got := fieldListValue(note, c.field); !slices.Equal(got, c.want) {
			t.Errorf("list %s = %q, want %q", c.field, got, c.want)
		}
	}
}

// A note without the field passes only !=; has and contains work on it
// like any other test.
func TestMissingFieldConditions(t *testing.T) {
	notes := []Note{
		{Path: "A.md", Title: "A", Fields: map[string]string{"status": "active", "labels": "[x, Y]"}},
		{Path: "B.md", Title: "B"},
	}
	cases := []struct {
		where string
		want  []string
	}{
		{"status != active", []string{"B"}},
		{"status != other", []string{"A", "B"}},
		{"status = active", []string{"A"}},
		{"status < zzz", []string{"A"}},
		{"labels has y", []string{"A"}},
		{"labels contains X", []string{"A"}},
		{"tags != x", []string{"A", "B"}},
		{"folder exists", []string{"A", "B"}},
		{"created exists", nil},
	}
	for _, c := range cases {
		spec, err := Parse("where: " + c.where + "\ncolumns: title")
		if err != nil {
			t.Fatal(err)
		}
		var got []string
		for _, row := range Evaluate(spec, notes).Rows {
			got = append(got, row.Cells[0])
		}
		if !slices.Equal(got, c.want) {
			t.Errorf("where %s: %q, want %q", c.where, got, c.want)
		}
	}
}

// A board groups by the exact text of the field, keeps the order in which
// each value first appears in the sorted rows, and puts "(none)" last.
func TestBoardGroupOrder(t *testing.T) {
	notes := []Note{
		{Path: "1.md", Title: "1", Fields: map[string]string{"s": "b"}},
		{Path: "2.md", Title: "2"},
		{Path: "3.md", Title: "3", Fields: map[string]string{"s": "a"}},
		{Path: "4.md", Title: "4", Fields: map[string]string{"s": "B"}},
		{Path: "5.md", Title: "5", Fields: map[string]string{"s": "b"}},
	}
	spec, err := Parse("group-by: s\ncolumns: title")
	if err != nil {
		t.Fatal(err)
	}
	result := Evaluate(spec, notes)
	var names []string
	for _, g := range result.Groups {
		var titles []string
		for _, row := range g.Rows {
			titles = append(titles, row.Cells[0])
		}
		names = append(names, g.Name+":"+strings.Join(titles, ","))
	}
	if want := []string{"b:1,5", "a:3", "B:4", "(none):2"}; !slices.Equal(names, want) {
		t.Errorf("groups = %q, want %q", names, want)
	}
}

func TestSortNotesUsesUTF16Order(t *testing.T) {
	notes := []Note{{Path: "b"}, {Path: "\U0001F600"}, {Path: ""}, {Path: "B"}, {Path: "a"}}
	SortNotes(notes)
	var got []string
	for _, n := range notes {
		got = append(got, n.Path)
	}
	// A character above U+FFFF sorts by its UTF-16 surrogates, which sort
	// below U+E000.
	if want := []string{"B", "a", "b", "\U0001F600", ""}; !slices.Equal(got, want) {
		t.Errorf("order = %q, want %q", got, want)
	}
}

// typedCases lists how each text is read: "date" with the wall-clock
// time and "local", "utc" or the UTC offset in seconds; "number" with the
// value; or "text".
var typedCases = []struct{ text, kind, detail string }{
	{"2026-08-01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00", "date", "2026-08-01T10:00:00.000 local"},
	{"2026-08-01 10:00", "date", "2026-08-01T10:00:00.000 local"},
	{"2026-08-01t10:00", "date", "2026-08-01T10:00:00.000 local"},
	{"2026-08-01T10", "date", "2026-08-01T10:00:00.000 local"},
	{"2026-08-01T10:00:30", "date", "2026-08-01T10:00:30.000 local"},
	{"2026-08-01T10:00:30.5", "date", "2026-08-01T10:00:30.500 local"},
	{"2026-08-01T10:00:30,25", "date", "2026-08-01T10:00:30.250 local"},
	{"2026-08-01T10:00Z", "date", "2026-08-01T10:00:00.000 utc"},
	{"2026-08-01T10:00z", "date", "2026-08-01T10:00:00.000 utc"},
	{"2026-08-01T10:00+02:00", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+0200", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+02", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00-05:30", "date", "2026-08-01T10:00:00.000 -19800"},
	{"2026-08-01T24:00", "date", "2026-08-02T00:00:00.000 local"},
	{"2026-08-01T24:30", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01abc", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01 is the day", "date", "2026-08-01T00:00:00.000 local"},
	{"2026/08/01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026.08.01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-8-1", "text", ""},
	{"2026-02-30", "text", ""},
	{"2024-02-29", "date", "2024-02-29T00:00:00.000 local"},
	{"0000-01-01", "text", ""},
	{"10000-01-01", "text", ""},
	{"2026-08-011", "text", ""},
	{"2026-08-01T", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T1", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:0", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:3", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10.5", "date", "2026-08-01T10:30:00.000 local"},
	{"2026-08-01T10:30.5", "date", "2026-08-01T10:30:30.000 local"},
	{"+026-08-01", "text", ""},
	{"2026-08-01T10:00+24:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00+23:59", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00.9999", "date", "2026-08-01T10:00:01.000 local"},
	{"2026-08-01T23:59:59.9999", "date", "2026-08-02T00:00:00.000 local"},
	{"2026_08_01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01\t10:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00Z", "date", "2026-08-01T10:00:00.000 utc"},
	{"2026-08-01T10:00:00.123+01:00", "date", "2026-08-01T10:00:00.123 3600"},
	{"2026-08-01T10:00+1", "date", "2026-08-01T10:00:00.000 3600"},
	{"2026-08-01T10:00+02:0", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00:00.", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00.5.5", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00,5,5", "date", "2026-08-01T00:00:00.000 local"},
	{"0001-01-01", "date", "0001-01-01T00:00:00.000 local"},
	{"9999-12-31", "date", "9999-12-31T00:00:00.000 local"},
	{"2026-08-01T10:60", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:60", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01x10:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-13-01", "text", ""},
	{"2026-00-10", "text", ""},
	{"2026-08-00", "text", ""},
	{"2026-08-01T10:00:00+02:00Z", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T-05:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00-", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00 +02:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T+10:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T1:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00.1234567", "date", "2026-08-01T10:00:00.123 local"},
	{"2026-08-01T24", "date", "2026-08-02T00:00:00.000 local"},
	{"2026-08-01T24:00:00.000", "date", "2026-08-02T00:00:00.000 local"},
	{"2026-08-01T23:59:59.9996", "date", "2026-08-02T00:00:00.000 local"},
	{"2026a08b01", "text", ""},
	{"2026-08-01T10:00:00+2:00", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+14:00", "date", "2026-08-01T10:00:00.000 50400"},
	{"2026-08-01T10:00+14:01", "date", "2026-08-01T10:00:00.000 50460"},
	{"2026-08-01T10:00+16:00", "date", "2026-08-01T10:00:00.000 57600"},
	{"2026-08-01T10:00-16:00", "date", "2026-08-01T10:00:00.000 -57600"},
	{"2026-08-01T10:00-16:01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00+ 2", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+02:60", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00+0260", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00+020", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+02000", "date", "2026-08-01T10:00:00.000 7200"},
	{"2026-08-01T10:00+020000", "date", "2026-08-01T00:00:00.000 local"},
	{"٢٠٢٦-08-01", "text", ""},
	{"2026-08-01١", "text", ""},
	{"2026—08—01", "date", "2026-08-01T00:00:00.000 local"},
	{"2026+08+01", "text", ""},
	{"2026-08-01T10:00:00.5,5", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10,5", "date", "2026-08-01T10:30:00.000 local"},
	{"2026-08-01T10:00,", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T.5", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00ZZ", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T10:00:00+02:00:00", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T00:00:00.0005", "date", "2026-08-01T00:00:00.001 local"},
	{"2026-08-01T00:00:00.0015", "date", "2026-08-01T00:00:00.002 local"},
	{"2026-08-01T00:00:00.0025", "date", "2026-08-01T00:00:00.003 local"},
	{"2026-08-01T00:00:00.00049999", "date", "2026-08-01T00:00:00.000 local"},
	{"2026-08-01T00:00:00.000000000000000000000005", "date", "2026-08-01T00:00:00.000 local"},
	{"1e5", "number", "100000"},
	{"1E5", "number", "100000"},
	{".5", "number", "0.5"},
	{"5.", "number", "5"},
	{"+5", "number", "5"},
	{"-5", "number", "-5"},
	{"inf", "number", "+inf"},
	{"Inf", "number", "+inf"},
	{"INF", "number", "+inf"},
	{"-inf", "number", "-inf"},
	{"+inf", "number", "+inf"},
	{"infinity", "text", ""},
	{"nan", "number", "nan"},
	{"NaN", "number", "nan"},
	{"Nan", "number", "nan"},
	{"-nan", "text", ""},
	{"+nan", "text", ""},
	{"0x10", "text", ""},
	{"1_000", "text", ""},
	{"1,000", "text", ""},
	{"1.2.3", "text", ""},
	{"1e", "text", ""},
	{"e5", "text", ""},
	{"1e400", "text", ""},
	{"1e-400", "text", ""},
	{"0", "number", "0"},
	{"-0", "number", "0"},
	{"00012", "number", "12"},
	{"1.5e+3", "number", "1500"},
	{"1.5e-3", "number", "0.0015"},
	{"١", "text", ""},
	{"１２", "text", ""},
	{"12abc", "text", ""},
	{"1 2", "text", ""},
	{"5e5.5", "text", ""},
	{"1.", "number", "1"},
	{"+.5", "number", "0.5"},
	{"-.5e2", "number", "-50"},
	{"++5", "text", ""},
	{"1d", "text", ""},
	{"1f", "text", ""},
	{"1e+", "text", ""},
	{"0.", "number", "0"},
	{"abc", "text", ""},
	{".", "text", ""},
	{"+", "text", ""},
	{"-", "text", ""},
	{".e5", "text", ""},
	{"5.e3", "number", "5000"},
	{"1e05", "number", "100000"},
	{"1E-5", "number", "1.0000000000000001e-05"},
	{"- 5", "text", ""},
	{"1e-310", "number", "9.9999999999999694e-311"},
	{"4.9e-324", "number", "4.9406564584124654e-324"},
	{"2e-324", "text", ""},
	{"3e-324", "number", "4.9406564584124654e-324"},
	{"1.7976931348623157e308", "number", "1.7976931348623157e+308"},
	{"1.8e308", "text", ""},
	{"-infinity", "text", ""},
	{"0e-500", "number", "0"},
	{"00.0e-400", "number", "0"},
	{"123456789012345678901234567890", "number", "1.2345678901234568e+29"},
	{"inff", "text", ""},
}

func TestTypedValuesMatchQt(t *testing.T) {
	for _, c := range typedCases {
		v := typedFromString(c.text, true)
		switch c.kind {
		case "date":
			if !v.isDate {
				t.Errorf("%q: not a date, want %s", c.text, c.detail)
				continue
			}
			wall, zone, _ := strings.Cut(c.detail, " ")
			loc := time.Local
			switch zone {
			case "local":
			case "utc":
				loc = time.UTC
			default:
				offset, _ := strconv.Atoi(zone)
				loc = time.FixedZone("", offset)
			}
			want, err := time.ParseInLocation("2006-01-02T15:04:05.000", wall, loc)
			if err != nil {
				t.Fatal(err)
			}
			if !v.date.Equal(want) {
				t.Errorf("%q: date %v, want %v", c.text, v.date, want)
			}
		case "number":
			if v.isDate || !v.isNumber {
				t.Errorf("%q: date %v number %v, want the number %s", c.text, v.isDate, v.isNumber, c.detail)
				continue
			}
			switch c.detail {
			case "nan":
				if !math.IsNaN(v.number) {
					t.Errorf("%q: %v, want NaN", c.text, v.number)
				}
			case "+inf", "-inf":
				if !math.IsInf(v.number, map[string]int{"+inf": 1, "-inf": -1}[c.detail]) {
					t.Errorf("%q: %v, want %s", c.text, v.number, c.detail)
				}
			default:
				want, _ := strconv.ParseFloat(c.detail, 64)
				if v.number != want {
					t.Errorf("%q: %v, want %v", c.text, v.number, want)
				}
			}
		default:
			if v.isDate || v.isNumber {
				t.Errorf("%q: date %v number %v, want text", c.text, v.isDate, v.isNumber)
			}
		}
	}
}

func TestCompareFoldMatchesQt(t *testing.T) {
	cases := []struct {
		a, b     string
		cmp      int
		contains bool
	}{
		{"a", "B", -1, false},
		{"B", "a", 1, false},
		{"abc", "ABC", 0, true},
		{"ab", "abc", -1, false},
		{"", "a", -1, false},
		{"é", "É", 0, true},
		{"ς", "Σ", 0, true},
		{"ſ", "S", 0, true},
		{"K", "k", 0, true},
		{"", "\U0001F600", 1, false},
		{"z", "é", -1, false},
		{"_", "a", -1, false},
		{"_", "A", -1, false},
		{"[", "a", -1, false},
		{"ẞ", "ß", 0, true},
		{"İ", "i", 1, false},
		{"ı", "I", 1, false},
		{"ǅ", "ǆ", 0, true},
		// Folds Unicode 16.0 has and Go's unicode tables lack or add.
		{"ΐ", "ΐ", 0, true},
		{"ﬅ", "ﬆ", 0, true},
		{"Ꭰ", "ꭰ", 0, true},
		{"\U0001E921", "\U0001E943", 0, true},
		{"꟎", "꟏", -1, false},
	}
	for _, c := range cases {
		if got := compareFold(c.a, c.b); got != c.cmp {
			t.Errorf("compareFold(%q, %q) = %d, want %d", c.a, c.b, got, c.cmp)
		}
		if got := containsFold(c.a, c.b); got != c.contains {
			t.Errorf("containsFold(%q, %q) = %v, want %v", c.a, c.b, got, c.contains)
		}
	}
	if !hasPrefixFold("ſub/x", "SUB/") {
		t.Error("hasPrefixFold does not fold long s")
	}
	if got := qtToLower("İX"); got != "i̇x" {
		t.Errorf("qtToLower = %q", got)
	}
}

// Without Post, the answer is delivered on the background goroutine.
func TestRequestRunWithoutPost(t *testing.T) {
	f := makeFixture(t)
	answers := make(chan Answer, 1)
	tools := &Tools{OnResult: func(_ string, a Answer) { answers <- a }}
	tools.SetCollection(f)
	tools.RequestRun("x", "where: status = active")
	select {
	case a := <-answers:
		if !a.OK || len(a.Rows) != 3 || a.View != ViewTable {
			t.Errorf("answer = %+v", a)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("no answer")
	}
	if a, ok := tools.CachedResult("where: status = active"); !ok || len(a.Rows) != 3 {
		t.Errorf("CachedResult = %+v, %v", a, ok)
	}

	tools.SetCollection(nil)
	tools.RequestRun("y", "where: status = active")
	if a := <-answers; a.OK || a.Error != NoCollectionError {
		t.Errorf("answer without a collection = %+v", a)
	}
}

func TestCacheEntryLimit(t *testing.T) {
	f := makeFixture(t)
	tools := &Tools{}
	tools.SetCollection(f)
	for i := 0; i < maxCacheEntries+6; i++ {
		tools.Run("limit: " + strconv.Itoa(i+1))
	}
	if n := tools.CacheSize(); n != maxCacheEntries {
		t.Errorf("CacheSize = %d, want %d", n, maxCacheEntries)
	}
	// The newest entry is kept; the oldest were dropped.
	if _, ok := tools.CachedResult("limit: 1"); ok {
		t.Error("oldest entry still cached")
	}
	if _, ok := tools.CachedResult("limit: " + strconv.Itoa(maxCacheEntries+6)); !ok {
		t.Error("newest entry not cached")
	}
}
