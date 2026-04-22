package bdd

import (
	"runtime"
	"sync"
	"unsafe"
	"weak"
)

const (
	shardCount      = 64
	shardMask       = shardCount - 1
	sweepEvery      = 4096
	initialShardCap = 256
)

type nodeKey struct {
	v  uint32
	lo uintptr
	hi uintptr
}

type uniqueShard struct {
	mu       sync.Mutex
	table    map[nodeKey]weak.Pointer[Node]
	opsSince uint32
}

type uniqueTable struct {
	shards [shardCount]uniqueShard
}

var unique = func() *uniqueTable {
	t := &uniqueTable{}
	for i := range t.shards {
		t.shards[i].table = make(map[nodeKey]weak.Pointer[Node], initialShardCap)
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
	if wp, ok := s.table[k]; ok {
		if n := wp.Value(); n != nil {
			s.mu.Unlock()
			runtime.KeepAlive(lo)
			runtime.KeepAlive(hi)
			return n
		}
	}
	n := &Node{Var: v, Low: lo, High: hi}
	s.table[k] = weak.Make(n)
	s.opsSince++
	if s.opsSince >= sweepEvery {
		s.opsSince = 0
		s.sweepLocked()
	}
	s.mu.Unlock()
	runtime.KeepAlive(lo)
	runtime.KeepAlive(hi)
	return n
}

func (s *uniqueShard) sweepLocked() {
	for k, wp := range s.table {
		if wp.Value() == nil {
			delete(s.table, k)
		}
	}
}

// TableSize returns the approximate number of live entries across all shards.
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
