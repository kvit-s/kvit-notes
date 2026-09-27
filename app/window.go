package app

// The Kvit Notes window over one vault: the sidebar (search field and
// scopes), the note list, and the editor pane with its tag strip, side by
// side and resizable, with the status line under them.

import (
	"errors"
	"fmt"
	"path"
	"path/filepath"
	"slices"
	"strings"
	"time"

	"github.com/kvit-s/kvit-notes/editor"
	"github.com/kvit-s/kvit-notes/vault"
	kvitui "github.com/kvit-s/kvit-ui"
	"github.com/kvit-s/kvit-ui/palette"
	"github.com/richardwilkes/toolbox/v2/geom"
	"github.com/richardwilkes/unison"
	"github.com/richardwilkes/unison/enums/align"
	"github.com/richardwilkes/unison/enums/mod"
	"github.com/richardwilkes/unison/enums/paintstyle"
)

// The panes' widths in design pixels (main.qml).
const (
	sidebarWidth  = 200
	noteListWidth = 260
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

	toolbar *Toolbar
	split   *kvitui.SplitView
	main    *unison.Panel // holds the split, or the editor pane alone
	slot    *unison.Panel // the split's third pane, which holds the editor pane
	pane    *unison.Panel // the editor pane: tag strip and editor
	hidden  bool          // the side panes are hidden (Ctrl+\)
	scope   Scope
	sortBy  string // "modified", "created" or "title"
	ascend  bool
	heading *kvitui.Label // the note list's title: the scope it shows
	sortDir *kvitui.IconButton
	shown   []*vault.Entry // the notes the list shows, in order
	open    *vault.Entry   // the note in the editor, or nil
	page    *vault.Page    // its front matter and body as loaded
	saveAt  time.Time      // when the pending save is due; zero for none
	message string         // what the status line says about saving
}

// Open opens a window over a vault.
func Open(ui *kvitui.UI, v *vault.Vault) (*Window, error) {
	win, err := kvitui.NewWindow(ui, filepath.Base(v.Root)+" — Kvit Notes")
	if err != nil {
		return nil, err
	}
	w := &Window{ui: ui, Win: win, Vault: v}

	w.search = kvitui.NewSearchField(ui)
	w.search.Placeholder = "Search all notes"
	w.search.OnChange = func(string) { w.refreshList() }
	w.scopes = NewScopeList(ui)
	w.scopes.OnChoose = func(s Scope) { w.scope = s; w.refreshList() }
	w.scopes.OnToggle = func(folder string, expanded bool) {
		v.State.SetFolderExpanded(folder, expanded)
		w.refreshScopes()
	}
	notesLabel := kvitui.NewLabel(ui, "Notes")
	notesLabel.Role, notesLabel.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	newFolder := kvitui.NewIconButton(ui, "folder-plus", "New folder")
	newFolder.OnClick = w.newFolder
	sideHead := headerRow(ui, notesLabel, newFolder)
	side := kvitui.Column(ui, kvitui.SizeSpaceSnug, sideHead, w.search, w.scopes)
	sideHead.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	side.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	w.scopes.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.search.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	side.DrawCallback = ground(ui, side, func() kvitui.Ink { return kvitui.InkPanelBackground })

	w.list = NewNoteList(ui)
	w.list.OnOpen = func(i int) { w.openNote(w.shown[i]) }
	listRegion := kvitui.NewRegion(ui, w.list)
	listRegion.Padding = kvitui.Px(0)
	w.sortBy = "modified"
	w.heading = kvitui.NewLabel(ui, "All Notes")
	w.heading.Role, w.heading.Ink = kvitui.RoleStrong, kvitui.InkTextSecondary
	newNote := kvitui.NewIconButton(ui, "plus", "New note (Ctrl+N)")
	newNote.OnClick = w.newNote
	sortBy := kvitui.NewSelect(ui, "Sort notes by",
		kvitui.Option{Value: "modified", Label: "Modified"},
		kvitui.Option{Value: "created", Label: "Created"},
		kvitui.Option{Value: "title", Label: "Title"})
	sortBy.OnChoose = func(value string) { w.sortBy = value; w.refreshList() }
	w.sortDir = kvitui.NewIconButton(ui, "sort-descending", "Newest first")
	w.sortDir.OnClick = func() {
		w.ascend = !w.ascend
		w.sortDir.Symbol, w.sortDir.Label = "sort-descending", "Newest first"
		if w.ascend {
			w.sortDir.Symbol, w.sortDir.Label = "sort-ascending", "Oldest first"
		}
		w.sortDir.MarkForRedraw()
		w.refreshList()
	}
	sortRow := kvitui.Row(ui, kvitui.SizeSpaceSnug, sortBy, w.sortDir)
	sortBy.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true, VAlign: align.Middle})
	listHead := kvitui.Column(ui, kvitui.SizeSpaceSnug, headerRow(ui, w.heading, newNote), sortRow)
	listHead.SetBorder(kvitui.Padding(ui, kvitui.SizeSpace))
	listPane := kvitui.Column(ui, kvitui.Px(0), listHead, listRegion)
	for _, p := range []*unison.Panel{listHead, sortRow} {
		p.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	}
	listRegion.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	listPane.DrawCallback = ground(ui, listPane, func() kvitui.Ink { return kvitui.InkListBackground })

	w.Editor = editor.New(ui, editor.NewDoc(nil))
	w.Editor.OnChange = w.edited
	w.tags = NewTagStrip(ui)
	w.tags.OnAdd = func(tag string) { w.setTags(append(w.page.Tags(), tag)) }
	w.tags.OnRemove = func(tag string) {
		w.setTags(slices.DeleteFunc(w.page.Tags(), func(t string) bool { return t == tag }))
	}
	w.region = kvitui.NewRegion(ui, w.Editor)
	w.region.Padding = kvitui.Px(0)
	strip := kvitui.Row(ui, kvitui.SizeSpace, w.tags)
	strip.SetBorder(kvitui.Insets(ui, kvitui.Px(12), kvitui.Px(24), kvitui.Px(0), kvitui.Px(24)))
	pane := kvitui.Column(ui, kvitui.Px(0), strip, w.region)
	w.pane = pane
	w.region.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	strip.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})

	fill := func(p *unison.Panel) *unison.Panel {
		p.SetLayout(&unison.FlexLayout{Columns: 1, HAlign: align.Fill, VAlign: align.Fill})
		p.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
		return p
	}
	pane.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.slot = fill(unison.NewPanel())
	w.slot.AddChild(pane)
	w.split = kvitui.NewSplitView(ui, side, listPane, w.slot)
	w.split.SetSize(0, kvitui.Px(sidebarWidth))
	w.split.SetSize(1, kvitui.Px(noteListWidth))
	w.split.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, VAlign: align.Fill, HGrab: true, VGrab: true})
	w.main = fill(unison.NewPanel())
	w.main.AddChild(w.split)
	w.toolbar = NewToolbar(ui, w.Editor)
	w.toolbar.SetLayoutData(&unison.FlexLayoutData{HAlign: align.Fill, HGrab: true})
	body := fill(unison.NewPanel())
	body.AddChild(w.toolbar)
	body.AddChild(w.main)
	w.list.OnDrag = w.dragNote
	w.list.OnDrop = w.dropNote
	w.status = kvitui.NewStatusBar(ui)
	win.SetBody(body)
	win.SetStatusBar(w.status)
	win.SidebarVisible = false
	win.OnKeyDown = w.keyDown
	win.WillCloseCallback = chain(win.WillCloseCallback, func() {
		w.saveNow()
		_ = v.SaveState()
		v.Close()
	})

	w.installMenus()
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
	w.tick()
	return w, nil
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
	if !mods.OSMenuCommandDown() {
		return false
	}
	switch key {
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
	w.pane.RemoveFromParent()
	w.main.RemoveAllChildren()
	if w.hidden {
		w.main.AddChild(w.pane)
	} else {
		w.slot.AddChild(w.pane)
		w.main.AddChild(w.split)
	}
	w.main.MarkForLayoutAndRedraw()
	w.Editor.RequestFocus()
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
}

// dropNote moves a dragged note into the folder it was dropped on.
func (w *Window) dropNote(i int, where geom.Point) {
	w.scopes.Target = -1
	w.scopes.MarkForRedraw()
	_, folder, ok := w.dropTarget(where)
	if !ok || i < 0 || i >= len(w.shown) {
		return
	}
	e := w.shown[i]
	if e.Folder == folder {
		return
	}
	if e == w.open {
		w.saveNow()
	}
	if err := w.Vault.Move(e, folder); err != nil {
		w.fail("Could not move the note", err)
		return
	}
	w.refreshScopes()
	w.refreshList()
	w.update()
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
	if q := strings.ToLower(strings.TrimSpace(w.search.Text())); q != "" {
		return strings.Contains(strings.ToLower(e.Title), q) || strings.Contains(strings.ToLower(e.Text), q)
	}
	return true
}

// refreshList rebuilds the note list for the current scope, pinned notes
// first, the most recently changed next.
func (w *Window) refreshList() {
	w.shown = w.shown[:0]
	for _, e := range w.Vault.Entries {
		if w.inScope(e) {
			w.shown = append(w.shown, e)
		}
	}
	slices.SortStableFunc(w.shown, func(a, b *vault.Entry) int {
		if a.Pinned != b.Pinned {
			if a.Pinned {
				return -1
			}
			return 1
		}
		var c int
		switch w.sortBy {
		case "title":
			c = strings.Compare(strings.ToLower(a.Title), strings.ToLower(b.Title))
			if w.ascend {
				// Title order reads A to Z unless turned around.
				c = -c
			}
			return c
		case "created":
			c = createdOf(b).Compare(createdOf(a))
		default:
			c = b.Modified.Compare(a.Modified)
		}
		if w.ascend {
			c = -c
		}
		return c
	})
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
	page, err := w.Vault.Load(e.Path)
	if err != nil {
		w.message = "Could not open " + e.Title + ": " + err.Error()
		w.update()
		return
	}
	w.open, w.page = e, page
	w.Vault.State.LastOpenNote = e.Path
	doc := editor.NewDoc(editor.ParseMarkdown(page.Body))
	w.Editor.SetDoc(doc)
	w.tags.SetTags(page.Tags())
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
	if w.open != nil && w.Editor.Doc.Dirty && w.saveAt.IsZero() {
		w.saveAt = time.Now().Add(saveDelay)
	}
	w.autoTitle()
	w.update()
}

// tick saves a note whose save has come due, twice a second while the
// window is open.
func (w *Window) tick() {
	if w.Win.Window == nil || !w.Win.IsValid() {
		return
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
	if w.open != nil {
		facts = append(facts, w.open.Path)
	}
	words, chars := d.Words()
	facts = append(facts, fmt.Sprintf("%d blocks", len(d.Blocks)), fmt.Sprintf("%d words", words), fmt.Sprintf("%d chars", chars))
	w.status.Facts = facts
	w.toolbar.Update()
	w.status.MarkForLayoutAndRedraw()
}
