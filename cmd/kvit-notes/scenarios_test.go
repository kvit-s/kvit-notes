package main

import (
	"testing"

	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/kvit-s/kvit-ui/uitest"
)

// TestScenarios replays the scripted scenarios headlessly; see scenarios.go.
// `kvit-notes --scenario all --out DIR` runs the same ones and saves their
// screenshots.
func TestScenarios(t *testing.T) {
	for _, scn := range scenarios {
		t.Run(scn.name, func(t *testing.T) {
			fails, _ := runScenario(scn, "", "", checkNamed)
			for _, f := range fails {
				t.Error(f)
			}
		})
	}
}

// TestScenariosInTheOtherThemes runs the same scenarios in the three other
// themes, which is where a colour taken from the wrong token or a warning
// unison logs while drawing would show.
func TestScenariosInTheOtherThemes(t *testing.T) {
	if testing.Short() {
		t.Skip("the light theme's run covers the behaviour")
	}
	for _, theme := range []string{tokens.Dark, tokens.Sepia, tokens.HighContrast} {
		for _, scn := range scenarios {
			t.Run(theme+"/"+scn.name, func(t *testing.T) {
				fails, _ := runScenario(scn, "", theme, checkNamed)
				for _, f := range fails {
					t.Error(f)
				}
			})
		}
	}
}

// TestSourceRules holds this repository to the library's rules: no colour
// written as a literal and no font size written as a number.
func TestSourceRules(t *testing.T) {
	if n := uitest.CheckRules(t, "../.."); n < 10 {
		t.Errorf("the rule check read only %d files", n)
	}
}

// checkNamed fails a scenario that ends with a control a screen reader can
// reach but that has no role or no name.
func checkNamed(dr *driver) {
	for _, problem := range uitest.Unnamed(dr.screen.AccessibilityTree(dr.n.win.Window)) {
		dr.fails = append(dr.fails, "screen reader: "+problem)
	}
}
