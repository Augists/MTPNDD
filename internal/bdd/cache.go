package bdd

import (
	"sync/atomic"
	"unsafe"
)

type opTag uint8

const (
	opAnd opTag = iota
	opOr
	opXor
	opNot
	opExist
)

// opSlot stores the result pointer as a uintptr to keep the op cache out
// of Go's GC scan path. Nodes are pinned by the unique table for the life
// of a session, so the uintptr can never become dangling while the slot is
// live; Reset() clears the slot before freeing nodes. Removing ~2^22 *Node
// fields from GC's pointer bitmap cut scanObject time ~40 % on fattree12
// MF=3 w=4.
type opSlot struct {
	seq atomic.Uint64
	fp  uint64
	res uintptr
}

var (
	opCache               []opSlot
	opCacheMask           uint64
	bddCacheClearInterval uint64
	bddCachePutCount      atomic.Uint64
)

func bddFingerprint(tag opTag, idA, idB uint64, aux uint32) uint64 {
	h := idA*0x9E3779B97F4A7C15 ^ idB*0xBF58476D1CE4E5B9 ^ ((uint64(aux) << 8) | uint64(tag))
	h ^= h >> 32
	h *= 0xFF51AFD7ED558CCD
	h ^= h >> 32
	return h
}

func cacheGet(tag opTag, a, b *Node, aux uint32) (*Node, bool) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := bddFingerprint(tag, a.id, idB, aux)
	s := &opCache[fp&opCacheMask]
	seq1 := s.seq.Load()
	if seq1&1 != 0 {
		return nil, false
	}
	fpV := s.fp
	if fpV != fp {
		return nil, false
	}
	resV := s.res
	if s.seq.Load() != seq1 {
		return nil, false
	}
	return (*Node)(unsafe.Pointer(resV)), true
}

func cachePut(tag opTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := bddFingerprint(tag, a.id, idB, aux)
	s := &opCache[fp&opCacheMask]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.fp = fp
	s.res = uintptr(unsafe.Pointer(res))
	s.seq.Store(seq + 2)
	// The periodic clear feature exists for hosts that want to cap cache
	// footprint; under this project's strong-ref model it's mostly dead
	// weight. When the interval is effectively disabled (as JNI callers
	// set it) skip the atomic increment entirely — under concurrent load
	// it becomes a cache-line-ping hotspot (880 ms flat / 2.3 % of CPU on
	// sre-ndd fattree12 MF=3 at w=4).
	if bddCacheClearInterval < (1 << 40) {
		if c := bddCachePutCount.Add(1); c%bddCacheClearInterval == 0 {
			clearBDDCache()
		}
	}
}

func clearBDDCache() {
	for i := range opCache {
		s := &opCache[i]
		seq := s.seq.Load()
		if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
			continue
		}
		s.fp = 0
		s.res = 0
		s.seq.Store(seq + 2)
	}
}
