package app

// The Kvit Notes window over one vault: the sidebar (search field and
// scopes), the note list, and the editor pane with its tag strip, side by
// side and resizable, with the status line under them.

import (
	"errors"
	"fmt"
	"path"
	"path/filepath"
	"runtime"
	"slices"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/export"
	"github.com/kvit-s/kvit-notes/query"
	"github.com/kvit-s/kvit-notes/search"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// The panes' widths in design pixels until the reader changes them
// (main.qml).
const (
	sidebarWidth  = 200
	noteListWidth = 260
	outlineWidth  = 220
)

// saveDelay is how long after the last change a note is saved.
const saveDelay = 2 * time.Second

// Window is one Kvit Notes window over a vault.
type Window struct {
	ui     *kvitui.UI
	Win    *kvitui.Window
	Vault  *vault.Vault
	Editor *editor.Editor

	search *kvitui.SearchField
	scopes *ScopeList
	list   *NoteList
	tags   *TagStrip
	status *kvitui.StatusBar
	region *kvitui.Region

	toolbar  *Toolbar
	body     *unison.Panel     // the toolbar over the panes
	split    *kvitui.SplitView // the panes side by side; nil with the editor alone
	main     *unison.Panel     // holds the split, or the editor pane alone
	side     *unison.Panel     // the sidebar
	listPane *unison.Panel     // the note list under its heading
	pane     *unison.Panel     // the editor pane: tag strip and editor
	outline  *Outline          // the open note's headings, at the right
	// backlinks are the notes linking to the open one, left of the outline.
	backlinks *Backlinks
	bodyCache map[string]cachedBody
	queries   *query.Tools // the answers of query blocks
	hidden    bool         // Ctrl+\ hid the sidebar and the note list
	focus     bool         // focus mode: the editor alone, the window maximised
	// maximized is set when focus mode maximized the window, which leaving
	// it undoes.
	maximized   bool
	shownStatus unison.Paneler // the status line, when it is shown
	prefs       *prefs
	scope       Scope
	sortBy      string // "modified", "created" or "title"
	ascend      bool
	heading     *kvitui.Label // the note list's title: the scope it shows
	sortDir     *kvitui.IconButton
	shown       []*vault.Entry // the notes the list shows, in order
	open        *vault.Entry   // the note in the editor, or nil
	page        *vault.Page    // its front matter and body as loaded
	saveAt      time.Time      // when the pending save is due; zero for none
	message     string         // what the status line says about saving

	journalAt time.Time       // when the unsaved text is next journalled; zero for none
	banner    *unison.Panel   // above the note list: recovered unsaved changes
	theirs    *unison.Panel   // above the note: its file changed elsewhere
	stopWatch func()          // stops following other programs' changes
	trashList []vault.Trashed // what the list shows in the trash scope
	trashed   *vault.Trashed  // the trashed note shown read-only, or nil
	addButton *kvitui.IconButton

	capture *kvitui.Window // the quick capture window, while it is open
	finder  *finder        // the find bar, made the first time it opens

	// Searching across notes (searchview.go).
	index         *search.Index
	indexed       map[string]time.Time // each note's time of change when indexed
	results       *ResultsList
	resultsRegion *kvitui.Region
	listRegion    *kvitui.Region
	listHead      *unison.Panel
	sortRow       *unison.Panel
	searchRow     *unison.Panel // the match count and the date menu
	searchCount   *kvitui.Label
	searchDates   *kvitui.Select

	bulkRow      *unison.Panel // the bar over the list while notes are picked
	bulkCount    *kvitui.Label
	sessionWords int             // the open note's words when it was opened
	stats        *kvitui.Popover // the statistics, while they are shown

	back, forward []string      // the notes Back and Forward return to
	travelling    bool          // Back or Forward is opening a note
	switcher      *kvitui.Popup // the quick switcher, while it is open
}

// Open opens a window over a vault.
func Open(ui *kvitui.UI, v *vault.Vault) (*Window, error) {
	win, err := kvitui.NewWindow(ui, filepath.Base(v.Root)+" — Kvit Notes")
	if err != nil {
		return nil, err
	}
	w := &Window{ui: ui, Win: win, Vault: v, prefs: newPrefs(ui)}

	w.search = kvitui.NewSearchField(ui)
	w.search.Placeholder = "Search all notes"
	w.search.OnChange = func(string) { w.refreshList() }
	w.scopes = NewScopeList(ui)
	w.scopes.OnChoose = func(s Scope) {
		if w.scope.Kind == ScopeTrash && s.Kind != ScopeTrash && w.trashed != nil {
			w.trashed = nil
			if last := v.Find(v.State.LastOpenNote); last != nil {
				w.openNote(last)
			}
		}
		w.scope = s
		w.list.ClearSelection()
		w.refreshList()
	}
	w.scopes.OnToggle = func(folder string, expanded bool) {
		v.State.SetFolderExpanded(folder, expanded)
		w.refreshScopes()
	}
	notesLabel := kvitui.NewLabel(ui, "Notes")
	notesLabel.Role, notesLabel.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	newFolder := kvitui.NewIconButton(ui, "folder-plus", "New folder")
	newFolder.OnClick = w.newFolder
	hideSide := kvitui.NewIconButton(ui, "caret-double-left", "Hide the sidebar")
	hideSide.OnClick = func() { w.showPane("panels.sidebarCollapsed", false) }
	sideHead := headerRow(ui, notesLabel, newFolder, hideSide)
	side := kvitui.Column(ui, kvitui.SizeSpaceSnug, sideHead, w.search, w.scopes)
	sideHead.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	side.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	w.scopes.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.search.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	side.DrawCallback = ground(ui, side, func() kvitui.Ink { return kvitui.InkPanelBackground })

	w.list = NewNoteList(ui)
	w.list.OnOpen = func(i int) {
		if w.scope.Kind == ScopeTrash {
			w.showTrashed(w.trashList[i])
			return
		}
		w.openNote(w.shown[i])
	}
	listRegion := kvitui.NewRegion(ui, w.list)
	listRegion.Padding = kvitui.Px(0)
	w.listRegion = listRegion
	w.results = newResultsList(ui)
	w.results.OnOpen = w.openResult
	w.resultsRegion = kvitui.NewRegion(ui, w.results)
	w.resultsRegion.Padding = kvitui.Px(0)
	w.resultsRegion.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.searchCount = kvitui.NewLabel(ui, "")
	w.searchCount.Ink = kvitui.InkTextSecondary
	var dates []kvitui.Option
	for _, c := range dateChoices {
		dates = append(dates, kvitui.Option{Value: c.label, Label: c.label})
	}
	w.searchDates = kvitui.NewSelect(ui, "Modified", dates...)
	w.searchDates.OnChoose = func(string) { w.refreshList() }
	w.searchCount.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	w.searchRow = kvitui.Row(ui, kvitui.SizeSpaceSnug, w.searchCount, kvitui.Width(ui, kvitui.Px(130), w.searchDates))
	w.searchRow.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	w.sortBy = "modified"
	w.heading = kvitui.NewLabel(ui, "All Notes")
	w.heading.Role, w.heading.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	w.addButton = kvitui.NewIconButton(ui, "plus", "New note (Ctrl+N)")
	w.addButton.OnClick = w.addOrEmpty
	sortBy := kvitui.NewSelect(ui, "Sort notes by",
		kvitui.Option{Value: "modified", Label: "Modified"},
		kvitui.Option{Value: "created", Label: "Created"},
		kvitui.Option{Value: "title", Label: "Title"},
		kvitui.Option{Value: "manual", Label: "Manual"})
	sortBy.OnChoose = func(value string) {
		w.sortBy = value
		w.prefs.set("noteList.sortMode", value)
		w.refreshList()
	}
	w.sortDir = kvitui.NewIconButton(ui, "sort-descending", "Newest first")
	w.sortDir.OnClick = func() {
		w.ascend = !w.ascend
		w.prefs.set("noteList.ascending", w.ascend)
		w.showSortDirection()
		w.refreshList()
	}
	hideList := kvitui.NewIconButton(ui, "caret-double-left", "Hide the note list")
	hideList.OnClick = func() { w.showPane("panels.noteListCollapsed", false) }
	sortRow := kvitui.Row(ui, kvitui.SizeSpaceSnug, sortBy, w.sortDir, hideList)
	sortBy.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	listHead := kvitui.Column(ui, kvitui.SizeSpaceSnug, headerRow(ui, w.heading, w.addButton), sortRow)
	w.listHead, w.sortRow = listHead, sortRow
	w.bulkRow = w.newBulkRow()
	w.list.OnSelect = w.showBulk
	listHead.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	w.banner = kvitui.Column(ui, kvitui.SizeSpaceSnug)
	w.banner.SetBorder(kvitui.Insets(ui, kvitui.Px(0), kvitui.SizeSpace, kvitui.Px(0), kvitui.SizeSpace))
	listPane := kvitui.Column(ui, kvitui.Px(0), listHead, w.banner, listRegion)
	for _, p := range []*unison.Panel{listHead, sortRow, w.banner} {
		p.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	}
	listRegion.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	listPane.DrawCallback = ground(ui, listPane, func() kvitui.Ink { return kvitui.InkListBackground })

	w.Editor = editor.New(ui, editor.NewDoc(nil))
	w.Editor.OnChange = w.edited
	w.Editor.PickImage = w.pickImage
	w.Editor.FormatBar = true
	w.Editor.FollowLink = w.followLink
	w.Editor.CompleteLink = w.completeLink
	w.Editor.SetMathCommands(MathCommands(ui))
	w.Editor.CopyRich = w.copyRich
	w.Editor.RunQuery = w.runQuery
	w.Editor.OpenNote = func(p string) {
		if e := v.Find(p); e != nil {
			w.openNote(e)
		}
	}
	w.Editor.PasteRich = pasteRich
	w.Editor.BlocksHTML = func(blocks []editor.Block, indexes []int) string {
		return export.HTMLFromSelection(blocks, indexes, "", w.exportOptions())
	}
	w.Editor.OnLink = w.openLinkDialog
	w.Editor.LoadPreview = w.loadPreview
	w.wireDiagrams()
	w.Editor.OpenURL = func(address string) {
		if err := unison.OpenBrowser(address); err != nil {
			w.fail("Could not open "+address, err)
		}
	}
	w.tags = NewTagStrip(ui)
	w.tags.OnAdd = func(tag string) { w.setTags(append(w.page.Tags(), tag)) }
	w.tags.OnRemove = func(tag string) {
		w.setTags(slices.DeleteFunc(w.page.Tags(), func(t string) bool { return t == tag }))
	}
	w.region = kvitui.NewRegion(ui, w.Editor)
	w.region.Padding = kvitui.Px(0)
	versions := kvitui.NewIconButton(ui, "clock-counter-clockwise", "Earlier versions of this note")
	versions.OnClick = w.openBackups
	w.tags.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	strip := kvitui.Row(ui, kvitui.SizeSpace, w.tags, versions)
	strip.SetBorder(kvitui.Insets(ui, kvitui.Px(12), kvitui.Px(24), kvitui.Px(0), kvitui.Px(24)))
	w.theirs = kvitui.Column(ui, kvitui.SizeSpaceSnug)
	w.theirs.SetBorder(kvitui.Insets(ui, kvitui.Px(0), kvitui.Px(24), kvitui.Px(0), kvitui.Px(24)))
	w.theirs.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	pane := kvitui.Column(ui, kvitui.Px(0), strip, w.theirs, w.region)
	w.pane = pane
	w.region.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	strip.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})

	pane.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.side, w.listPane = side, listPane
	w.outline = NewOutline(ui, w.Editor)
	w.outline.Levels = w.prefs.int("view.outlineLevels", 0xF)
	w.outline.OnLevels = func(levels int) { w.prefs.set("view.outlineLevels", levels) }
	w.outline.OnClose = func() { w.showPane("view.outline", false) }
	w.outline.OnGo = w.goToBlock
	w.backlinks = newBacklinks(ui, func(p string) {
		if e := v.Find(p); e != nil {
			w.openNote(e)
		}
	}, func() { w.showPane("view.backlinks", false) })
	w.main = fillPanel(unison.NewPanel())
	w.toolbar = NewToolbar(ui, w.Editor, ToolbarHooks{File: w.fileMenu, View: w.viewMenu,
		Back: w.goBack, Forward: w.goForward, Link: w.openLinkDialog})
	w.toolbar.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	body := fillPanel(unison.NewPanel())
	body.AddChild(w.toolbar)
	body.AddChild(w.main)
	w.body = body
	w.list.OnDrag = w.dragNote
	w.list.OnDrop = w.dropNote
	w.status = kvitui.NewStatusBar(ui)
	w.status.OnFact = func(_, fact int) {
		switch fact {
		case 0:
			w.openStatistics()
		case 1:
			w.openGoal()
		}
	}
	win.SetBody(body)
	win.SidebarVisible = false
	win.OnKeyDown = w.keyDown
	win.WillCloseCallback = chain(win.WillCloseCallback, func() {
		w.forget()
		w.saveNow()
		if w.stopWatch != nil {
			w.stopWatch()
		}
		_ = v.SaveState()
		v.Close()
	})
	ClosesToTray(ui, win, w.saveNow)
	AcceptDroppedNotes(win, w.openDropped)

	w.hidden = !w.prefs.bool("panels.visible", true)
	w.sortBy = w.prefs.string("noteList.sortMode", "modified")
	w.ascend = w.prefs.bool("noteList.ascending", false)
	sortBy.Current = w.sortBy
	w.showSortDirection()
	w.arrange()
	w.installMenus()
	w.buildIndex()
	w.refreshScopes()
	w.refreshList()
	if last := v.Find(v.State.LastOpenNote); last != nil {
		w.openNote(last)
	} else if len(w.shown) > 0 {
		w.openNote(w.shown[0])
	}
	if w.open != nil {
		// The keyboard starts in the note, at its start, as in the Qt app.
		w.Editor.FocusBlock(0, 0)
	}
	if v.ReadOnly {
		w.message = "This vault cannot be written, so it is open for reading only"
		w.update()
	}
	w.offerRecovery()
	w.watch()
	w.tick()
	windows = append(windows, w)
	w.prefs.rememberVault(v.Root)
	w.recordOpenVaults()
	w.Editor.LineNumbers = w.prefs.bool("view.codeLineNumbers", false)
	w.Editor.EquationNumbers = w.prefs.bool("view.equationNumbers", false)
	if w.prefs.bool("view.typewriterMode", false) {
		w.Editor.Typewriter = w.region
	}
	return w, nil
}

// recordOpenVaults writes the vaults open in windows to the settings, which
// is where the next start, of this app or the Qt one, opens.
func (w *Window) recordOpenVaults() {
	var roots []string
	for _, o := range windows {
		roots = append(roots, o.Vault.Root)
	}
	w.prefs.setStrings("session.openVaults", roots)
}

// forget takes a closing window out of the list of windows. The last one
// stays in the settings' open vaults, so the next start opens it again.
func (w *Window) forget() {
	i := slices.Index(windows, w)
	if i < 0 {
		return
	}
	windows = slices.Delete(windows, i, i+1)
	if len(windows) > 0 {
		windows[0].recordOpenVaults()
	}
}

// addOrEmpty is the note list's button: a new note, or in the trash,
// emptying it.
func (w *Window) addOrEmpty() {
	if w.scope.Kind == ScopeTrash {
		w.confirmEmptyTrash()
		return
	}
	w.newNote()
}

// stateColour is a folder's or tag's colour as collection.json keeps it
// ("#rrggbb"), chosen by the reader, and whether there is one.
func stateColour(hex string) (palette.Color, bool) {
	if hex == "" {
		return palette.Color{}, false
	}
	c, err := palette.ParseHex(hex)
	return c, err == nil
}

// headerRow is a pane's title with its buttons at the right.
func headerRow(ui *kvitui.UI, title *kvitui.Label, buttons ...unison.Paneler) *unison.Panel {
	title.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	return kvitui.Row(ui, kvitui.SizeSpaceSnug, append([]unison.Paneler{title}, buttons...)...)
}

// scopeTitle is what the note list's heading says for a scope.
func scopeTitle(s Scope) string {
	switch s.Kind {
	case ScopeFavorites:
		return "Favorites"
	case ScopeFolder:
		return path.Base(s.Path)
	case ScopeTag:
		return "#" + s.Path
	case ScopeTrash:
		return "Trash"
	}
	return "All Notes"
}

// newFolder makes a folder in the folder shown, named "New folder" or the
// first free "New folder N", and shows it.
func (w *Window) newFolder() {
	parent := ""
	if w.scope.Kind == ScopeFolder {
		parent = w.scope.Path
	}
	for n := 1; ; n++ {
		name := "New folder"
		if n > 1 {
			name = fmt.Sprintf("New folder %d", n)
		}
		rel, err := w.Vault.CreateFolder(parent, name)
		if errors.Is(err, vault.ErrExists) {
			continue
		}
		if err != nil {
			w.message = "Could not make a folder: " + err.Error()
			w.update()
			return
		}
		if parent != "" {
			w.Vault.State.SetFolderExpanded(parent, true)
		}
		w.scope = Scope{Kind: ScopeFolder, Path: rel}
		w.refreshScopes()
		w.refreshList()
		return
	}
}

// chain runs two callbacks in turn.
func chain(a, b func()) func() {
	return func() {
		if a != nil {
			a()
		}
		b()
	}
}

// ground fills a panel with a surface colour.
func ground(ui *kvitui.UI, p *unison.Panel, ink func() kvitui.Ink) func(gc *unison.Canvas, _ geom.Rect) {
	return func(gc *unison.Canvas, _ geom.Rect) {
		r := p.ContentRect(true)
		gc.DrawRect(r, kvitui.Color(ink().Of(ui)).Paint(gc, r, paintstyle.Fill))
	}
}

func (w *Window) keyDown(key unison.KeyCode, mods mod.Modifiers, _ bool) bool {
	if mods == mod.Option && runtime.GOOS != "darwin" && !w.focus && w.toolbar.OpenMenu(key) {
		// Alt and a menu's letter, as on Windows and Linux; Option and a
		// letter types a character on macOS, where the menus are in the menu
		// bar.
		return true
	}
	if mods.OptionDown() && !mods.OSMenuCommandDown() {
		switch key {
		case unison.KeyLeft:
			w.goBack()
			return true
		case unison.KeyRight:
			w.goForward()
			return true
		}
	}
	switch {
	case key == unison.KeyF11 && mods == 0:
		w.toggleFocus()
		return true
	case key == unison.KeyEscape && mods == 0 && w.focus:
		w.toggleFocus()
		return true
	}
	if key == unison.KeyB && mods.OSMenuCommandDown() && mods.ShiftDown() {
		w.showPane("view.backlinks", !w.paneOn("view.backlinks"))
		return true
	}
	if !mods.OSMenuCommandDown() {
		return false
	}
	if key == unison.KeyN && mods.OptionDown() {
		w.openCapture()
		return true
	}
	switch key {
	case unison.KeyK:
		w.openLinkDialog()
	case unison.KeyF:
		w.openFind(false)
	case unison.KeyH:
		w.openFind(true)
	case unison.KeyP:
		w.openSwitcher()
	case unison.KeyS:
		w.saveNow()
	case unison.KeyN:
		w.newNote()
	case unison.KeyBackslash:
		w.toggleSides()
	default:
		return false
	}
	return true
}

// toggleSides hides the sidebar and the note list, giving the editor the
// whole window, or brings them back (Ctrl+\, as in the Qt app).
func (w *Window) toggleSides() {
	w.hidden = !w.hidden
	w.prefs.set("panels.visible", !w.hidden)
	w.arrange()
	w.Editor.RequestFocus()
}

// showSortDirection shows the sort direction on its button.
func (w *Window) showSortDirection() {
	w.sortDir.Symbol, w.sortDir.Label = "sort-descending", "Newest first"
	if w.ascend {
		w.sortDir.Symbol, w.sortDir.Label = "sort-ascending", "Oldest first"
	}
	w.sortDir.MarkForRedraw()
}

// shown reports whether a pane is on: the sidebar and the note list unless
// the reader hid them, the outline when the reader showed it.
func (w *Window) paneOn(key string) bool {
	switch key {
	case "view.outline", "view.backlinks":
		return w.prefs.bool(key, false)
	}
	return !w.prefs.bool(key, false)
}

// showPane shows or hides one pane: "panels.sidebarCollapsed",
// "panels.noteListCollapsed" or "view.outline".
func (w *Window) showPane(key string, on bool) {
	if key == "view.outline" || key == "view.backlinks" {
		w.prefs.set(key, on)
	} else {
		w.prefs.set(key, !on)
		if on {
			w.hidden = false
			w.prefs.set("panels.visible", true)
		}
	}
	w.arrange()
}

// arrange puts the panes that are on side by side: the sidebar, the note
// list, the editor pane and the outline, at the widths the reader left
// them. Focus mode leaves the editor alone.
func (w *Window) arrange() {
	type slot struct {
		p     *unison.Panel
		key   string // where its width is remembered
		width int
	}
	var slots []slot
	fill := 0
	if !w.focus && !w.hidden {
		if w.paneOn("panels.sidebarCollapsed") {
			slots = append(slots, slot{w.side, "panels.sidebarWidth", sidebarWidth})
		}
		if w.paneOn("panels.noteListCollapsed") {
			slots = append(slots, slot{w.listPane, "panels.noteListWidth", noteListWidth})
		}
	}
	fill = len(slots)
	slots = append(slots, slot{p: w.pane})
	if !w.focus && w.paneOn("view.backlinks") {
		slots = append(slots, slot{w.backlinks.AsPanel(), "view.backlinksWidth", backlinksWidth})
	}
	if !w.focus && w.paneOn("view.outline") {
		slots = append(slots, slot{w.outline.AsPanel(), "view.outlineWidth", outlineWidth})
		w.outline.Refresh()
	}
	for _, p := range []*unison.Panel{w.side, w.listPane, w.pane, w.outline.AsPanel(), w.backlinks.AsPanel()} {
		p.RemoveFromParent()
	}
	w.main.RemoveAllChildren()
	w.split = nil
	if len(slots) == 1 {
		w.main.AddChild(w.pane)
	} else {
		var panes []unison.Paneler
		for _, sl := range slots {
			panes = append(panes, sl.p)
		}
		w.split = kvitui.NewSplitView(w.ui, panes...)
		w.split.Fill = fill
		for i, sl := range slots {
			if i != fill {
				w.split.SetSize(i, kvitui.Px(w.prefs.int(sl.key, sl.width)))
			}
		}
		w.split.OnResize = func(pane, design int) {
			if pane >= 0 && pane < len(slots) && slots[pane].key != "" {
				w.prefs.set(slots[pane].key, design)
			}
		}
		w.split.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
		w.main.AddChild(w.split)
	}
	w.refreshBacklinks()
	w.Editor.Centered = w.focus
	w.toolbar.RemoveFromParent()
	if !w.focus {
		w.body.AddChildAtIndex(w.toolbar, 0)
	}
	var status unison.Paneler
	if !w.focus && w.prefs.bool("view.statusBar", true) {
		status = w.status
	}
	if status != w.shownStatus {
		w.shownStatus = status
		w.Win.SetStatusBar(status)
	}
	w.Editor.Refresh()
	w.relayout()
}

// reorder moves the note in row i to the gap before row line, in the
// folder's manual order.
func (w *Window) reorder(i, line int) {
	if i < 0 || i >= len(w.shown) {
		return
	}
	e := w.shown[i]
	target := line
	if target > i {
		target--
	}
	// The gap is in the list as shown; the order is kept from the oldest
	// end, which the list shows last when it is turned around.
	pos := target
	if !w.ascend {
		pos = len(w.shown) - 1 - target
	}
	if err := w.Vault.SetManualPosition(e, pos); err != nil {
		w.fail("Could not move the note", err)
	}
	w.refreshList()
}

// fillPanel makes a panel one column that fills the room it is given.
func fillPanel(p *unison.Panel) *unison.Panel {
	p.SetLayout(&unison.FlexLayout{Columns: 1, HAlign: align.Fill, VAlign: align.Fill})
	p.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	return p
}

// SidesHidden reports whether Ctrl+\ has hidden the side panes.
func (w *Window) SidesHidden() bool { return w.hidden }

// dropTarget is the folder a note dragged to a point in the window would go
// to: the folder under it in the sidebar ("" for All Notes, the top of the
// vault), and whether there is one.
func (w *Window) dropTarget(where geom.Point) (int, string, bool) {
	i := w.scopes.RowAt(w.scopes.PointFromRoot(where))
	if i < 0 {
		return -1, "", false
	}
	switch s := w.scopes.Rows[i].Scope; s.Kind {
	case ScopeFolder:
		return i, s.Path, true
	case ScopeAll:
		return i, "", true
	}
	return -1, "", false
}

// dragNote shows where a dragged note would go.
func (w *Window) dragNote(_ int, where geom.Point) {
	i, _, _ := w.dropTarget(where)
	if i != w.scopes.Target {
		w.scopes.Target = i
		w.scopes.MarkForRedraw()
	}
	line := -1
	if p := w.list.PointFromRoot(where); w.manualOrder() && p.In(w.list.ContentRect(false)) {
		line = w.list.gapAt(p.Y)
	}
	if line != w.list.DropLine {
		w.list.DropLine = line
		w.list.MarkForRedraw()
	}
}

// dropNote moves a dragged note into the folder it was dropped on.
func (w *Window) dropNote(i int, where geom.Point) {
	w.scopes.Target = -1
	w.scopes.MarkForRedraw()
	if line := w.list.DropLine; line >= 0 {
		w.list.DropLine = -1
		w.reorder(i, line)
		return
	}
	_, folder, ok := w.dropTarget(where)
	if !ok || i < 0 || i >= len(w.shown) {
		return
	}
	e := w.shown[i]
	if e.Folder == folder {
		return
	}
	w.relocate(e, func() error { return w.Vault.Move(e, folder) }, func() {
		w.refreshScopes()
		w.refreshList()
		w.update()
	})
}

// refreshScopes rebuilds the sidebar's rows from the vault.
func (w *Window) refreshScopes() {
	v := w.Vault
	rows := []ScopeRow{
		{Scope: Scope{Kind: ScopeAll}, Label: "All Notes", Count: len(v.Entries)},
		{Scope: Scope{Kind: ScopeFavorites}, Label: "★ Favorites", Count: v.CountFavorites()},
		{Heading: true, Label: "Folders"},
	}
	var add func(parent string, depth int)
	add = func(parent string, depth int) {
		for _, f := range v.Folders {
			if vault.Parent(f.Path) != parent {
				continue
			}
			sub, open := v.HasSubfolders(f.Path), v.State.FolderExpanded(f.Path)
			row := ScopeRow{Scope: Scope{Kind: ScopeFolder, Path: f.Path}, Label: f.Name,
				Count: v.CountIn(f.Path), Depth: depth, Expandable: sub, Expanded: open}
			row.Mark, row.HasMark = stateColour(v.State.Folders[f.Path].Color)
			rows = append(rows, row)
			if sub && open {
				add(f.Path, depth+1)
			}
		}
	}
	add("", 0)
	var names []string
	for _, t := range v.Tags() {
		names = append(names, t.Name)
	}
	w.tags.SetSuggestions(names)
	if tags := v.Tags(); len(tags) > 0 {
		rows = append(rows, ScopeRow{Heading: true, Label: "Tags"})
		for _, t := range tags {
			row := ScopeRow{Scope: Scope{Kind: ScopeTag, Path: t.Name}, Label: "#" + t.Name, Count: t.Count}
			row.Mark, row.HasMark = stateColour(v.State.TagColors[t.Name])
			rows = append(rows, row)
		}
	}
	rows = append(rows, ScopeRow{Scope: Scope{Kind: ScopeTrash}, Label: "Trash", Count: v.TrashCount()})
	w.scopes.SetRows(rows)
	w.scopes.Current = w.scope
}

// inScope reports whether a note belongs in the list for the current scope
// and search.
func (w *Window) inScope(e *vault.Entry) bool {
	switch w.scope.Kind {
	case ScopeFavorites:
		if !e.Favorite {
			return false
		}
	case ScopeFolder:
		if e.Folder != w.scope.Path {
			return false
		}
	case ScopeTag:
		if !slices.Contains(e.Tags, w.scope.Path) {
			return false
		}
	case ScopeTrash:
		return false
	}
	return true
}

// refreshList rebuilds the note list for the current scope, pinned notes
// first, the most recently changed next.
func (w *Window) refreshList() {
	w.addButton.Symbol, w.addButton.Label = "plus", "New note (Ctrl+N)"
	if w.scope.Kind == ScopeTrash {
		w.addButton.Symbol, w.addButton.Label = "trash", "Empty the trash"
		w.showSearch(false)
		w.refreshTrash()
		return
	}
	w.addButton.MarkForRedraw()
	if strings.TrimSpace(w.search.Text()) != "" && w.index != nil {
		w.heading.Text = scopeTitle(w.scope)
		w.heading.MarkForRedraw()
		w.runSearch()
		return
	}
	w.showSearch(false)
	w.shown = w.shown[:0]
	for _, e := range w.Vault.Entries {
		if w.inScope(e) {
			w.shown = append(w.shown, e)
		}
	}
	w.sortShown()
	w.heading.Text = scopeTitle(w.scope)
	w.heading.MarkForRedraw()
	items := make([]ListItem, len(w.shown))
	for i, e := range w.shown {
		items[i] = ListItem{Key: e.Path, Title: e.Title, Snippet: e.Snippet, Pinned: e.Pinned, Favorite: e.Favorite,
			Details: fmt.Sprintf("%s · %d words", e.Modified.Format("Jan 2, 2006 15:04"), e.Words)}
	}
	current := ""
	if w.open != nil {
		current = w.open.Path
	}
	w.list.SetItems(items, current)
}

// manualOrder reports whether the list is in the reader's own order: the
// Manual sort, which only a folder has; elsewhere it is by title.
func (w *Window) manualOrder() bool { return w.sortBy == "manual" && w.scope.Kind == ScopeFolder }

// sortShown puts the notes shown in order, as the Qt note list does
// (NoteListModel): by the sort chosen, each way, ties by path, the Manual
// order as the reader left it (reversed when turned around), and pinned
// notes first in every order.
func (w *Window) sortShown() {
	if w.manualOrder() {
		order := w.Vault.ManualOrder(w.scope.Path)
		shown := map[*vault.Entry]bool{}
		for _, e := range w.shown {
			shown[e] = true
		}
		w.shown = w.shown[:0]
		for _, e := range order {
			if shown[e] {
				w.shown = append(w.shown, e)
			}
		}
		if !w.ascend {
			slices.Reverse(w.shown)
		}
	} else {
		slices.SortStableFunc(w.shown, func(a, b *vault.Entry) int {
			var c int
			switch w.sortBy {
			case "created":
				c = createdOf(a).Compare(createdOf(b))
			case "title", "manual":
				c = compareFold(a.Title, b.Title)
			default:
				c = a.Modified.Compare(b.Modified)
			}
			if c == 0 {
				c = strings.Compare(a.Path, b.Path)
			}
			if !w.ascend {
				c = -c
			}
			return c
		})
	}
	slices.SortStableFunc(w.shown, func(a, b *vault.Entry) int {
		switch {
		case a.Pinned == b.Pinned:
			return 0
		case a.Pinned:
			return -1
		}
		return 1
	})
}

// compareFold compares two titles without regard to case.
func compareFold(a, b string) int { return strings.Compare(strings.ToLower(a), strings.ToLower(b)) }

// refreshTrash lists what is in the trash.
func (w *Window) refreshTrash() {
	w.addButton.MarkForRedraw()
	w.heading.Text = scopeTitle(w.scope)
	w.heading.MarkForRedraw()
	w.trashList = w.Vault.TrashItems()
	items := make([]ListItem, len(w.trashList))
	current := ""
	for i, t := range w.trashList {
		kind := "Note"
		if t.Dir {
			kind = "Folder"
		}
		items[i] = ListItem{Key: t.Name, Title: t.Title, Snippet: kind,
			Details: "Moved to the trash " + t.Time.Format("Jan 2, 2006 15:04")}
		if w.trashed != nil && w.trashed.Name == t.Name {
			current = t.Name
		}
	}
	w.list.SetItems(items, current)
}

// createdOf is when a note was made: its front matter's date, else its file's.
func createdOf(e *vault.Entry) time.Time {
	if !e.Created.IsZero() {
		return e.Created
	}
	return e.Modified
}

// openNote saves the note being edited and shows another.
func (w *Window) openNote(e *vault.Entry) {
	if e == w.open {
		return
	}
	w.saveNow()
	w.remember(w.open)
	page, err := w.Vault.Load(e.Path)
	if err != nil {
		w.message = "Could not open " + e.Title + ": " + err.Error()
		w.update()
		return
	}
	w.open, w.trashed = e, nil
	w.Vault.State.LastOpenNote = e.Path
	w.show(page)
	w.theirs.RemoveAllChildren()
	w.region.ScrollTo(0)
	w.message = ""
	w.refreshList()
	w.update()
}

// newNote creates an untitled note in the current folder and opens it.
func (w *Window) newNote() {
	folder := ""
	if w.scope.Kind == ScopeFolder {
		folder = w.scope.Path
	}
	e, err := w.Vault.Create(folder)
	if err != nil {
		w.message = "Could not create a note: " + err.Error()
		w.update()
		return
	}
	w.refreshScopes()
	w.refreshList()
	w.openNote(e)
	w.Editor.FocusBlock(0, 0)
}

// edited follows every change the editor reports: a changed note is saved
// a little after the typing stops.
func (w *Window) edited() {
	if w.open != nil && w.Editor.Doc.Dirty {
		if w.saveAt.IsZero() {
			w.saveAt = time.Now().Add(saveDelay)
		}
		if w.journalAt.IsZero() {
			w.journalAt = time.Now().Add(journalDelay)
		}
	}
	w.autoTitle()
	if w.finder != nil && w.finder.hide != nil && w.Editor.Doc.Dirty {
		w.finder.recompute(false)
	}
	if w.outline.Window() != nil {
		w.outline.Refresh()
	}
	w.update()
}

// goToBlock scrolls the note so a block is at the top of the view, and
// puts the caret at its start.
func (w *Window) goToBlock(i int) {
	if i < 0 || i >= len(w.Editor.Doc.Blocks) {
		return
	}
	w.Editor.FocusBlock(i, 0)
	w.region.ScrollTo(w.Editor.RowRect(i).Y - 8)
}

// tick saves a note whose save has come due, twice a second while the
// window is open.
func (w *Window) tick() {
	if w.Win.Window == nil || !w.Win.IsValid() {
		return
	}
	if !w.journalAt.IsZero() && time.Now().After(w.journalAt) {
		w.journal()
	}
	if !w.saveAt.IsZero() && time.Now().After(w.saveAt) {
		w.saveNow()
	}
	unison.InvokeTaskAfter(w.tick, 500*time.Millisecond)
}

// saveNow writes the open note if it has changed.
func (w *Window) saveNow() {
	w.saveAt = time.Time{}
	if w.open == nil || !w.Editor.Doc.Dirty {
		return
	}
	w.page.Body = editor.Serialize(w.Editor.Doc.Blocks)
	if err := w.Vault.Save(w.open, w.page); err != nil {
		w.message = "Save failed: " + err.Error()
	} else {
		w.Editor.Doc.Dirty = false
		w.journalAt = time.Time{}
		w.Vault.ClearJournal(w.open.Path)
		w.message = ""
		w.refreshList()
	}
	w.update()
}

// setTags changes the open note's tags and saves it.
func (w *Window) setTags(tags []string) {
	if w.open == nil {
		return
	}
	w.page.SetTags(tags)
	w.tags.SetTags(w.page.Tags())
	w.Editor.Doc.Dirty = true
	w.saveNow()
	w.refreshScopes()
}

// update shows the save state and the caret's place in the status line.
func (w *Window) update() {
	d := w.Editor.Doc
	st := "Saved"
	switch {
	case w.message != "":
		st = w.message
	case w.Vault.ReadOnly:
		st = "Read only"
	case d.Dirty:
		st = "Unsaved"
	}
	w.status.Activity = st
	var facts []string
	if b := d.CaretBlock(); b != nil && d.Focused {
		i := d.Index(b.ID)
		facts = append(facts, fmt.Sprintf("Block %d", i+1), b.Kind.String())
	}
	switch {
	case w.open != nil:
		facts = append(facts, w.open.Path)
	case w.trashed != nil:
		facts = append(facts, "In the trash, read only")
	}
	stats := d.Stats()
	facts = append(facts, fmt.Sprintf("%d blocks", stats.Blocks), fmt.Sprintf("%d chars", stats.Chars))
	w.status.Facts = facts
	w.status.Groups = []kvitui.StatusGroup{{Facts: w.countFacts(stats)}}
	w.toolbar.Update()
	w.status.MarkForLayoutAndRedraw()
}
