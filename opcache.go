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

// nddOpSlot is a compact 24-byte slot: seqlock counter, 64-bit
// fingerprint encoding (tag, aux, idA, idB), and a strong result
// pointer. The full fingerprint replaces the separate tag/aux/idA/idB
// fields, cutting the slot size from 40 to 24 bytes and improving
// L3/L2 locality at N ≥ 13. Collision probability ~2⁻⁶⁴.
type nddOpSlot struct {
	seq atomic.Uint64
	fp  uint64
	res *Node
}

const (
	nddOpCacheSize        = 1 << 19
	nddCacheClearInterval = 1 << 21
)

var (
	nddOpCache       [nddOpCacheSize]nddOpSlot
	nddCachePutCount atomic.Uint64
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
	s := &nddOpCache[fp&(nddOpCacheSize-1)]
	seq1 := s.seq.Load()
	if seq1&1 != 0 {
		return nil, false
	}
	fpV := s.fp
	// Fast-miss exit. On an fp mismatch we return a miss regardless of
	// any concurrent writer, so the second seq-load check is only needed
	// when we are about to return a real result.
	if fpV != fp {
		return nil, false
	}
	resV := s.res
	if s.seq.Load() != seq1 {
		return nil, false
	}
	return resV, true
}

func nddCachePut(tag nddOpTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := nddFingerprint(tag, a.id, idB, aux)
	s := &nddOpCache[fp&(nddOpCacheSize-1)]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.fp = fp
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
		s.fp = 0
		s.res = nil
		s.seq.Store(seq + 2)
	}
}
