package diagram

// These tests check the class-diagram layout and renderer: compartment
// boxes, the extension marker, namespace frames, note boxes, and that the
// same source lays out the same way twice. The class-diagram parser is
// tested in package mermaid.

import (
	"strings"
	"testing"
)

func TestClassLayoutCompartmentBoxes(t *testing.T) {
	r := parse("classDiagram\n" +
		"  class Animal {\n" +
		"    +int age\n" +
		"    +isMammal()\n" +
		"  }\n" +
		"  Animal <|-- Duck\n")
	s := LayoutClass(&r.Class, testOpts())
	if len(s.Shapes) != 2 {
		t.Errorf("shapes = %d, want 2", len(s.Shapes))
	}
	// Two compartment lines in each class box, and the relation.
	if len(s.Paths) != 5 {
		t.Errorf("paths = %d, want 5", len(s.Paths))
	}
	titleBold, member := false, false
	for _, tx := range s.Texts {
		titleBold = titleBold || tx.Text == "Animal" && tx.Bold
		member = member || tx.Text == "+int age" && tx.Align == AlignLeft
	}
	if !titleBold || !member {
		t.Errorf("bold title, left member = %t, %t", titleBold, member)
	}
	if !strings.Contains(s.Summary, "2 classes") {
		t.Errorf("summary = %q", s.Summary)
	}
}

func TestClassLayoutExtensionMarker(t *testing.T) {
	r := parse("classDiagram\n  Animal <|-- Duck")
	s := LayoutClass(&r.Class, testOpts())
	triangle := false
	for _, p := range s.Paths {
		triangle = triangle || p.StartMarker == MarkerTriangleOpen
	}
	if !triangle {
		t.Error("no hollow triangle")
	}
}

func TestClassLayoutNamespaceGroup(t *testing.T) {
	r := parse("classDiagram\n" +
		"  namespace Shapes {\n" +
		"    class A\n" +
		"    class B\n" +
		"  }\n" +
		"  A --> B\n")
	s := LayoutClass(&r.Class, testOpts())
	if len(s.Groups) != 1 {
		t.Fatalf("groups = %d, want 1", len(s.Groups))
	}
	if s.Groups[0].Title != "Shapes" {
		t.Errorf("title = %q, want Shapes", s.Groups[0].Title)
	}
}

func TestClassLayoutNoteBoxes(t *testing.T) {
	r := parse("classDiagram\n" +
		"  class Cat\n" +
		"  note for Cat \"a note\"\n")
	s := LayoutClass(&r.Class, testOpts())
	note := false
	for _, sh := range s.Shapes {
		note = note || sh.FillRole == RoleNoteFill
	}
	if !note {
		t.Error("no note")
	}
}

func TestClassLayoutDeterministic(t *testing.T) {
	src := "classDiagram\n  A <|-- B\n  A <|-- C\n  B --> D\n  C --> D\n"
	r1, r2 := parse(src), parse(src)
	if !sameShapePositions(LayoutClass(&r1.Class, testOpts()), LayoutClass(&r2.Class, testOpts())) {
		t.Error("two layouts of the same source differ")
	}
}

func TestClassRendererRendersClassDiagram(t *testing.T) {
	ClearCache()
	r := Render("classDiagram\n  Animal <|-- Duck", testOpts())
	if !r.Valid || r.UnsupportedFamily || r.HasError {
		t.Errorf("valid %t, unsupported %t, error %t", r.Valid, r.UnsupportedFamily, r.HasError)
	}
}
