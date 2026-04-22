package mtpndd

import (
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
	"github.com/Augists/mtpndd-go/internal/work"
)

// spawnPairThreshold is the cartesian-product size at which And/Or begin to
// spawn per-pair sub-tasks. Below this threshold every pair is computed
// inline. Goroutine spawn costs ~1-5µs so this needs to stay conservative.
const spawnPairThreshold = 4

// And returns a AND b (boolean NDD conjunction).
func And(a, b *Node) *Node {
	if a == False || b == False {
		return False
	}
	if a == True {
		return b
	}
	if b == True {
		return a
	}
	if a == b {
		return a
	}

	ca, cb := a, b
	if uintptr(unsafe.Pointer(ca)) > uintptr(unsafe.Pointer(cb)) {
		ca, cb = cb, ca
	}
	if r, ok := nddCacheGet(opAnd, ca, cb, 0); ok {
		return r
	}

	var res *Node
	if a.fieldID == b.fieldID {
		res = andSameField(a, b)
	} else {
		if a.fieldID > b.fieldID {
			a, b = b, a
		}
		res = andDiffField(a, b)
	}
	nddCachePut(opAnd, ca, cb, res, 0)
	return res
}

func andSameField(a, b *Node) *Node {
	total := len(a.edges) * len(b.edges)
	edges := make([]edge, 0, total)
	parallel := total >= spawnPairThreshold

	if parallel {
		type item struct {
			child *Node
			label *bdd.Node
		}
		items := make([]item, total)
		futs := make([]*work.Future, 0, total)
		idx := 0
		for i := range a.edges {
			for j := range b.edges {
				ai, bj := a.edges[i], b.edges[j]
				label := bdd.And(ai.label, bj.label)
				if label == bdd.False {
					items[idx].label = nil
					idx++
					continue
				}
				slot := idx
				items[slot].label = label
				idx++
				futs = append(futs, work.Go(func() {
					items[slot].child = And(ai.child, bj.child)
				}))
			}
		}
		for _, f := range futs {
			f.Wait()
		}
		for _, it := range items {
			if it.label == nil || it.child == False {
				continue
			}
			edges = append(edges, edge{child: it.child, label: it.label})
		}
	} else {
		for i := range a.edges {
			for j := range b.edges {
				ai, bj := a.edges[i], b.edges[j]
				label := bdd.And(ai.label, bj.label)
				if label == bdd.False {
					continue
				}
				child := And(ai.child, bj.child)
				if child == False {
					continue
				}
				edges = append(edges, edge{child: child, label: label})
			}
		}
	}
	return mk(a.fieldID, edges)
}

func andDiffField(a, b *Node) *Node {
	parallel := len(a.edges) >= spawnPairThreshold
	edges := make([]edge, 0, len(a.edges))
	if parallel {
		children := make([]*Node, len(a.edges))
		futs := make([]*work.Future, len(a.edges))
		for i := range a.edges {
			futs[i] = work.Go(func() {
				children[i] = And(a.edges[i].child, b)
			})
		}
		for _, f := range futs {
			f.Wait()
		}
		for i, e := range a.edges {
			if children[i] == False {
				continue
			}
			edges = append(edges, edge{child: children[i], label: e.label})
		}
	} else {
		for _, e := range a.edges {
			child := And(e.child, b)
			if child == False {
				continue
			}
			edges = append(edges, edge{child: child, label: e.label})
		}
	}
	return mk(a.fieldID, edges)
}

// Or returns a OR b (boolean NDD disjunction).
func Or(a, b *Node) *Node {
	if a == True || b == True {
		return True
	}
	if a == False {
		return b
	}
	if b == False {
		return a
	}
	if a == b {
		return a
	}

	ca, cb := a, b
	if uintptr(unsafe.Pointer(ca)) > uintptr(unsafe.Pointer(cb)) {
		ca, cb = cb, ca
	}
	if r, ok := nddCacheGet(opOr, ca, cb, 0); ok {
		return r
	}

	var res *Node
	if a.fieldID == b.fieldID {
		res = orSameField(a, b)
	} else {
		if a.fieldID > b.fieldID {
			a, b = b, a
		}
		res = orDiffField(a, b)
	}
	nddCachePut(opOr, ca, cb, res, 0)
	return res
}

func orSameField(a, b *Node) *Node {
	residualA := make([]*bdd.Node, len(a.edges))
	residualB := make([]*bdd.Node, len(b.edges))
	for i, e := range a.edges {
		residualA[i] = e.label
	}
	for j, e := range b.edges {
		residualB[j] = e.label
	}

	edges := make([]edge, 0, len(a.edges)+len(b.edges))
	for i := range a.edges {
		for j := range b.edges {
			intersect := bdd.And(a.edges[i].label, b.edges[j].label)
			if intersect == bdd.False {
				continue
			}
			notIntersect := bdd.Not(intersect)
			residualA[i] = bdd.And(residualA[i], notIntersect)
			residualB[j] = bdd.And(residualB[j], notIntersect)
			child := Or(a.edges[i].child, b.edges[j].child)
			edges = append(edges, edge{child: child, label: intersect})
		}
	}
	for i, e := range a.edges {
		if residualA[i] != bdd.False {
			edges = append(edges, edge{child: e.child, label: residualA[i]})
		}
	}
	for j, e := range b.edges {
		if residualB[j] != bdd.False {
			edges = append(edges, edge{child: e.child, label: residualB[j]})
		}
	}
	return mk(a.fieldID, edges)
}

func orDiffField(a, b *Node) *Node {
	residualB := bdd.True
	edges := make([]edge, 0, len(a.edges)+1)
	for _, e := range a.edges {
		notLabel := bdd.Not(e.label)
		residualB = bdd.And(residualB, notLabel)
		child := Or(e.child, b)
		edges = append(edges, edge{child: child, label: e.label})
	}
	if residualB != bdd.False {
		edges = append(edges, edge{child: b, label: residualB})
	}
	return mk(a.fieldID, edges)
}

// Not returns NOT a.
func Not(a *Node) *Node {
	if a == True {
		return False
	}
	if a == False {
		return True
	}
	if r, ok := nddCacheGet(opNot, a, nil, 0); ok {
		return r
	}
	residual := bdd.True
	edges := make([]edge, 0, len(a.edges)+1)
	for _, e := range a.edges {
		notLabel := bdd.Not(e.label)
		residual = bdd.And(residual, notLabel)
		child := Not(e.child)
		if child == False {
			continue
		}
		edges = append(edges, edge{child: child, label: e.label})
	}
	if residual != bdd.False {
		edges = append(edges, edge{child: True, label: residual})
	}
	res := mk(a.fieldID, edges)
	nddCachePut(opNot, a, nil, res, 0)
	return res
}

// Diff returns a AND NOT b.
func Diff(a, b *Node) *Node {
	return And(a, Not(b))
}

// Exist returns the NDD with the given field existentially quantified out.
func Exist(a *Node, fieldID uint32) *Node {
	if IsTerminal(a) {
		return a
	}
	if r, ok := nddCacheGet(opExist, a, nil, fieldID); ok {
		return r
	}
	var res *Node
	if a.fieldID == fieldID {
		res = False
		for _, e := range a.edges {
			res = Or(res, e.child)
		}
	} else {
		edges := make([]edge, 0, len(a.edges))
		for _, e := range a.edges {
			child := Exist(e.child, fieldID)
			if child == False {
				continue
			}
			edges = append(edges, edge{child: child, label: e.label})
		}
		res = mk(a.fieldID, edges)
	}
	nddCachePut(opExist, a, nil, res, fieldID)
	return res
}
