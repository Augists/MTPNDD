package bdd

import (
	"sync/atomic"
	"unsafe"
	"weak"
)

type opTag uint8

const (
	opAnd opTag = iota
	opOr
	opXor
	opNot
	opExist
)

// opEntry uses weak.Pointer for both operands and the result. See the
// note in mtpndd/opcache.go for the rationale (stale-address aliasing
// protection).
type opEntry struct {
	tag opTag
	aux uint32
	a   weak.Pointer[Node]
	b   weak.Pointer[Node]
	res weak.Pointer[Node]
}

const opCacheSize = 1 << 20

var opCache [opCacheSize]atomic.Pointer[opEntry]

func opHashPtr(tag opTag, a, b *Node, aux uint32) uint32 {
	h := uint64(uintptr(unsafe.Pointer(a))) * 0x9E3779B97F4A7C15
	h ^= uint64(uintptr(unsafe.Pointer(b))) * 0xBF58476D1CE4E5B9
	h ^= (uint64(aux) << 8) | uint64(tag)
	h ^= h >> 32
	return uint32(h) & (opCacheSize - 1)
}

func cacheGet(tag opTag, a, b *Node, aux uint32) (*Node, bool) {
	e := opCache[opHashPtr(tag, a, b, aux)].Load()
	if e == nil || e.tag != tag || e.aux != aux {
		return nil, false
	}
	if e.a.Value() != a {
		return nil, false
	}
	if b != nil && e.b.Value() != b {
		return nil, false
	}
	res := e.res.Value()
	if res == nil {
		return nil, false
	}
	return res, true
}

func cachePut(tag opTag, a, b, res *Node, aux uint32) {
	var bw weak.Pointer[Node]
	if b != nil {
		bw = weak.Make(b)
	}
	e := &opEntry{
		tag: tag,
		aux: aux,
		a:   weak.Make(a),
		b:   bw,
		res: weak.Make(res),
	}
	opCache[opHashPtr(tag, a, b, aux)].Store(e)
}
