package mtpndd

import (
	"sync"
	"sync/atomic"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

var nextNDDNodeID atomic.Uint64

func init() { nextNDDNodeID.Store(100) }

// ---------------------------------------------------------------------------
// Node slab allocator (lock-free fast path).
// ---------------------------------------------------------------------------

const (
	nddSlabChunk = 1 << 18
	nddSlabShift = 18
	nddSlabMask  = nddSlabChunk - 1
	nddMaxChunks = 1 << 12 // ~1 G node upper bound
)

type nddSlabState struct {
	chunks [nddMaxChunks]atomic.Pointer[[]Node]
	next   atomic.Int64
	growMu sync.Mutex // only held while publishing a new chunk
}

var nddSlab nddSlabState

func allocNDDNode() *Node {
	gi := nddSlab.next.Add(1) - 1
	chunkIdx := int(gi >> nddSlabShift)
	slotIdx := int(gi) & nddSlabMask
	if cp := nddSlab.chunks[chunkIdx].Load(); cp != nil {
		return &(*cp)[slotIdx]
	}
	nddSlab.growMu.Lock()
	if nddSlab.chunks[chunkIdx].Load() == nil {
		c := make([]Node, nddSlabChunk)
		nddSlab.chunks[chunkIdx].Store(&c)
	}
	nddSlab.growMu.Unlock()
	return &(*nddSlab.chunks[chunkIdx].Load())[slotIdx]
}

// ---------------------------------------------------------------------------
// Unique table: linear-probed open-addressing hash table, per-shard.
// ---------------------------------------------------------------------------

const (
	nddShardCount   = 64
	nddShardMask    = nddShardCount - 1
	initialShardCap = 1024
	shardResizeLoad = 7
)

type nddSlot struct {
	hash uint64
	node *Node // nil = empty
}

type nddShard struct {
	mu    sync.Mutex
	slots []nddSlot
	mask  uint64
	count int
}

var ndUnique = func() *[nddShardCount]nddShard {
	var t [nddShardCount]nddShard
	for i := range t {
		t[i].slots = make([]nddSlot, initialShardCap)
		t[i].mask = uint64(initialShardCap - 1)
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
	// On a hash match we return the candidate without doing a secondary
	// edgesEqual compare. The 64-bit fingerprint is a safe full key at
	// the collision rate we care about (~2⁻⁶⁴).
	idx := h & s.mask
	for {
		slot := &s.slots[idx]
		if slot.node == nil {
			// Miss: copy caller's edges into a tight node-owned
			// slice. Lets callers pass transient buffers (e.g.
			// stack-allocated) without worrying about ownership
			// transfer.
			owned := make([]edge, len(edges))
			copy(owned, edges)
			n := allocNDDNode()
			n.fieldID = fieldID
			n.id = nextNDDNodeID.Add(1)
			n.edges = owned
			n.hash = h
			slot.hash = h
			slot.node = n
			s.count++
			if s.count*10 > len(s.slots)*shardResizeLoad {
				s.resizeLocked()
			}
			s.mu.Unlock()
			return n
		}
		if slot.hash == h {
			n := slot.node
			s.mu.Unlock()
			return n
		}
		idx = (idx + 1) & s.mask
	}
}

func (s *nddShard) resizeLocked() {
	oldSlots := s.slots
	newSize := len(oldSlots) * 2
	s.slots = make([]nddSlot, newSize)
	s.mask = uint64(newSize - 1)
	for _, slot := range oldSlots {
		if slot.node == nil {
			continue
		}
		idx := slot.hash & s.mask
		for s.slots[idx].node != nil {
			idx = (idx + 1) & s.mask
		}
		s.slots[idx] = slot
	}
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
func Reset() {
	clearNDDCache()
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		s.slots = make([]nddSlot, initialShardCap)
		s.mask = uint64(initialShardCap - 1)
		s.count = 0
		s.mu.Unlock()
	}
	for i := range nddSlab.chunks {
		nddSlab.chunks[i].Store(nil)
	}
	nddSlab.next.Store(0)
	bdd.Reset()
}

// TableSize returns the approximate number of live interior NDD nodes.
func TableSize() int {
	n := 0
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		n += s.count
		s.mu.Unlock()
	}
	return n
}
