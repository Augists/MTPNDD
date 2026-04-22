package mtpndd

import (
	"sync"
	"weak"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

const (
	nddShardCount = 64
	nddShardMask  = nddShardCount - 1
	nddSweepEvery = 4096
)

type nddShard struct {
	mu       sync.Mutex
	table    map[uint64][]weak.Pointer[Node] // cumulative hash -> candidates
	opsSince uint32
}

var ndUnique = func() *[nddShardCount]nddShard {
	var t [nddShardCount]nddShard
	for i := range t {
		t[i].table = make(map[uint64][]weak.Pointer[Node], 64)
	}
	return &t
}()

// mk returns the canonical NDD node for (fieldID, edges). The edges slice
// must be a fresh buffer: on a hit it is discarded; on a miss it is retained
// inside the new node after being sorted in place.
func mk(fieldID uint32, edges []edge) *Node {
	// Drop dead edges (label=False or child=False) in place.
	w := 0
	for _, e := range edges {
		if e.label == bdd.False || e.child == False {
			continue
		}
		edges[w] = e
		w++
	}
	edges = edges[:w]
	if len(edges) == 0 {
		return False
	}
	if len(edges) > 1 {
		edges = mergeDuplicateChildren(edges)
	}
	sortEdges(edges)

	var edgeXor uint64
	for _, e := range edges {
		edgeXor ^= edgeHash(e.child, e.label)
	}
	h := mixFieldHash(fieldID, edgeXor)

	s := &ndUnique[h&nddShardMask]
	s.mu.Lock()
	for _, wp := range s.table[h] {
		if n := wp.Value(); n != nil {
			if n.fieldID == fieldID && edgesEqual(n.edges, edges) {
				s.mu.Unlock()
				return n
			}
		}
	}
	n := &Node{
		fieldID: fieldID,
		edges:   edges,
		hash:    h,
	}
	s.table[h] = append(s.table[h], weak.Make(n))
	s.opsSince++
	if s.opsSince >= nddSweepEvery {
		s.opsSince = 0
		s.sweepLocked()
	}
	s.mu.Unlock()
	return n
}

func (s *nddShard) sweepLocked() {
	for h, bucket := range s.table {
		w := 0
		for _, wp := range bucket {
			if wp.Value() != nil {
				bucket[w] = wp
				w++
			}
		}
		if w == 0 {
			delete(s.table, h)
		} else {
			s.table[h] = bucket[:w]
		}
	}
}

// mergeDuplicateChildren combines entries sharing the same child by OR-ing
// their BDD labels.
func mergeDuplicateChildren(edges []edge) []edge {
	out := edges[:0]
outer:
	for _, e := range edges {
		for i := range out {
			if out[i].child == e.child {
				out[i].label = bdd.Or(out[i].label, e.label)
				continue outer
			}
		}
		out = append(out, e)
	}
	return out
}

// TableSize returns the approximate number of live interior NDD nodes.
func TableSize() int {
	n := 0
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		for _, bucket := range s.table {
			for _, wp := range bucket {
				if wp.Value() != nil {
					n++
				}
			}
		}
		s.mu.Unlock()
	}
	return n
}
