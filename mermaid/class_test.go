package mermaid

// These tests check the class-diagram parser; laying class diagrams out is
// tested in the diagram package. Some inputs are from
// demos/classchart.html of mermaid@11.16.0 (MIT license, (c) Knut
// Sveidqvist).

import (
	"fmt"
	"slices"
	"testing"
)

func classNode(a *ClassAst, id string) *ClassNode {
	if i := a.IndexOfClass(id); i >= 0 {
		return &a.Classes[i]
	}
	return nil
}

func classRelation(a *ClassAst, from, to string) *ClassRelation {
	for i := range a.Relations {
		if a.Relations[i].From == from && a.Relations[i].To == to {
			return &a.Relations[i]
		}
	}
	return nil
}

func TestClassFamilyIsSupported(t *testing.T) {
	for _, header := range []string{"classDiagram", "classDiagram-v2"} {
		r := Parse(header + "\n  Animal <|-- Duck")
		if r.Type != Class || !r.Supported || r.HasErrors() {
			t.Errorf("%s: type %v supported %v error %q", header, r.Type, r.Supported, errorsOf(r))
		}
		if len(r.Class.Classes) != 2 || len(r.Class.Relations) != 1 {
			t.Errorf("%s: classes %d relations %d", header, len(r.Class.Classes), len(r.Class.Relations))
		}
	}
}

func TestClassRelationEndsAndLines(t *testing.T) {
	cases := []struct {
		op             string
		fromEnd, toEnd ClassRelEnd
		dotted         bool
	}{
		{"<|--", RelExtension, RelNone, false},
		{"--|>", RelNone, RelExtension, false},
		{"<|..", RelExtension, RelNone, true},
		{"..|>", RelNone, RelExtension, true},
		{"*--", RelComposition, RelNone, false},
		{"--*", RelNone, RelComposition, false},
		{"o--", RelAggregation, RelNone, false},
		{"--o", RelNone, RelAggregation, false},
		{"-->", RelNone, RelDependency, false},
		{"<--", RelDependency, RelNone, false},
		{"..>", RelNone, RelDependency, true},
		{"()--", RelLollipop, RelNone, false},
		{"--", RelNone, RelNone, false},
		{"..", RelNone, RelNone, true},
		{"<-->", RelDependency, RelDependency, false},
	}
	for _, c := range cases {
		r := Parse(fmt.Sprintf("classDiagram\n  A %s B", c.op))
		if r.HasErrors() {
			t.Errorf("%s: error %q", c.op, errorsOf(r))
		}
		if len(r.Class.Relations) != 1 {
			t.Errorf("%s: relations = %d, want 1", c.op, len(r.Class.Relations))
			continue
		}
		rel := r.Class.Relations[0]
		if rel.FromEnd != c.fromEnd || rel.ToEnd != c.toEnd || rel.Dotted != c.dotted {
			t.Errorf("%s: relation = %+v", c.op, rel)
		}
	}
}

func TestClassCardinalitiesAndLabels(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  Class03 \"0\" *-- \"0..n\" Class04\n" +
		"  Class09 \"many\" --> \"1\" C2 : Where am i?\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	r1 := classRelation(&r.Class, "Class03", "Class04")
	if r1 == nil || r1.FromCard != "0" || r1.ToCard != "0..n" || r1.FromEnd != RelComposition {
		t.Errorf("Class03-Class04 = %+v", r1)
	}
	r2 := classRelation(&r.Class, "Class09", "C2")
	if r2 == nil || r2.FromCard != "many" || r2.ToCard != "1" || r2.Label != "Where am i?" {
		t.Errorf("Class09-C2 = %+v", r2)
	}
}

func TestClassMembersViaColonAndBody(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  Animal : +int age\n" +
		"  Animal : +isMammal()\n" +
		"  class Duck{\n" +
		"    +String beakColor\n" +
		"    +swim()\n" +
		"    +quack()\n" +
		"  }\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	animal := classNode(&r.Class, "Animal")
	if animal == nil || !slices.Equal(animal.Attributes, []string{"+int age"}) ||
		!slices.Equal(animal.Methods, []string{"+isMammal()"}) {
		t.Errorf("Animal = %+v", animal)
	}
	duck := classNode(&r.Class, "Duck")
	if duck == nil || len(duck.Attributes) != 1 || len(duck.Methods) != 2 {
		t.Errorf("Duck = %+v", duck)
	}
}

func TestClassAnnotationsInlineStandaloneAndInBody(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  class Shape <<interface>>\n" +
		"  <<abstract>> Animal\n" +
		"  class Svc {\n" +
		"    <<service>>\n" +
		"    int id\n" +
		"  }\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	for id, want := range map[string]string{"Shape": "interface", "Animal": "abstract", "Svc": "service"} {
		if got := classNode(&r.Class, id).Annotation; got != want {
			t.Errorf("%s annotation = %q, want %q", id, got, want)
		}
	}
	if got := classNode(&r.Class, "Svc").Attributes; !slices.Equal(got, []string{"int id"}) {
		t.Errorf("Svc attributes = %q", got)
	}
}

func TestClassGenericsAndLabelsAndBackticks(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  class Squirrel~T~\n" +
		"  class Animal[\"Animal with a label\"]\n" +
		"  class `Bank Account`\n" +
		"  `Bank Account` <|-- Squirrel~T~\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if sq := classNode(&r.Class, "Squirrel~T~"); sq == nil || sq.Label != "Squirrel<T>" {
		t.Errorf("Squirrel~T~ = %+v", sq)
	}
	if got := classNode(&r.Class, "Animal").Label; got != "Animal with a label" {
		t.Errorf("Animal label = %q", got)
	}
	if classNode(&r.Class, "Bank Account") == nil {
		t.Error("no class Bank Account")
	}
	if len(r.Class.Relations) != 1 {
		t.Errorf("relations = %d, want 1", len(r.Class.Relations))
	}
}

func TestClassNamespacesGroupClasses(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  namespace BaseShapes {\n" +
		"    class Triangle\n" +
		"    class Rectangle {\n" +
		"      double width\n" +
		"    }\n" +
		"  }\n" +
		"  class Free\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Class.Namespaces) != 1 {
		t.Fatalf("namespaces = %d, want 1", len(r.Class.Namespaces))
	}
	if got := r.Class.Namespaces[0].ClassIDs; !slices.Equal(got, []string{"Triangle", "Rectangle"}) {
		t.Errorf("namespace classes = %q", got)
	}
	if got := classNode(&r.Class, "Free").NamespaceIndex; got != -1 {
		t.Errorf("Free namespace = %d", got)
	}
	if got := classNode(&r.Class, "Triangle").NamespaceIndex; got != 0 {
		t.Errorf("Triangle namespace = %d", got)
	}
}

func TestClassNotesFreeAndForClass(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  note \"This is a general note\"\n" +
		"  note for Cat \"should have no members area\"\n" +
		"  Animal ()-- Cat\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Class.Notes) != 2 {
		t.Fatalf("notes = %d, want 2", len(r.Class.Notes))
	}
	if r.Class.Notes[0].ForClass != "" || r.Class.Notes[1].ForClass != "Cat" {
		t.Errorf("notes = %+v", r.Class.Notes)
	}
}

func TestClassDirectionAndStyling(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  direction LR\n" +
		"  classDef pink fill:#f9f\n" +
		"  class A:::pink\n" +
		"  cssClass \"B,C\" pink\n" +
		"  style A fill:#ccf\n" +
		"  A --> B\n" +
		"  B --> C\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if r.Class.Direction != LR {
		t.Errorf("direction = %v", r.Class.Direction)
	}
	if _, ok := r.Class.ClassDefs["pink"]; !ok {
		t.Error("no classDef pink")
	}
	a := classNode(&r.Class, "A").CSSClasses
	if !slices.Contains(a, "pink") || !slices.Contains(a, "__style_A") {
		t.Errorf("A classes = %q", a)
	}
	if b := classNode(&r.Class, "B").CSSClasses; !slices.Contains(b, "pink") {
		t.Errorf("B classes = %q", b)
	}
}

func TestClassInteractivityWarnsNotFails(t *testing.T) {
	r := Parse("classDiagram\n" +
		"  class Shape\n" +
		"  click Shape href \"https://example.com\"\n" +
		"  callback Shape \"cb\"\n" +
		"  link Shape \"https://example.com\"\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	warnings := 0
	for _, d := range r.Diagnostics {
		if d.Severity == SeverityWarning {
			warnings++
		}
	}
	if warnings != 3 {
		t.Errorf("warnings = %d, want 3", warnings)
	}
}

func TestClassMemberTextWithDashesStaysAMember(t *testing.T) {
	r := Parse("classDiagram\n  A : --strange-flag")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Class.Relations) != 0 {
		t.Errorf("relations = %d, want 0", len(r.Class.Relations))
	}
	if got := classNode(&r.Class, "A").Attributes; !slices.Equal(got, []string{"--strange-flag"}) {
		t.Errorf("A attributes = %q", got)
	}
}

func TestClassDemoCorpusParsesClean(t *testing.T) {
	// From demos/classchart.html of mermaid@11.16.0 (MIT).
	r := Parse("classDiagram\n" +
		"  Class01 <|-- AveryLongClass : Cool\n" +
		"  <<interface>> Class01\n" +
		"  Class03 \"0\" *-- \"0..n\" Class04\n" +
		"  Class05 \"1\" o-- \"many\" Class06\n" +
		"  Class07 .. Class08\n" +
		"  Class09 \"many\" --> \"1\" C2 : Where am i?\n" +
		"  Class09 \"0\" --* \"1..n\" C3\n" +
		"  Class09 --|> Class07\n" +
		"  Class07 : equals()\n" +
		"  Class07 : Object[] elementData\n" +
		"  Class01 : #size()\n" +
		"  Class01 : -int chimp\n" +
		"  Class01 : +int gorilla\n" +
		"  Class08 <--> C2: Cool label\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Class.Relations) != 8 {
		t.Errorf("relations = %d, want 8", len(r.Class.Relations))
	}
	c := classNode(&r.Class, "Class01")
	if c.Annotation != "interface" || !slices.Equal(c.Methods, []string{"#size()"}) || len(c.Attributes) != 2 {
		t.Errorf("Class01 = %+v", c)
	}
}
