package bdd

import (
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

var nextBDDNodeID atomic.Uint64

func init() { nextBDDNodeID.Store(100) }

// ---------------------------------------------------------------------------
// BDD node slab (lock-free fast path).
// ---------------------------------------------------------------------------

const (
	bddSlabChunk = 1 << 18
	bddSlabShift = 18
	bddSlabMask  = bddSlabChunk - 1
	bddMaxChunks = 1 << 12
)

type bddSlabState struct {
	chunks [bddMaxChunks]atomic.Pointer[[]Node]
	next   atomic.Int64
	growMu sync.Mutex
}

var bddSlab bddSlabState

func allocBDDNode() *Node {
	gi := bddSlab.next.Add(1) - 1
	chunkIdx := int(gi >> bddSlabShift)
	slotIdx := int(gi) & bddSlabMask
	if cp := bddSlab.chunks[chunkIdx].Load(); cp != nil {
		return &(*cp)[slotIdx]
	}
	bddSlab.growMu.Lock()
	if bddSlab.chunks[chunkIdx].Load() == nil {
		c := make([]Node, bddSlabChunk)
		bddSlab.chunks[chunkIdx].Store(&c)
	}
	bddSlab.growMu.Unlock()
	return &(*bddSlab.chunks[chunkIdx].Load())[slotIdx]
}

// ---------------------------------------------------------------------------
// Unique table: linear-probed open-addressing hash table, per-shard.
// ---------------------------------------------------------------------------

const (
	shardCount      = 64
	shardMask       = shardCount - 1
	initialShardCap = 512
	shardResizeLoad = 7
)

type shardSlot struct {
	hash uint64
	node *Node
}

type nodeKey struct {
	v  uint32
	lo uintptr
	hi uintptr
}

type uniqueShard struct {
	mu    sync.Mutex
	slots []shardSlot
	mask  uint64
	count int
}

type uniqueTable struct {
	shards [shardCount]uniqueShard
}

var unique = func() *uniqueTable {
	t := &uniqueTable{}
	for i := range t.shards {
		t.shards[i].slots = make([]shardSlot, initialShardCap)
		t.shards[i].mask = uint64(initialShardCap - 1)
	}
	return t
}()

func keyHash(k nodeKey) uint64 {
	h := uint64(k.v) * 0x9E3779B97F4A7C15
	h ^= uint64(k.lo) * 0xBF58476D1CE4E5B9
	h ^= uint64(k.hi) * 0x94D049BB133111EB
	h ^= h >> 32
	return h
}

func (t *uniqueTable) intern(v uint32, lo, hi *Node) *Node {
	k := nodeKey{
		v:  v,
		lo: uintptr(unsafe.Pointer(lo)),
		hi: uintptr(unsafe.Pointer(hi)),
	}
	h := keyHash(k)
	s := &t.shards[h&shardMask]
	s.mu.Lock()
	idx := h & s.mask
	for {
		slot := &s.slots[idx]
		if slot.node == nil {
			n := allocBDDNode()
			n.Var = v
			n.id = nextBDDNodeID.Add(1)
			n.Low = lo
			n.High = hi
			slot.hash = h
			slot.node = n
			s.count++
			if s.count*10 > len(s.slots)*shardResizeLoad {
				s.resizeLocked()
			}
			s.mu.Unlock()
			runtime.KeepAlive(lo)
			runtime.KeepAlive(hi)
			return n
		}
		if slot.hash == h {
			n := slot.node
			s.mu.Unlock()
			runtime.KeepAlive(lo)
			runtime.KeepAlive(hi)
			return n
		}
		idx = (idx + 1) & s.mask
	}
}

func (s *uniqueShard) resizeLocked() {
	oldSlots := s.slots
	newSize := len(oldSlots) * 2
	s.slots = make([]shardSlot, newSize)
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

// Reset clears all interned BDD nodes and the op cache.
func Reset() {
	clearBDDCache()
	for i := range unique.shards {
		s := &unique.shards[i]
		s.mu.Lock()
		s.slots = make([]shardSlot, initialShardCap)
		s.mask = uint64(initialShardCap - 1)
		s.count = 0
		s.mu.Unlock()
	}
	for i := range bddSlab.chunks {
		bddSlab.chunks[i].Store(nil)
	}
	bddSlab.next.Store(0)
}

// TableSize returns the total number of live interned BDD nodes.
func TableSize() int {
	n := 0
	for i := range unique.shards {
		s := &unique.shards[i]
		s.mu.Lock()
		n += s.count
		s.mu.Unlock()
	}
	return n
}
