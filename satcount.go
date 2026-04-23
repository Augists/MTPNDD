package mtpndd

import (
	"sync/atomic"
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// SatCount returns the number of satisfying assignments of n across all BDD
// variables declared by engine. Fields that n does not mention at a given
// level contribute free factors of 2^bitWidth.
//
// Intermediate (node, fieldIdx) → float64 results are memoised in a global
// open-addressed table with per-slot seqlocks. Nodes are immutable for the
// life of a session so memo entries stay valid until mtpndd.Reset() clears
// the table. Collisions overwrite the previous occupant; the victim is
// simply recomputed next time. The 64-bit fingerprint includes the node
// pointer and fieldIdx so false hits are ~2⁻⁶⁴ per lookup.

const (
	// 2^22 slots × 24 B = 96 MB. SRE fattree08 MF=3 memo peaks around
	// 1-2 M entries, so 4 M slots keeps load factor ≤ 50 % and avoids
	// collision-driven recomputation in the steady state.
	satMemoSlots = 1 << 22
	satMemoMask  = satMemoSlots - 1
)

type satMemoSlot struct {
	seq atomic.Uint64
	fp  uint64
	res float64
}

var satMemoTable [satMemoSlots]satMemoSlot

func satMemoFingerprint(node *Node, fieldIdx int32) uint64 {
	h := uint64(uintptr(unsafe.Pointer(node))) * 0x9E3779B97F4A7C15
	h ^= uint64(uint32(fieldIdx)) * 0xBF58476D1CE4E5B9
	h ^= h >> 32
	h *= 0xFF51AFD7ED558CCD
	h ^= h >> 32
	if h == 0 {
		h = 1
	}
	return h
}

func satMemoGet(node *Node, fieldIdx int32) (float64, bool) {
	fp := satMemoFingerprint(node, fieldIdx)
	s := &satMemoTable[fp&satMemoMask]
	seq1 := s.seq.Load()
	if seq1&1 != 0 {
		return 0, false
	}
	fpV := s.fp
	if fpV != fp {
		return 0, false
	}
	res := s.res
	if s.seq.Load() != seq1 {
		return 0, false
	}
	return res, true
}

func satMemoPut(node *Node, fieldIdx int32, v float64) {
	fp := satMemoFingerprint(node, fieldIdx)
	s := &satMemoTable[fp&satMemoMask]
	seq := s.seq.Load()
	if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
		return
	}
	s.fp = fp
	s.res = v
	s.seq.Store(seq + 2)
}

// resetSatCountCache is called from Reset() to invalidate memoised values
// (node pointers may be reused after Reset, so we must drop all entries).
func resetSatCountCache() {
	for i := range satMemoTable {
		s := &satMemoTable[i]
		seq := s.seq.Load()
		if seq&1 != 0 || !s.seq.CompareAndSwap(seq, seq+1) {
			continue
		}
		s.fp = 0
		s.res = 0
		s.seq.Store(seq + 2)
	}
}

func SatCount(n *Node, engine *Engine) float64 {
	if n == False {
		return 0
	}
	return satCountRec(n, 0, engine)
}

func satCountRec(n *Node, fieldIdx int, engine *Engine) float64 {
	if n == False {
		return 0
	}
	if n == True {
		r := 1.0
		for i := fieldIdx; i < len(engine.fields); i++ {
			r *= engine.fields[i].pow2BitWidth
		}
		return r
	}
	// Skip over fields n does not constrain at this level.
	skipFactor := 1.0
	for fieldIdx < len(engine.fields) && engine.fields[fieldIdx].ID != n.fieldID {
		skipFactor *= engine.fields[fieldIdx].pow2BitWidth
		fieldIdx++
	}
	fx := int32(fieldIdx)
	if v, ok := satMemoGet(n, fx); ok {
		return skipFactor * v
	}

	field := engine.fields[fieldIdx]
	bitsUpTo := field.bddVarBase + field.BitWidth
	invPow2Base := 1.0 / field.pow2Base

	total := 0.0
	for _, e := range n.edges {
		labelCount := bdd.SatCount(e.label, bitsUpTo) * invPow2Base
		childCount := satCountRec(e.child, fieldIdx+1, engine)
		total += labelCount * childCount
	}
	satMemoPut(n, fx, total)
	return skipFactor * total
}
