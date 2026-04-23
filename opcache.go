package mtpndd

import (
	"sync/atomic"
	"unsafe"
)

type nddOpTag uint8

const (
	opAnd nddOpTag = iota
	opOr
	opNot
	opDiff
	opExist
)

// nddOpSlot is a compact 24-byte slot: seqlock counter, 64-bit fingerprint,
// and a result pointer stored as uintptr so GC does not scan the entire
// op-cache array. Nodes are pinned by the unique table for the life of a
// session; Reset() clears slots before freeing nodes so the uintptr can
// never go dangling while the slot is live. See docs/design-decisions.md §4
// and the v1.15 changelog entry.
type nddOpSlot struct {
	seq atomic.Uint64
	fp  uint64
	res uintptr
}

var (
	nddOpCache            []nddOpSlot
	nddOpCacheMask        uint64
	nddCacheClearInterval uint64
	nddCachePutCount      atomic.Uint64
)

func nddFingerprint(tag nddOpTag, idA, idB uint64, aux uint32) uint64 {
	h := idA*0x9E3779B97F4A7C15 ^ idB*0xBF58476D1CE4E5B9 ^ ((uint64(aux) << 8) | uint64(tag))
	h ^= h >> 32
	h *= 0xFF51AFD7ED558CCD
	h ^= h >> 32
	return h
}

func nddCacheGet(tag nddOpTag, a, b *Node, aux uint32) (*Node, bool) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := nddFingerprint(tag, a.id, idB, aux)
	s := &nddOpCache[fp&nddOpCacheMask]
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

func nddCachePut(tag nddOpTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := nddFingerprint(tag, a.id, idB, aux)
	s := &nddOpCache[fp&nddOpCacheMask]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.fp = fp
	s.res = uintptr(unsafe.Pointer(res))
	s.seq.Store(seq + 2)
	if nddCacheClearInterval < (1 << 40) {
		if c := nddCachePutCount.Add(1); c%nddCacheClearInterval == 0 {
			clearNDDCache()
		}
	}
}

func clearNDDCache() {
	for i := range nddOpCache {
		s := &nddOpCache[i]
		seq := s.seq.Load()
		if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
			continue
		}
		s.fp = 0
		s.res = 0
		s.seq.Store(seq + 2)
	}
}
