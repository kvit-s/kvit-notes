package diagram

// These tests are the layout and render functions of the app's
// tests/test_mermaider.cpp, one Go test per test function and in the same
// order, with the same sources and expectations; the parser's functions of
// that file are ported in package mermaid. Each name has "Er" added,
// because every family's test file has a layoutDeterministic. The test
// tells a dashed line by ::DashLine; here it is LineDashed.

import (
	"strings"
	"testing"
)

func TestErLayoutEntityTables(t *testing.T) {
	r := parse("erDiagram\n" +
		"  CUSTOMER {\n" +
		"    string name PK\n" +
		"    int age\n" +
		"  }\n" +
		"  CUSTOMER ||--o{ ORDER : places\n")
	s := LayoutEr(&r.Er, testOpts())
	if len(s.Shapes) != 2 {
		t.Errorf("shapes = %d, want 2", len(s.Shapes))
	}
	typeCell, boldTitle := false, false
	for _, tx := range s.Texts {
		typeCell = typeCell || tx.Text == "string"
		boldTitle = boldTitle || tx.Text == "CUSTOMER" && tx.Bold
	}
	if !typeCell || !boldTitle {
		t.Errorf("type cell, bold title = %t, %t", typeCell, boldTitle)
	}
	if !strings.Contains(s.Summary, "2 entities") {
		t.Errorf("summary = %q", s.Summary)
	}
}

func TestErLayoutCrowsFootMarkers(t *testing.T) {
	r := parse("erDiagram\n  CUSTOMER ||--o{ ORDER : places")
	s := LayoutEr(&r.Er, testOpts())
	one, zeroMany := false, false
	for _, p := range s.Paths {
		one = one || p.StartMarker == MarkerErOne
		zeroMany = zeroMany || p.EndMarker == MarkerErZeroMany
	}
	if !one || !zeroMany {
		t.Errorf("one, zero or many = %t, %t", one, zeroMany)
	}
}

func TestErLayoutNonIdentifyingIsDashed(t *testing.T) {
	r := parse("erDiagram\n  A ||..o{ B : maybe")
	s := LayoutEr(&r.Er, testOpts())
	dashed := false
	for _, p := range s.Paths {
		dashed = dashed || p.Style == LineDashed && p.EndMarker != MarkerNone
	}
	if !dashed {
		t.Error("the relationship is not dashed")
	}
}

func TestErLayoutDeterministic(t *testing.T) {
	src := "erDiagram\n" +
		"  CUSTOMER ||--o{ ORDER : places\n" +
		"  ORDER ||--|{ LINE-ITEM : contains\n" +
		"  CUSTOMER }|..|{ DELIVERY-ADDRESS : uses\n"
	r1, r2 := parse(src), parse(src)
	if !sameShapePositions(LayoutEr(&r1.Er, testOpts()), LayoutEr(&r2.Er, testOpts())) {
		t.Error("two layouts of the same source differ")
	}
}

func TestErRendererRendersErDiagram(t *testing.T) {
	ClearCache()
	r := Render("erDiagram\n  CUSTOMER ||--o{ ORDER : places", testOpts())
	if !r.Valid || r.UnsupportedFamily || r.HasError {
		t.Errorf("valid %t, unsupported %t, error %t", r.Valid, r.UnsupportedFamily, r.HasError)
	}
}
