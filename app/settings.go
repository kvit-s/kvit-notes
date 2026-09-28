package app

// The settings dialog (Kvit's SettingsDialog.qml): Appearance (theme,
// accent and highlight colours, interface size, motion), Typography (the
// note's font, size, line height, block spacing, width and code font),
// General (keeping the app in the tray, where there is one) and This vault
// (where pictures are read from and saved). The first two are kvit-ui's
// settings, which the Qt app shares; the last is the vault's own
// .kvit/settings.json. Each change applies as it is made.

import (
	"errors"
	"fmt"
	"strings"

	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/tokens"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
)

// openSettings shows the settings dialog.
func (w *Window) openSettings() { OpenSettings(w.ui, w.Win, w.Vault) }

// OpenSettings shows the settings dialog over a window; the This vault
// section is there when a vault is open in it.
func OpenSettings(ui *kvitui.UI, win *kvitui.Window, v *vault.Vault) {
	relayout := func() {
		win.Content().MarkForLayoutRecursively()
		win.MarkForRedraw()
	}
	type section struct {
		name  string
		build func() *unison.Panel
	}
	sections := []section{
		{"Appearance", func() *unison.Panel { return appearanceSettings(ui) }},
		{"Typography", func() *unison.Panel { return typographySettings(ui, relayout) }},
	}
	if tray != nil {
		sections = append(sections, section{"General", func() *unison.Panel { return generalSettings(ui) }})
	}
	if v != nil {
		sections = append(sections, section{"This vault", func() *unison.Panel { return vaultSettings(ui, v, relayout) }})
	}
	stack := fillPanel(unison.NewPanel())
	var tabs []*kvitui.Tab
	show := func(i int) {
		stack.RemoveAllChildren()
		section := sections[i].build()
		section.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		stack.AddChild(section)
		for k, t := range tabs {
			t.Selected = k == i
			t.MarkForRedraw()
		}
		stack.MarkForLayoutRecursivelyUpward()
		relayout()
	}
	var row []unison.Paneler
	for i, s := range sections {
		t := kvitui.NewTab(ui, s.name)
		t.OnClick = func() { show(i) }
		tabs = append(tabs, t)
		row = append(row, t)
	}
	show(0)
	body := kvitui.Column(ui, kvitui.SizeSpace, kvitui.Row(ui, kvitui.Px(0), row...), stack)
	d := kvitui.NewDialog(ui, "Settings", body)
	d.ConfirmText, d.CancelText = "", "Close"
	d.Open(win)
}

// settingRow is a setting's name above its control.
func settingRow(ui *kvitui.UI, name string, controls ...unison.Paneler) *unison.Panel {
	label := kvitui.NewLabel(ui, name)
	label.Ink = kvitui.InkTextSecondary
	for _, c := range controls {
		switch c.(type) {
		case *kvitui.Field, *kvitui.Slider, *kvitui.Select, *kvitui.Label:
			// A field, a slider or a list takes the row's width.
			c.AsPanel().SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
		}
	}
	parts := append([]unison.Paneler{label}, controls...)
	col := kvitui.Column(ui, kvitui.SizeSpaceSnug, parts...)
	col.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	return col
}

// colourChoice is a Select of a palette, with "Theme default" first.
func colourChoice(ui *kvitui.UI, label, current string, colours []string, names []string, set func(string)) *kvitui.Select {
	options := []kvitui.Option{{Value: "", Label: "Theme default"}}
	for i, c := range colours {
		options = append(options, kvitui.Option{Value: c, Label: names[i]})
	}
	s := kvitui.NewSelect(ui, label, options...)
	s.Current = current
	if s.Current != "" && !contains(colours, s.Current) {
		s.Options = append(s.Options, kvitui.Option{Value: current, Label: current})
	}
	s.OnChoose = set
	return s
}

func contains(list []string, s string) bool {
	for _, x := range list {
		if strings.EqualFold(x, s) {
			return true
		}
	}
	return false
}

func appearanceSettings(ui *kvitui.UI) *unison.Panel {
	var themes []kvitui.Option
	for _, id := range tokens.AvailableThemes() {
		themes = append(themes, kvitui.Option{Value: id, Label: tokens.DisplayName(id)})
	}
	theme := kvitui.NewSegmented(ui, "Theme", themes...)
	theme.Current = ui.Theme.ThemeID()
	theme.OnChoose = ui.Theme.SetThemeID

	var accents, highlights []string
	for _, c := range tokens.ColorPalette() {
		accents = append(accents, c.Hex())
	}
	for _, c := range tokens.HighlightPalette() {
		highlights = append(highlights, c.Hex())
	}
	accent := colourChoice(ui, "Accent color", ui.Theme.AccentOverride(), accents, tokens.ColorPaletteNames(),
		ui.Theme.SetAccentOverride)
	highlight := colourChoice(ui, "Highlight color", ui.Theme.HighlightOverride(), highlights,
		tokens.HighlightPaletteNames(), ui.Theme.SetHighlightOverride)

	size := kvitui.NewStepper(ui, "Interface size", tokens.MinInterfaceSize, tokens.MaxInterfaceSize)
	size.Unit = "px"
	size.Value = ui.Interface.FontSize()
	size.OnChange = ui.Interface.SetFontSize
	sizeNote := kvitui.NewLabel(ui, "This sizes the sidebar, note list, toolbar, menus and dialogs. The note's own text size is under Typography.")
	sizeNote.Ink, sizeNote.Wrap = kvitui.InkTextSecondary, true

	motion := kvitui.NewSegmented(ui, "Motion",
		kvitui.Option{Value: "system", Label: "As the system"},
		kvitui.Option{Value: "on", Label: "Reduced"},
		kvitui.Option{Value: "off", Label: "Full"})
	motion.Current = ui.Theme.ReducedMotionSetting()
	motion.OnChoose = ui.Theme.SetReducedMotionSetting

	return kvitui.Column(ui, kvitui.SizeSpace,
		settingRow(ui, "Theme", theme),
		settingRow(ui, "Accent color", accent),
		settingRow(ui, "Highlight color", highlight),
		settingRow(ui, "Interface size", size, sizeNote),
		settingRow(ui, "Motion", motion))
}

func typographySettings(ui *kvitui.UI, relayout func()) *unison.Panel {
	ty := ui.Typography
	family := kvitui.NewField(ui)
	family.Label = "Editor font"
	family.Placeholder = "System default"
	family.SetText(ty.FontFamily())
	family.OnChange = func(s string) { ty.SetFontFamily(strings.TrimSpace(s)) }

	size := kvitui.NewStepper(ui, "Font size", tokens.MinBaseSize, tokens.MaxBaseSize)
	size.Unit = "px"
	size.Value = ty.BaseSize()
	size.OnChange = ty.SetBaseSize

	line := kvitui.NewSlider(ui, "Line height", ty.LineHeight())
	line.From, line.To, line.Step, line.Precision = tokens.MinLineHeight, tokens.MaxLineHeight, 0.05, 2
	line.Unit = "×"
	line.OnChange = ty.SetLineHeight

	spacing := kvitui.NewStepper(ui, "Block spacing", tokens.MinParagraphSpacing, tokens.MaxParagraphSpacing)
	spacing.Unit = ""
	spacing.Value = ty.ParagraphSpacing()
	spacing.OnChange = ty.SetParagraphSpacing

	limit := kvitui.NewCheck(ui, "Limit to a width")
	limit.Checked = ty.MaxContentWidth() > 0
	width := kvitui.NewStepper(ui, "Content width", tokens.MinContentWidth, 2000)
	width.Step = 20
	width.Value = ty.MaxContentWidth()
	if width.Value <= 0 {
		width.Value = 700
	}
	width.OnChange = func(v int) {
		if limit.Checked {
			ty.SetMaxContentWidth(v)
		}
	}
	limit.OnChange = func(on bool) {
		if on {
			ty.SetMaxContentWidth(width.Value)
		} else {
			ty.SetMaxContentWidth(0)
		}
	}

	mono := kvitui.NewField(ui)
	mono.Label = "Code font"
	mono.Placeholder = "monospace"
	mono.SetText(ty.MonoFamily())
	mono.OnChange = func(s string) {
		if s = strings.TrimSpace(s); s != "" {
			ty.SetMonoFamily(s)
		}
	}

	reset := kvitui.NewButton(ui, "Reset typography")
	reset.OnClick = func() {
		ty.ResetToDefaults()
		family.SetText(ty.FontFamily())
		size.Value, spacing.Value = ty.BaseSize(), ty.ParagraphSpacing()
		line.Value = ty.LineHeight()
		limit.Checked = ty.MaxContentWidth() > 0
		mono.SetText(ty.MonoFamily())
		relayout()
	}
	return kvitui.Column(ui, kvitui.SizeSpace,
		settingRow(ui, "Editor font", family),
		settingRow(ui, "Font size", size),
		settingRow(ui, "Line height", line),
		settingRow(ui, "Block spacing, in pixels between blocks", spacing),
		settingRow(ui, "Content width, in pixels, centered", limit, width),
		settingRow(ui, "Code font", mono),
		reset)
}

// generalSettings is the General section. The Qt app's also has remote
// content and the update check; this one has the tray, and is shown only
// where the desktop has a notification area, as the Qt app shows its tray
// setting.
func generalSettings(ui *kvitui.UI) *unison.Panel {
	p := newPrefs(ui)
	keep := kvitui.NewCheck(ui, "Keep running in the tray when the window is closed")
	keep.Checked = p.bool(closeToTrayKey, false)
	keep.OnChange = func(on bool) { p.set(closeToTrayKey, on) }
	return kvitui.Column(ui, kvitui.SizeSpace, settingRow(ui, "System tray", keep))
}

func vaultSettings(ui *kvitui.UI, v *vault.Vault, relayout func()) *unison.Panel {
	where := kvitui.NewLabel(ui, fmt.Sprintf("Saved in this vault, in %s", ".kvit/settings.json"))
	where.Ink = kvitui.InkTextSecondary
	explain := kvitui.NewLabel(ui, "A website writes a picture as /images/a.png and keeps the file in its own folder, such as static/ in a Hugo site. Kvit reads a picture path that starts with / from this folder.")
	explain.Ink, explain.Wrap = kvitui.InkTextSecondary, true

	site := kvitui.NewField(ui)
	site.Label = "Folder that paths starting with / point to"
	site.Placeholder = "The vault's own folder"
	site.SetText(v.SiteFolder())
	detected := ""
	if v.Pictures.DetectedFrom != "" {
		detected = "Found from " + v.Pictures.DetectedFrom
	}
	image := kvitui.NewField(ui)
	image.Label = "Folder new pictures are saved in"
	image.SetText(v.ImageFolder())
	status := kvitui.NewLabel(ui, detected)
	status.Ink, status.Wrap = kvitui.InkTextSecondary, true
	apply := func() {
		s, i := site.Text(), image.Text()
		err := v.SetPictureFolders(&s, &i)
		switch {
		case errors.Is(err, vault.ErrName):
			status.Text = "A folder must be inside the vault and not in .kvit"
		case err != nil:
			status.Text = "Could not save this vault's settings: " + err.Error()
		default:
			status.Text = "Saved"
		}
		status.MarkForLayoutAndRedraw()
		relayout()
	}
	save := kvitui.NewButton(ui, "Save")
	save.OnClick = apply
	reset := kvitui.NewButton(ui, "Reset")
	reset.OnClick = func() {
		if err := v.SetPictureFolders(nil, nil); err != nil {
			status.Text = "Could not save this vault's settings: " + err.Error()
		}
		site.SetText(v.SiteFolder())
		image.SetText(v.ImageFolder())
		relayout()
	}
	if v.ReadOnly {
		for _, c := range []interface{ SetEnabled(bool) }{site, image, save, reset} {
			c.SetEnabled(false)
		}
		status.Text = "This vault cannot be written."
	}
	return kvitui.Column(ui, kvitui.SizeSpace, where, explain,
		settingRow(ui, "Pictures", site, image),
		kvitui.Row(ui, kvitui.SizeSpaceSnug, save, reset), status)
}
