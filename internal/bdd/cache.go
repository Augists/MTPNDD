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

// opSlot mirrors the NDD seqlock design.
type opSlot struct {
	seq atomic.Uint64
	tag opTag
	aux uint32
	idA uint64
	idB uint64
	res *Node
}

const (
	opCacheSize           = 1 << 20
	bddCacheClearInterval = 1 << 22
)

var (
	opCache          [opCacheSize]opSlot
	bddCachePutCount atomic.Uint64
)

func opHashID(tag opTag, idA, idB uint64, aux uint32) uint32 {
	h := idA * 0x9E3779B97F4A7C15
	h ^= idB * 0xBF58476D1CE4E5B9
	h ^= (uint64(aux) << 8) | uint64(tag)
	h ^= h >> 32
	return uint32(h) & (opCacheSize - 1)
}

func cacheGet(tag opTag, a, b *Node, aux uint32) (*Node, bool) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	s := &opCache[opHashID(tag, a.id, idB, aux)]
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

func cachePut(tag opTag, a, b, res *Node, aux uint32) {
	var idB uint64
	if b != nil {
		idB = b.id
	}
	s := &opCache[opHashID(tag, a.id, idB, aux)]
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
		s.res = nil
		s.idA = 0
		s.idB = 0
		s.seq.Store(seq + 2)
	}
}
