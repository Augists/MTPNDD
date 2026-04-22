package bdd

import (
	"sync/atomic"
)

type opTag uint8

const (
	opAnd opTag = iota
	opOr
	opXor
	opNot
	opExist
)

// opSlot is a compact 24-byte slot: seqlock counter + fingerprint +
// strong result pointer. See mtpndd/opcache.go for the rationale.
type opSlot struct {
	seq atomic.Uint64
	fp  uint64
	res *Node
}

const (
	opCacheSize           = 1 << 19
	bddCacheClearInterval = 1 << 21
)

var (
	opCache          [opCacheSize]opSlot
	bddCachePutCount atomic.Uint64
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
	s := &opCache[fp&(opCacheSize-1)]
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
	return resV, true
}

func cachePut(tag opTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	fp := bddFingerprint(tag, a.id, idB, aux)
	s := &opCache[fp&(opCacheSize-1)]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.fp = fp
	s.res = res
	s.seq.Store(seq + 2)
	if c := bddCachePutCount.Add(1); c%bddCacheClearInterval == 0 {
		clearBDDCache()
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
		s.res = nil
		s.seq.Store(seq + 2)
	}
}
