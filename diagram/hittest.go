package diagram

// Finding what is under a point and what a source position belongs to, the
// scene-only part of the Qt app's src/content/diagrams/diagramcanvas.cpp
// (nodeAt, edgeAt, sourceOffsetAt, highlightSourceOffset and the selection
// helpers). Points are in the scene's coordinates; the editor divides out
// its zoom first.

// edgeHitWidth is how wide a path is for the pointer: 9 pixels, 4.5 either
// side of the line. The Qt app strokes the path at this width with square
// ends; this measures the distance to the line, which rounds the ends.
const edgeHitWidth = 9.0

// NodeAt is the id of the topmost node whose rectangle holds p, or "".
func (s *Scene) NodeAt(p Point) string {
	for i := len(s.Shapes) - 1; i >= 0; i-- {
		if sh := &s.Shapes[i]; sh.NodeID != "" && sh.Rect.Contains(p) {
			return sh.NodeID
		}
	}
	return ""
}

// EdgeAt is the EdgeIndex of the topmost selectable path within half of
// edgeHitWidth of p, or -1.
func (s *Scene) EdgeAt(p Point) int {
	for i := len(s.Paths) - 1; i >= 0; i-- {
		path := &s.Paths[i]
		if path.EdgeIndex < 0 {
			continue
		}
		if !path.Outline.Bounds().Adjusted(-edgeHitWidth, -edgeHitWidth, edgeHitWidth, edgeHitWidth).Contains(p) {
			continue
		}
		if path.Outline.DistanceTo(p) <= edgeHitWidth/2 {
			return path.EdgeIndex
		}
	}
	return -1
}

// NodeRect is the rectangle of the first shape drawing node id, and false
// when there is none.
func (s *Scene) NodeRect(id string) (Rect, bool) {
	for _, sh := range s.Shapes {
		if id != "" && sh.NodeID == id {
			return sh.Rect, true
		}
	}
	return Rect{}, false
}

// NodeIDs is the id of every node in the order the shapes are drawn, each
// once; moving the selection with the keyboard walks it.
func (s *Scene) NodeIDs() []string {
	var ids []string
	seen := map[string]bool{}
	for _, sh := range s.Shapes {
		if sh.NodeID != "" && !seen[sh.NodeID] {
			seen[sh.NodeID] = true
			ids = append(ids, sh.NodeID)
		}
	}
	return ids
}

// HasEdge reports whether a path draws the edge with this index.
func (s *Scene) HasEdge(index int) bool {
	for _, p := range s.Paths {
		if index >= 0 && p.EdgeIndex == index {
			return true
		}
	}
	return false
}

// NodeSource is the source offset a node's first shape starts at, or -1.
func (s *Scene) NodeSource(id string) int {
	for _, sh := range s.Shapes {
		if id != "" && sh.NodeID == id && sh.Src.Valid() {
			return sh.Src.Start
		}
	}
	return -1
}

// EdgeSource is the source offset of the edge's first path, or -1.
func (s *Scene) EdgeSource(index int) int {
	for _, p := range s.Paths {
		if index >= 0 && p.EdgeIndex == index && p.Src.Valid() {
			return p.Src.Start
		}
	}
	return -1
}

// SourceOffsetAt is where in the source the node or edge under p starts, a
// node winning over an edge, or -1 for empty space.
func (s *Scene) SourceOffsetAt(p Point) int {
	if node := s.NodeAt(p); node != "" {
		if off := s.NodeSource(node); off >= 0 {
			return off
		}
	}
	if edge := s.EdgeAt(p); edge >= 0 {
		return s.EdgeSource(edge)
	}
	return -1
}

// ElementAtOffset is the node or edge whose source span holds offset, the
// shortest span winning, so a caret in the source can light up what it is
// in. It gives the node's id and -1, an edge's index and "", or "" and -1.
func (s *Scene) ElementAtOffset(offset int) (nodeID string, edge int) {
	edge = -1
	bestLen := -1
	if offset < 0 {
		return "", -1
	}
	for _, sh := range s.Shapes {
		if sh.NodeID == "" || !sh.Src.Contains(offset) {
			continue
		}
		if bestLen < 0 || sh.Src.Length < bestLen {
			nodeID, edge, bestLen = sh.NodeID, -1, sh.Src.Length
		}
	}
	for _, p := range s.Paths {
		if p.EdgeIndex < 0 || !p.Src.Contains(offset) {
			continue
		}
		if bestLen < 0 || p.Src.Length < bestLen {
			nodeID, edge, bestLen = "", p.EdgeIndex, p.Src.Length
		}
	}
	return nodeID, edge
}
