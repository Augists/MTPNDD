package mtpndd

import (
	"sync"
	"sync/atomic"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

var nextNDDNodeID atomic.Uint64

func init() { nextNDDNodeID.Store(100) } // leave room for terminals

// ---------------------------------------------------------------------------
// Node slab allocator (mutex-guarded bump allocator).
// ---------------------------------------------------------------------------

const nddSlabChunk = 1 << 18 // 256K nodes per chunk

type nddSlabState struct {
	mu     sync.Mutex
	chunk  []Node
	nextIx int
}

var nddSlab nddSlabState

func allocNDDNode() *Node {
	nddSlab.mu.Lock()
	if nddSlab.nextIx == len(nddSlab.chunk) {
		nddSlab.chunk = make([]Node, nddSlabChunk)
		nddSlab.nextIx = 0
	}
	n := &nddSlab.chunk[nddSlab.nextIx]
	nddSlab.nextIx++
	nddSlab.mu.Unlock()
	return n
}

// ---------------------------------------------------------------------------
// Unique table: sharded strong-pointer Go map with hash-collision chain.
// ---------------------------------------------------------------------------

const (
	nddShardCount = 64
	nddShardMask  = nddShardCount - 1
)

type nddShard struct {
	mu    sync.Mutex
	table map[uint64][]*Node // cumulative hash -> candidates
}

var ndUnique = func() *[nddShardCount]nddShard {
	var t [nddShardCount]nddShard
	for i := range t {
		t[i].table = make(map[uint64][]*Node, 64)
	}
	return &t
}()

func mk(fieldID uint32, edges []edge) *Node {
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
	for _, n := range s.table[h] {
		if n.fieldID == fieldID && edgesEqual(n.edges, edges) {
			s.mu.Unlock()
			return n
		}
	}
	n := allocNDDNode()
	n.fieldID = fieldID
	n.id = nextNDDNodeID.Add(1)
	n.edges = edges
	n.hash = h
	s.table[h] = append(s.table[h], n)
	s.mu.Unlock()
	return n
}

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

// Reset clears the NDD and BDD unique tables plus the op caches.
// Nodes are session-scoped now that the tables hold strong references;
// call Reset between independent computations to release memory.
func Reset() {
	clearNDDCache()
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		for h := range s.table {
			delete(s.table, h)
		}
		s.mu.Unlock()
	}
	bdd.Reset()
}

// TableSize returns the approximate number of live interior NDD nodes.
func TableSize() int {
	n := 0
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		for _, bucket := range s.table {
			n += len(bucket)
		}
		s.mu.Unlock()
	}
	return n
}
