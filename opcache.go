package mtpndd

import (
	"sync/atomic"
)

type nddOpTag uint8

const (
	opAnd nddOpTag = iota
	opOr
	opNot
	opDiff
	opExist
)

// nddOpSlot is inlined in a flat array: one slot per hashed position. No
// allocation happens on cache put. Concurrency is a per-slot seqlock:
// writers CAS seq from even to odd, readers load seq before and after
// reading payload fields. Losing writers silently drop their put.
//
// Operand validation compares node IDs (not pointers or weak.Pointer):
// IDs are monotonically assigned and never reused, so the earlier
// weak.Pointer workaround for address-reuse aliasing is unnecessary.
type nddOpSlot struct {
	seq atomic.Uint64
	tag nddOpTag
	aux uint32
	idA uint64
	idB uint64
	res *Node
}

const (
	nddOpCacheSize        = 1 << 20
	nddCacheClearInterval = 1 << 22
)

var (
	nddOpCache       [nddOpCacheSize]nddOpSlot
	nddCachePutCount atomic.Uint64
)

func nddOpHash(tag nddOpTag, idA, idB uint64, aux uint32) uint32 {
	h := idA * 0x9E3779B97F4A7C15
	h ^= idB * 0xBF58476D1CE4E5B9
	h ^= (uint64(aux) << 8) | uint64(tag)
	h ^= h >> 32
	return uint32(h) & (nddOpCacheSize - 1)
}

func nddCacheGet(tag nddOpTag, a, b *Node, aux uint32) (*Node, bool) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	s := &nddOpCache[nddOpHash(tag, a.id, idB, aux)]
	seq1 := s.seq.Load()
	if seq1&1 != 0 {
		return nil, false
	}
	tagV := s.tag
	auxV := s.aux
	idAV := s.idA
	idBV := s.idB
	resV := s.res
	if s.seq.Load() != seq1 {
		return nil, false
	}
	if tagV != tag || auxV != aux || idAV != a.id || idBV != idB {
		return nil, false
	}
	return resV, true
}

func nddCachePut(tag nddOpTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	s := &nddOpCache[nddOpHash(tag, a.id, idB, aux)]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.tag = tag
	s.aux = aux
	s.idA = a.id
	s.idB = idB
	s.res = res
	s.seq.Store(seq + 2)
	if c := nddCachePutCount.Add(1); c%nddCacheClearInterval == 0 {
		clearNDDCache()
	}
}

func clearNDDCache() {
	for i := range nddOpCache {
		s := &nddOpCache[i]
		seq := s.seq.Load()
		if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
			continue
		}
		s.res = nil
		s.idA = 0
		s.idB = 0
		s.seq.Store(seq + 2)
	}
}
