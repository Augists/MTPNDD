package mtpndd

import (
	"sync"
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// SatCount returns the number of satisfying assignments of n across all BDD
// variables declared by engine. Fields that n does not mention at a given
// level contribute free factors of 2^bitWidth.
//
// Intermediate subgraph counts are memoised in a sharded global map keyed
// by (node, fieldIdx). Nodes are immutable for the life of a session so the
// memo is valid until mtpndd.Reset() (which clears it). The sharding keeps
// RWMutex contention low at 4+ worker concurrency (sre-ndd workloads do
// millions of concurrent SatCount calls).
type satCountKey struct {
	node    *Node
	fieldIx int32
}

const (
	satMemoShards = 64
	satMemoMask   = satMemoShards - 1
)

type satMemoShard struct {
	mu sync.RWMutex
	m  map[satCountKey]float64
}

var satMemoTable [satMemoShards]satMemoShard

func init() {
	for i := range satMemoTable {
		satMemoTable[i].m = make(map[satCountKey]float64, 256)
	}
}

func satShardFor(k satCountKey) *satMemoShard {
	// Spread by node pointer low bits + fieldIdx.
	h := uint64(uintptr(unsafe.Pointer(k.node)))>>3 ^ uint64(k.fieldIx)
	return &satMemoTable[h&satMemoMask]
}

// resetSatCountCache is called from Reset() to invalidate memoised values
// (node pointers may be reused after Reset, so we must drop all entries).
func resetSatCountCache() {
	for i := range satMemoTable {
		s := &satMemoTable[i]
		s.mu.Lock()
		s.m = make(map[satCountKey]float64, 256)
		s.mu.Unlock()
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
			r *= pow2int(engine.fields[i].BitWidth)
		}
		return r
	}
	// Skip over fields n does not constrain at this level.
	skipFactor := 1.0
	for fieldIdx < len(engine.fields) && engine.fields[fieldIdx].ID != n.fieldID {
		skipFactor *= pow2int(engine.fields[fieldIdx].BitWidth)
		fieldIdx++
	}
	key := satCountKey{node: n, fieldIx: int32(fieldIdx)}
	shard := satShardFor(key)
	shard.mu.RLock()
	if v, ok := shard.m[key]; ok {
		shard.mu.RUnlock()
		return skipFactor * v
	}
	shard.mu.RUnlock()

	field := engine.fields[fieldIdx]
	bitsUpTo := field.bddVarBase + field.BitWidth

	total := 0.0
	for _, e := range n.edges {
		labelCount := bdd.SatCount(e.label, bitsUpTo) / pow2int(field.bddVarBase)
		childCount := satCountRec(e.child, fieldIdx+1, engine)
		total += labelCount * childCount
	}
	shard.mu.Lock()
	shard.m[key] = total
	shard.mu.Unlock()
	return skipFactor * total
}

func pow2int(n uint32) float64 {
	p := 1.0
	for range n {
		p *= 2
	}
	return p
}
