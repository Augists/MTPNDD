package bdd

import (
	"fmt"
	"runtime"
	"sync"
	"sync/atomic"
	"unsafe"
)

var nextBDDNodeID atomic.Uint64

func init() { nextBDDNodeID.Store(100) }

// Config is the BDD-layer subset of mtpndd.Config. Exposed for mtpndd's
// Init to propagate; direct callers should use mtpndd.Init(cfg).
type Config struct {
	OpCacheSize        int
	CacheClearInterval uint64
	ShardCount         int
	InitialShardCap    int
	SlabChunkSize      int
}

// DefaultConfig returns the BDD-layer defaults used when no explicit
// configuration has been installed.
func DefaultConfig() Config {
	return Config{
		OpCacheSize:        1 << 19,
		CacheClearInterval: 1 << 21,
		ShardCount:         64,
		InitialShardCap:    512,
		SlabChunkSize:      1 << 18,
	}
}

var initOnce sync.Once

// ensureInit allocates the BDD runtime with default configuration if the
// caller (or mtpndd.Init) hasn't done so already.
func ensureInit() {
	initOnce.Do(func() { applyConfig(DefaultConfig()) })
}

// Init installs cfg as the BDD-layer configuration and allocates the op
// cache, unique table, and slab state. Called from mtpndd.Init; first call
// wins.
func Init(cfg Config) {
	initOnce.Do(func() { applyConfig(cfg) })
}

func applyConfig(cfg Config) {
	checkPow2("bdd.OpCacheSize", cfg.OpCacheSize)
	checkPow2("bdd.ShardCount", cfg.ShardCount)
	checkPow2("bdd.InitialShardCap", cfg.InitialShardCap)
	checkPow2("bdd.SlabChunkSize", cfg.SlabChunkSize)

	opCache = make([]opSlot, cfg.OpCacheSize)
	opCacheMask = uint64(cfg.OpCacheSize - 1)
	bddCacheClearInterval = cfg.CacheClearInterval

	bddSlabChunk = cfg.SlabChunkSize
	bddSlabShift = uint(log2Pow2(cfg.SlabChunkSize))
	bddSlabMask = uint64(cfg.SlabChunkSize - 1)

	bddShardCount = cfg.ShardCount
	bddShardMask = uint64(cfg.ShardCount - 1)
	bddInitialShardCap = cfg.InitialShardCap
	bddInitialShardMask = uint64(cfg.InitialShardCap - 1)
	shards = make([]uniqueShard, cfg.ShardCount)
	for i := range shards {
		shards[i].slots = make([]shardSlot, cfg.InitialShardCap)
		shards[i].mask = bddInitialShardMask
	}
}

func checkPow2(name string, v int) {
	if v <= 0 || v&(v-1) != 0 {
		panic(fmt.Sprintf("bdd: %s must be a positive power of two (got %d)", name, v))
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

// ---------------------------------------------------------------------------
// BDD node slab.
// ---------------------------------------------------------------------------

// bddMaxChunks caps the slab chunk directory. With the default
// SlabChunkSize of 2^18 nodes per chunk this gives a 2^33 ≈ 8 G node
// hard ceiling. The directory itself is bddMaxChunks * 8 bytes = 256 KB
// regardless of how many chunks are live, so the overhead is free.
// sre-ndd bgp_fattree12 consumes over 1 G BDD nodes cumulatively; the
// earlier 2^12 ceiling panicked there.
const bddMaxChunks = 1 << 15

var (
	bddSlabChunk int
	bddSlabShift uint
	bddSlabMask  uint64
)

type bddSlabState struct {
	chunks [bddMaxChunks]atomic.Pointer[[]Node]
	next   atomic.Int64
	growMu sync.Mutex
}

var bddSlab bddSlabState

func allocBDDNode() *Node {
	gi := bddSlab.next.Add(1) - 1
	chunkIdx := int(uint64(gi) >> bddSlabShift)
	slotIdx := int(uint64(gi) & bddSlabMask)
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
// Unique table.
// ---------------------------------------------------------------------------

// Resize when count*10 > len*shardResizeLoad; shardResizeLoad=5 means
// expand at 50 % load. At 70 % (the previous value) linear-probe average
// chain length was 1/(1-0.7) ≈ 3.3, each probe step a near-guaranteed
// DRAM miss at SRE scale (256 MB table). Dropping to 50 % halves probe
// steps on average at the cost of 2× peak table memory.
const shardResizeLoad = 5

var (
	bddShardCount       int
	bddShardMask        uint64
	bddInitialShardCap  int
	bddInitialShardMask uint64
)

// shardSlot stores the node as uintptr to keep the unique-table array
// out of GC's pointer-bitmap sweep. The slab (bddSlab.chunks) is what
// actually roots nodes; this slot's pointer was redundant for GC
// rooting. Reset() clears slots before dropping slab chunks, so the
// uintptr can never dangle while the slot is live.
type shardSlot struct {
	hash uint64
	node uintptr
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

var shards []uniqueShard

func keyHash(k nodeKey) uint64 {
	h := uint64(k.v) * 0x9E3779B97F4A7C15
	h ^= uint64(k.lo) * 0xBF58476D1CE4E5B9
	h ^= uint64(k.hi) * 0x94D049BB133111EB
	h ^= h >> 32
	return h
}

func intern(v uint32, lo, hi *Node) *Node {
	k := nodeKey{
		v:  v,
		lo: uintptr(unsafe.Pointer(lo)),
		hi: uintptr(unsafe.Pointer(hi)),
	}
	h := keyHash(k)
	s := &shards[h&bddShardMask]
	s.mu.Lock()
	idx := h & s.mask
	for {
		slot := &s.slots[idx]
		if slot.node == 0 {
			n := allocBDDNode()
			n.Var = v
			n.id = nextBDDNodeID.Add(1)
			n.Low = lo
			n.High = hi
			slot.hash = h
			slot.node = uintptr(unsafe.Pointer(n))
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
			n := (*Node)(unsafe.Pointer(slot.node))
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

// Reset clears all interned BDD nodes and the op cache.
func Reset() {
	clearBDDCache()
	ResetSatCountCache()
	for i := range shards {
		s := &shards[i]
		s.mu.Lock()
		s.slots = make([]shardSlot, bddInitialShardCap)
		s.mask = bddInitialShardMask
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
	for i := range shards {
		s := &shards[i]
		s.mu.Lock()
		n += s.count
		s.mu.Unlock()
	}
	return n
}
