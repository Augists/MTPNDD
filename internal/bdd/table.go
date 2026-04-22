package bdd

import (
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

var nextBDDNodeID atomic.Uint64

func init() { nextBDDNodeID.Store(100) } // leave room for terminals

// ---------------------------------------------------------------------------
// BDD node slab allocator. Mutex-guarded bump allocator.
// ---------------------------------------------------------------------------

const bddSlabChunk = 1 << 18

type bddSlabState struct {
	mu     sync.Mutex
	chunk  []Node
	nextIx int
}

var bddSlab bddSlabState

func allocBDDNode() *Node {
	bddSlab.mu.Lock()
	if bddSlab.nextIx == len(bddSlab.chunk) {
		bddSlab.chunk = make([]Node, bddSlabChunk)
		bddSlab.nextIx = 0
	}
	n := &bddSlab.chunk[bddSlab.nextIx]
	bddSlab.nextIx++
	bddSlab.mu.Unlock()
	return n
}

// ---------------------------------------------------------------------------
// Unique table: sharded strong-pointer Go map (one *Node per key).
// ---------------------------------------------------------------------------

const (
	shardCount      = 64
	shardMask       = shardCount - 1
	initialShardCap = 256
)

type nodeKey struct {
	v  uint32
	lo uintptr
	hi uintptr
}

type uniqueShard struct {
	mu    sync.Mutex
	table map[nodeKey]*Node
}

type uniqueTable struct {
	shards [shardCount]uniqueShard
}

var unique = func() *uniqueTable {
	t := &uniqueTable{}
	for i := range t.shards {
		t.shards[i].table = make(map[nodeKey]*Node, initialShardCap)
	}
	return t
}()

func shardIdx(k nodeKey) uint32 {
	h := uint64(k.v) * 0x9E3779B97F4A7C15
	h ^= uint64(k.lo) * 0xBF58476D1CE4E5B9
	h ^= uint64(k.hi) * 0x94D049BB133111EB
	h ^= h >> 32
	return uint32(h) & shardMask
}

func (t *uniqueTable) intern(v uint32, lo, hi *Node) *Node {
	k := nodeKey{
		v:  v,
		lo: uintptr(unsafe.Pointer(lo)),
		hi: uintptr(unsafe.Pointer(hi)),
	}
	s := &t.shards[shardIdx(k)]
	s.mu.Lock()
	if n, ok := s.table[k]; ok {
		s.mu.Unlock()
		runtime.KeepAlive(lo)
		runtime.KeepAlive(hi)
		return n
	}
	n := allocBDDNode()
	n.Var = v
	n.id = nextBDDNodeID.Add(1)
	n.Low = lo
	n.High = hi
	s.table[k] = n
	s.mu.Unlock()
	runtime.KeepAlive(lo)
	runtime.KeepAlive(hi)
	return n
}

// Reset clears all interned BDD nodes and the op cache.
func Reset() {
	clearBDDCache()
	for i := range unique.shards {
		s := &unique.shards[i]
		s.mu.Lock()
		for k := range s.table {
			delete(s.table, k)
		}
		s.mu.Unlock()
	}
}

// TableSize returns the total number of live interned BDD nodes.
func TableSize() int {
	n := 0
	for i := range unique.shards {
		s := &unique.shards[i]
		s.mu.Lock()
		n += len(s.table)
		s.mu.Unlock()
	}
	return n
}
