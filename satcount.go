package mtpndd

import (
	"sync"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// SatCount returns the number of satisfying assignments of n across all BDD
// variables declared by engine. Fields that n does not mention at a given
// level contribute free factors of 2^bitWidth.
//
// Intermediate subgraph counts are memoised in a global map keyed by
// (node, fieldIdx). Nodes are immutable for the life of a session so the
// memo is valid until mtpndd.Reset() (which clears it). This collapses the
// cost of repeated SatCount calls on overlapping sub-DAGs — e.g. sre-ndd
// workloads invoke SatCount ~10^6 times on heavily shared NDD subgraphs;
// without the global memo each call pays a full DAG walk.
type satCountKey struct {
	node    *Node
	fieldIx int32
}

var satMemo struct {
	mu sync.RWMutex
	m  map[satCountKey]float64
}

func init() { satMemo.m = make(map[satCountKey]float64, 4096) }

// resetSatCountCache is called from Reset() to invalidate memoised values
// (node pointers may be reused after Reset, so we must drop all entries).
func resetSatCountCache() {
	satMemo.mu.Lock()
	satMemo.m = make(map[satCountKey]float64, 4096)
	satMemo.mu.Unlock()
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
	satMemo.mu.RLock()
	if v, ok := satMemo.m[key]; ok {
		satMemo.mu.RUnlock()
		return skipFactor * v
	}
	satMemo.mu.RUnlock()

	field := engine.fields[fieldIdx]
	bitsUpTo := field.bddVarBase + field.BitWidth

	total := 0.0
	for _, e := range n.edges {
		labelCount := bdd.SatCount(e.label, bitsUpTo) / pow2int(field.bddVarBase)
		childCount := satCountRec(e.child, fieldIdx+1, engine)
		total += labelCount * childCount
	}
	satMemo.mu.Lock()
	satMemo.m[key] = total
	satMemo.mu.Unlock()
	return skipFactor * total
}

func pow2int(n uint32) float64 {
	p := 1.0
	for range n {
		p *= 2
	}
	return p
}
