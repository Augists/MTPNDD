package mtpndd

import (
	"sync"
	"sync/atomic"
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

var nextNDDNodeID atomic.Uint64

func init() { nextNDDNodeID.Store(100) } // leave room for terminals

// ---------------------------------------------------------------------------
// Node slab allocator (lock-free fast path).
// ---------------------------------------------------------------------------

// nddMaxChunks caps the slab chunk directory. With the default chunk
// size of 2^18 nodes this gives a 2^33 ≈ 8 G-node hard ceiling; the
// directory itself is nddMaxChunks * 8 bytes = 256 KB regardless of
// live chunk count. Raised from 2^12 after sre-ndd bgp_fattree12
// overflowed the earlier ~1 G cap during a single session.
const nddMaxChunks = 1 << 15

var (
	nddSlabChunk int
	nddSlabShift uint
	nddSlabMask  uint64
)

type nddSlabState struct {
	chunks [nddMaxChunks]atomic.Pointer[[]Node]
	next   atomic.Int64
	growMu sync.Mutex
}

var nddSlab nddSlabState

func allocNDDNode() *Node {
	gi := nddSlab.next.Add(1) - 1
	chunkIdx := int(uint64(gi) >> nddSlabShift)
	slotIdx := int(uint64(gi) & nddSlabMask)
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

const shardResizeLoad = 5 // resize at 50 % load; see internal/bdd/table.go for rationale

var (
	nddShardCount       int
	nddShardMask        uint64
	nddInitialShardCap  int
	nddInitialShardMask uint64
)

// nddSlot stores the node as uintptr; see internal/bdd/table.go for
// rationale (nodes rooted by slab, slot pointer was redundant for GC).
type nddSlot struct {
	hash uint64
	node uintptr
}

type nddShard struct {
	mu    sync.Mutex
	slots []nddSlot
	mask  uint64
	count int

	// Edge arena — bump-allocates edge slices for newly-interned nodes
	// under the shard lock. Avoids one runtime.mallocgc per mk-miss
	// (130 ms cum / ~8 % GC time on sre-ndd fattree08 MF=3).
	edgeChunk []edge
	edgeOff   int
}

const shardEdgeChunkSize = 4096

// allocEdgesLocked must be called with s.mu held. Returns a slice of length n
// backed by the shard's edge arena. Slices returned here are never freed
// until mtpndd.Reset() drops the chunks.
func (s *nddShard) allocEdgesLocked(n int) []edge {
	if n == 0 {
		return nil
	}
	if n > shardEdgeChunkSize {
		return make([]edge, n)
	}
	if s.edgeOff+n > len(s.edgeChunk) {
		s.edgeChunk = make([]edge, shardEdgeChunkSize)
		s.edgeOff = 0
	}
	out := s.edgeChunk[s.edgeOff : s.edgeOff+n : s.edgeOff+n]
	s.edgeOff += n
	return out
}

var ndUnique []nddShard

// allocateNDD sizes all module-level structures. Called exactly once from
// the runtime init path (engine.go's Init).
func allocateNDD(cfg SubConfig) {
	nddOpCache = make([]nddOpSlot, cfg.OpCacheSize)
	nddOpCacheMask = uint64(cfg.OpCacheSize - 1)
	nddCacheClearInterval = cfg.CacheClearInterval

	nddSlabChunk = cfg.SlabChunkSize
	nddSlabShift = uint(log2Pow2(cfg.SlabChunkSize))
	nddSlabMask = uint64(cfg.SlabChunkSize - 1)

	nddShardCount = cfg.ShardCount
	nddShardMask = uint64(cfg.ShardCount - 1)
	nddInitialShardCap = cfg.InitialShardCap
	nddInitialShardMask = uint64(cfg.InitialShardCap - 1)
	ndUnique = make([]nddShard, cfg.ShardCount)
	for i := range ndUnique {
		ndUnique[i].slots = make([]nddSlot, cfg.InitialShardCap)
		ndUnique[i].mask = nddInitialShardMask
	}
}

func log2Pow2(v int) int {
	n := 0
	for v > 1 {
		v >>= 1
		n++
	}
	return n
}

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
	idx := h & s.mask
	for {
		slot := &s.slots[idx]
		if slot.node == 0 {
			owned := s.allocEdgesLocked(len(edges))
			copy(owned, edges)
			n := allocNDDNode()
			n.fieldID = fieldID
			n.id = nextNDDNodeID.Add(1)
			n.edges = owned
			n.hash = h
			slot.hash = h
			slot.node = uintptr(unsafe.Pointer(n))
			s.count++
			if s.count*10 > len(s.slots)*shardResizeLoad {
				s.resizeLocked()
			}
			s.mu.Unlock()
			return n
		}
		if slot.hash == h {
			n := (*Node)(unsafe.Pointer(slot.node))
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
		if slot.node == 0 {
			continue
		}
		idx := slot.hash & s.mask
		for s.slots[idx].node != 0 {
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
	resetSatCountCache()
	for i := range ndUnique {
		s := &ndUnique[i]
		s.mu.Lock()
		s.slots = make([]nddSlot, nddInitialShardCap)
		s.mask = nddInitialShardMask
		s.count = 0
		s.edgeChunk = nil
		s.edgeOff = 0
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
