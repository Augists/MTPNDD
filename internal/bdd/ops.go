package bdd

import (
	"sync"

	"github.com/Augists/mtpndd-go/internal/work"
)

// Not returns NOT f.
func Not(f *Node) *Node {
	if f == True {
		return False
	}
	if f == False {
		return True
	}
	if r, ok := cacheGet(opNot, f, nil, 0); ok {
		return r
	}
	lo := Not(f.Low)
	hi := Not(f.High)
	r := Mk(f.Var, lo, hi)
	cachePut(opNot, f, nil, r, 0)
	return r
}

// And returns f AND g.
func And(f, g *Node) *Node {
	// Terminal short-circuits.
	if f == False || g == False {
		return False
	}
	if f == True {
		return g
	}
	if g == True {
		return f
	}
	if f == g {
		return f
	}
	// Canonicalize operand order so (a,b) and (b,a) hit the same cache slot.
	a, b := f, g
	if uintptrOf(a) > uintptrOf(b) {
		a, b = b, a
	}
	if r, ok := cacheGet(opAnd, a, b, 0); ok {
		return r
	}
	v := topVar(a, b)
	a0, a1 := cofactor(a, v)
	b0, b1 := cofactor(b, v)
	var lo, hi *Node
	if subproblemBig(a, b) {
		fut := work.Go(func() { lo = And(a0, b0) })
		hi = And(a1, b1)
		fut.Wait()
	} else {
		lo = And(a0, b0)
		hi = And(a1, b1)
	}
	r := Mk(v, lo, hi)
	cachePut(opAnd, a, b, r, 0)
	return r
}

// Or returns f OR g.
func Or(f, g *Node) *Node {
	if f == True || g == True {
		return True
	}
	if f == False {
		return g
	}
	if g == False {
		return f
	}
	if f == g {
		return f
	}
	a, b := f, g
	if uintptrOf(a) > uintptrOf(b) {
		a, b = b, a
	}
	if r, ok := cacheGet(opOr, a, b, 0); ok {
		return r
	}
	v := topVar(a, b)
	a0, a1 := cofactor(a, v)
	b0, b1 := cofactor(b, v)
	var lo, hi *Node
	if subproblemBig(a, b) {
		fut := work.Go(func() { lo = Or(a0, b0) })
		hi = Or(a1, b1)
		fut.Wait()
	} else {
		lo = Or(a0, b0)
		hi = Or(a1, b1)
	}
	r := Mk(v, lo, hi)
	cachePut(opOr, a, b, r, 0)
	return r
}

// Xor returns f XOR g.
func Xor(f, g *Node) *Node {
	if f == False {
		return g
	}
	if g == False {
		return f
	}
	if f == True {
		return Not(g)
	}
	if g == True {
		return Not(f)
	}
	if f == g {
		return False
	}
	a, b := f, g
	if uintptrOf(a) > uintptrOf(b) {
		a, b = b, a
	}
	if r, ok := cacheGet(opXor, a, b, 0); ok {
		return r
	}
	v := topVar(a, b)
	a0, a1 := cofactor(a, v)
	b0, b1 := cofactor(b, v)
	lo := Xor(a0, b0)
	hi := Xor(a1, b1)
	r := Mk(v, lo, hi)
	cachePut(opXor, a, b, r, 0)
	return r
}

// Diff returns f AND NOT g. Equivalent to And(f, Not(g)) but computed
// directly so (1) no intermediate NOT node is created and (2) the op
// cache entry is keyed on (f, g) instead of on (f, Not(g)), which means
// the cascading recursive calls also hit Diff cache entries rather than
// And-on-transient-Not-result entries. Used by NDD orSameField to
// subtract intersect labels from residuals.
func Diff(f, g *Node) *Node {
	if f == False || g == True {
		return False
	}
	if g == False {
		return f
	}
	if f == g {
		return False
	}
	if f == True {
		return Not(g)
	}
	if r, ok := cacheGet(opDiff, f, g, 0); ok {
		return r
	}
	v := topVar(f, g)
	f0, f1 := cofactor(f, v)
	g0, g1 := cofactor(g, v)
	lo := Diff(f0, g0)
	hi := Diff(f1, g1)
	r := Mk(v, lo, hi)
	cachePut(opDiff, f, g, r, 0)
	return r
}

// Exist returns ∃v. f.
func Exist(f *Node, v uint32) *Node {
	if IsTerminal(f) {
		return f
	}
	if f.Var > v {
		// v does not appear in f.
		return f
	}
	if f.Var == v {
		return Or(f.Low, f.High)
	}
	if r, ok := cacheGet(opExist, f, nil, v); ok {
		return r
	}
	lo := Exist(f.Low, v)
	hi := Exist(f.High, v)
	r := Mk(f.Var, lo, hi)
	cachePut(opExist, f, nil, r, v)
	return r
}

// ExistMany quantifies a set of variables (given sorted ascending).
func ExistMany(f *Node, vars []uint32) *Node {
	for _, v := range vars {
		f = Exist(f, v)
	}
	return f
}

// SatCount returns the number of satisfying assignments over `nvars`
// variables. Variables not mentioned in f are treated as free.
//
// Results are memoised in a process-global table keyed by (node, nvars).
// Nodes are immutable for the life of a session so the memo is valid
// until Reset() (which clears it via ResetSatCountCache below).
type satKey struct {
	node  *Node
	nvars uint32
}

var satMemo struct {
	mu sync.RWMutex
	m  map[satKey]float64
}

func init() { satMemo.m = make(map[satKey]float64, 4096) }

// ResetSatCountCache drops all memoised values. Called from bdd.Reset.
func ResetSatCountCache() {
	satMemo.mu.Lock()
	satMemo.m = make(map[satKey]float64, 4096)
	satMemo.mu.Unlock()
}

func SatCount(f *Node, nvars uint32) float64 {
	if f == False {
		return 0
	}
	if f == True {
		return pow2(nvars)
	}
	return satCountRec(f, 0, nvars)
}

func satCountRec(f *Node, depth, nvars uint32) float64 {
	if f == False {
		return 0
	}
	if f == True {
		return pow2(nvars - depth)
	}
	skip := f.Var - depth
	factor := pow2(skip)
	key := satKey{node: f, nvars: nvars}
	satMemo.mu.RLock()
	if v, ok := satMemo.m[key]; ok {
		satMemo.mu.RUnlock()
		return factor * v
	}
	satMemo.mu.RUnlock()
	lo := satCountRec(f.Low, f.Var+1, nvars)
	hi := satCountRec(f.High, f.Var+1, nvars)
	sum := lo + hi
	satMemo.mu.Lock()
	satMemo.m[key] = sum
	satMemo.mu.Unlock()
	return factor * sum
}

func pow2(n uint32) float64 {
	p := 1.0
	for range n {
		p *= 2
	}
	return p
}
