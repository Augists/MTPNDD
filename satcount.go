package mtpndd

import "github.com/Augists/mtpndd-go/internal/bdd"

// SatCount returns the number of satisfying assignments of n across all BDD
// variables declared by engine. Fields that n does not mention at a given
// level contribute free factors of 2^bitWidth.
//
// The implementation assumes NDD nodes carry disjoint-label edge sets at each
// level (the canonical form produced by our Or/Not/Exist).
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
	field := engine.fields[fieldIdx]
	bitsUpTo := field.bddVarBase + field.BitWidth

	total := 0.0
	for _, e := range n.edges {
		// |{σ on [bddVarBase, bddVarBase+BitWidth) : label(σ) = True}|.
		// bdd.SatCount counts over [0, bitsUpTo); vars below bddVarBase are
		// free in the label and contribute 2^bddVarBase that we divide out.
		labelCount := bdd.SatCount(e.label, bitsUpTo) / pow2int(field.bddVarBase)
		childCount := satCountRec(e.child, fieldIdx+1, engine)
		total += labelCount * childCount
	}
	return skipFactor * total
}

func pow2int(n uint32) float64 {
	p := 1.0
	for range n {
		p *= 2
	}
	return p
}
