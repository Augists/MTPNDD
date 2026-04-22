package bdd

import (
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
	// Skipped variables between depth and f.Var contribute 2^skip.
	skip := f.Var - depth
	factor := pow2(skip)
	lo := satCountRec(f.Low, f.Var+1, nvars)
	hi := satCountRec(f.High, f.Var+1, nvars)
	return factor * (lo + hi)
}

func pow2(n uint32) float64 {
	p := 1.0
	for range n {
		p *= 2
	}
	return p
}
