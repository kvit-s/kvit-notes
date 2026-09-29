package mermaid

// These tests are the parser tests of the app's tests/test_mermaider.cpp,
// one Go test per test function there and in the same order, with the same
// inputs and expected outputs. The file's layout tests
// (layoutEntityTables, layoutCrowsFootMarkers, layoutNonIdentifyingIsDashed,
// layoutDeterministic and rendererRendersErDiagram) lay the diagram out into
// a scene and belong to the diagram package; demoCorpusParsesClean, which
// comes between them there, is here after the other parser tests. Some
// inputs follow demos/er.html of mermaid@11.16.0 (MIT license, (c) Knut
// Sveidqvist).

import (
	"fmt"
	"slices"
	"testing"
)

func erEntity(a *ErAst, id string) *ErEntity {
	if i := a.IndexOfEntity(id); i >= 0 {
		return &a.Entities[i]
	}
	return nil
}

func TestErFamilyIsSupported(t *testing.T) {
	r := Parse("erDiagram\n  CUSTOMER ||--o{ ORDER : places")
	if r.Type != Er || !r.Supported || r.HasErrors() {
		t.Fatalf("type %v supported %v error %q", r.Type, r.Supported, errorsOf(r))
	}
	if len(r.Er.Entities) != 2 || len(r.Er.Relationships) != 1 {
		t.Fatalf("entities %d relationships %d", len(r.Er.Entities), len(r.Er.Relationships))
	}
	if got := r.Er.Relationships[0].Label; got != "places" {
		t.Errorf("label = %q", got)
	}
}

func TestErSymbolicCardinalities(t *testing.T) {
	cases := []struct {
		op               string
		fromCard, toCard ErCardinality
		identifying      bool
	}{
		{"||--o{", OnlyOne, ZeroOrMore, true},
		{"|o--||", ZeroOrOne, OnlyOne, true},
		{"}o--o|", ZeroOrMore, ZeroOrOne, true},
		{"}|..|{", OneOrMore, OneOrMore, false},
		{"||.-||", OnlyOne, OnlyOne, false},
		{"||-.||", OnlyOne, OnlyOne, false},
	}
	for _, c := range cases {
		r := Parse(fmt.Sprintf("erDiagram\n  A %s B : rel", c.op))
		if r.HasErrors() {
			t.Errorf("%s: error %q", c.op, errorsOf(r))
		}
		if len(r.Er.Relationships) != 1 {
			t.Errorf("%s: relationships = %d, want 1", c.op, len(r.Er.Relationships))
			continue
		}
		rel := r.Er.Relationships[0]
		if rel.FromCard != c.fromCard || rel.ToCard != c.toCard || rel.Identifying != c.identifying {
			t.Errorf("%s: relationship = %+v", c.op, rel)
		}
	}
}

func TestErVerboseCardinalities(t *testing.T) {
	r := Parse("erDiagram\n" +
		"  CAR one or more to zero or more PERSON : driver\n" +
		"  HOUSE one or zero optionally to many ROOM : contains\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	rels := r.Er.Relationships
	if len(rels) != 2 {
		t.Fatalf("relationships = %d, want 2", len(rels))
	}
	if rels[0].FromCard != OneOrMore || rels[0].ToCard != ZeroOrMore || !rels[0].Identifying {
		t.Errorf("relationship 0 = %+v", rels[0])
	}
	if rels[1].FromCard != ZeroOrOne || rels[1].ToCard != ZeroOrMore || rels[1].Identifying {
		t.Errorf("relationship 1 = %+v", rels[1])
	}
}

func TestErAttributeBlocks(t *testing.T) {
	r := Parse("erDiagram\n" +
		"  CUSTOMER {\n" +
		"    string name PK \"customer name\"\n" +
		"    int custNumber PK, FK\n" +
		"    string sector\n" +
		"  }\n" +
		"  CUSTOMER ||--o{ ORDER : places\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	e := erEntity(&r.Er, "CUSTOMER")
	if e == nil || len(e.Attributes) != 3 {
		t.Fatalf("CUSTOMER = %+v", e)
	}
	a0 := e.Attributes[0]
	if a0.Type != "string" || a0.Name != "name" || !slices.Equal(a0.Keys, []string{"PK"}) || a0.Comment != "customer name" {
		t.Errorf("attribute 0 = %+v", a0)
	}
	if got := e.Attributes[1].Keys; !slices.Equal(got, []string{"PK", "FK"}) {
		t.Errorf("attribute 1 keys = %q", got)
	}
	if got := e.Attributes[2].Keys; len(got) != 0 {
		t.Errorf("attribute 2 keys = %q", got)
	}
}

func TestErQuotedEntityNamesAndAliases(t *testing.T) {
	r := Parse("erDiagram\n" +
		"  \"Person Entity\" }|..|{ \"Delivery Address\" : uses\n" +
		"  p[Person] {\n" +
		"    string firstName\n" +
		"  }\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if erEntity(&r.Er, "Person Entity") == nil || erEntity(&r.Er, "Delivery Address") == nil {
		t.Errorf("entities = %+v", r.Er.Entities)
	}
	p := erEntity(&r.Er, "p")
	if p == nil || p.Label != "Person" || len(p.Attributes) != 1 {
		t.Errorf("p = %+v", p)
	}
}

func TestErQuotedRoleAndGenericTypes(t *testing.T) {
	r := Parse("erDiagram\n" +
		"  A ||--|| B : \"has a long role\"\n" +
		"  BOX {\n" +
		"    type~T~ contents\n" +
		"  }\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if got := r.Er.Relationships[0].Label; got != "has a long role" {
		t.Errorf("label = %q", got)
	}
	if got := erEntity(&r.Er, "BOX").Attributes[0].Type; got != "type~T~" {
		t.Errorf("type = %q", got)
	}
}

func TestErStylingAndDirection(t *testing.T) {
	r := Parse("erDiagram\n" +
		"  direction LR\n" +
		"  classDef important fill:#f00\n" +
		"  CUSTOMER:::important ||--o{ ORDER : places\n" +
		"  class ORDER important\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if r.Er.Direction != LR {
		t.Errorf("direction = %v", r.Er.Direction)
	}
	for _, id := range []string{"CUSTOMER", "ORDER"} {
		if got := erEntity(&r.Er, id).CSSClasses; !slices.Contains(got, "important") {
			t.Errorf("%s classes = %q", id, got)
		}
	}
}

func TestErMissingCardinalityOrRoleIsAnError(t *testing.T) {
	if r := Parse("erDiagram\n  A -- B : r"); !r.HasErrors() {
		t.Error("a relationship without cardinalities has no error")
	}
	if r := Parse("erDiagram\n  A ||--o{ B"); !r.HasErrors() {
		t.Error("a relationship without a role has no error")
	}
}

func TestErBareEntityDeclarations(t *testing.T) {
	r := Parse("erDiagram\n  CUSTOMER\n  ORDER\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Er.Entities) != 2 {
		t.Errorf("entities = %d, want 2", len(r.Er.Entities))
	}
}

func TestErDemoCorpusParsesClean(t *testing.T) {
	// From demos/er.html of mermaid@11.16.0 (MIT).
	r := Parse("erDiagram\n" +
		"  CUSTOMER }|..|{ DELIVERY-ADDRESS : has\n" +
		"  CUSTOMER ||--o{ ORDER : places\n" +
		"  CUSTOMER ||--o{ INVOICE : \"liable for\"\n" +
		"  DELIVERY-ADDRESS ||--o{ ORDER : receives\n" +
		"  INVOICE ||--|{ ORDER : covers\n" +
		"  ORDER ||--|{ ORDER-ITEM : includes\n" +
		"  PRODUCT-CATEGORY ||--|{ PRODUCT : contains\n" +
		"  PRODUCT ||--o{ ORDER-ITEM : \"ordered in\"\n")
	if r.HasErrors() {
		t.Errorf("error %q", errorsOf(r))
	}
	if len(r.Er.Relationships) != 8 || len(r.Er.Entities) != 7 {
		t.Errorf("relationships %d entities %d, want 8 and 7", len(r.Er.Relationships), len(r.Er.Entities))
	}
}
