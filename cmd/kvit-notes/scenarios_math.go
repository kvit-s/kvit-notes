package main

// The math storyboards of Kvit's tests/tst_visual.qml (test_39_math,
// test_49_inline_math, test_62_math_canary, and the first picture of
// test_36b_table_math_and_column_widths; its second edits one cell, and a Go
// table is edited as its whole Markdown), added to the scenarios. They need
// the math library; without it the equations show as their source.

import (
	"strings"

	"github.com/kvit-s/kvit-notes/editor"
)

func init() {
	scenarios = append(scenarios, mathScenarios...)
}

var mathScenarios = []scenario{
	{"39_math", "# Equations\n\n$$\nE = mc^2\n$$\n\n$$\n\\int_0^1 x^2\\,dx = \\frac{1}{3}\n$$\n", func(dr *driver) {
		dr.expect(dr.kind(1) == editor.Math && dr.kind(2) == editor.Math, "the fences should be equations: %s", dr.blocks())
		dr.clearFocus()
		dr.shot("visual_39_math_01_rendered.png")
		// The first equation's TeX, with the preview under it.
		dr.focus(1, len("E = mc^2"))
		dr.shot("visual_39_math_02_editing_preview.png")
		// TeX that does not typeset shows the error, never nothing.
		setText := func(s string) {
			dr.do(func() {
				d := dr.doc()
				b := d.Block(d.Blocks[1].ID)
				d.Edit("scenario", func() { b.Text = s })
				d.SetCaret(b.ID, 0)
				dr.ed().Refresh()
			})
		}
		setText("a & b")
		dr.shot("visual_39_math_03_error.png")
		setText("E = mc^2")
		dr.focus(0, len("Equations"))
		dr.clearFocus()
		dr.do(func() { dr.ed().EquationNumbers = true; dr.ed().Refresh() })
		dr.shot("visual_39_math_04_numbered.png")
		dr.do(func() { dr.ed().EquationNumbers = false; dr.ed().Refresh() })
		md := ""
		dr.do(func() { md = editor.Serialize(dr.doc().Blocks) })
		dr.expect(strings.Contains(md, "$$\nE = mc^2\n$$"), "the equation should save as a fence: %q", md)
	}},
	{"49_inline_math", "# Inline math\n\nThe relation $E = mc^2$ ties mass to energy, and $\\frac{a}{b}$ is a fraction, " +
		"with $\\sum_{i=1}^{n} i$ too.\n\nIt costs $5 and $6, so the dollars stay literal.", func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_49_math_01_inline_rendered.png")
		// The caret inside the first equation shows its $…$ source; the
		// others stay typeset.
		dr.focus(1, len("The relation $E ="))
		dr.shot("visual_49_math_02_revealed_source.png")
	}},
	{"36b_table_math", "# Formulas in cells\n\n| Quantity $q$ | Definition | Value |\n| :--- | :--- | ---: |\n" +
		"| Area | circle $\\pi r^2$ or $\\frac{a}{b}$ | 12.6 |\n" +
		"| Mean | sample $\\frac{1}{n}\\sum_{i=1}^{n} x_i$ | 4.2 |\n| Plain | no math here | 7 |", func(dr *driver) {
		dr.expect(dr.kind(1) == editor.Table, "the table should be a table: %s", dr.blocks())
		dr.clearFocus()
		dr.shot("visual_36_tables_05_inline_math.png")
	}},
	{"62_math_canary", "# Math render canaries\n\nText before $x^2$ text after\n\n" +
		"Inline stress $x_0^2$ $x_1^2$ $x_2^2$ $x_3^2$ $x_4^2$ $x_5^2$ $x_6^2$ $x_7^2$ $x_8^2$ $x_9^2$\n\n" +
		"Fraction inline $\\frac{a}{b}$ text after\n\nSum inline $\\sum_{i=0}^n i^2$ text after\n\n" +
		"$$\n\\left(\\sum_{i=0}^{n} x_i\\right)^2\n$$", func(dr *driver) {
		dr.clearFocus()
		dr.shot("visual_62_math_canary_01_light.png")
		dr.do(func() { dr.ui.Theme.SetThemeID("dark") })
		dr.clearFocus()
		dr.shot("visual_62_math_canary_02_dark.png")
		dr.do(func() { dr.ui.Theme.SetThemeID("light") })
	}},
}
