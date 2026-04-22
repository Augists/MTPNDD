package mtpndd

import (
	"sync/atomic"
	"unsafe"
	"weak"
)

type nddOpTag uint8

const (
	opAnd nddOpTag = iota
	opOr
	opNot
	opDiff
	opExist
)

// nddOpEntry uses weak.Pointer for both operands and the result. The
// operand weaks protect against freed-then-reused addresses aliasing
// cache hits — without them, a pointer-keyed cache can silently
// return a stale result for a new operand that happens to occupy the
// same address.
type nddOpEntry struct {
	tag nddOpTag
	aux uint32
	a   weak.Pointer[Node]
	b   weak.Pointer[Node]
	res weak.Pointer[Node]
}

const nddOpCacheSize = 1 << 20

var nddOpCache [nddOpCacheSize]atomic.Pointer[nddOpEntry]

func nddOpHash(tag nddOpTag, a, b *Node, aux uint32) uint32 {
	h := uint64(uintptr(unsafe.Pointer(a))) * 0x9E3779B97F4A7C15
	h ^= uint64(uintptr(unsafe.Pointer(b))) * 0xBF58476D1CE4E5B9
	h ^= (uint64(aux) << 8) | uint64(tag)
	h ^= h >> 32
	return uint32(h) & (nddOpCacheSize - 1)
}

func nddCacheGet(tag nddOpTag, a, b *Node, aux uint32) (*Node, bool) {
	e := nddOpCache[nddOpHash(tag, a, b, aux)].Load()
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

func nddCachePut(tag nddOpTag, a, b, res *Node, aux uint32) {
	var bw weak.Pointer[Node]
	if b != nil {
		bw = weak.Make(b)
	}
	e := &nddOpEntry{
		tag: tag,
		aux: aux,
		a:   weak.Make(a),
		b:   bw,
		res: weak.Make(res),
	}
	nddOpCache[nddOpHash(tag, a, b, aux)].Store(e)
}
