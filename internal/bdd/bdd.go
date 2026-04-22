// Package bdd implements a small, parallel, reduced-ordered binary decision
// diagram library with two terminal values (True, False).
//
// Design notes:
//
//   - Nodes are canonicalized through a sharded unique table. The table holds
//     weak references so that unreachable nodes become eligible for the Go GC
//     without any explicit Ref/Deref calls.
//   - Edge labels in MTPNDD are BDDs from this package; the NDD layer holds
//     strong *Node references inside its edge maps, so any live NDD node
//     keeps its BDD labels alive automatically.
//   - Variable ordering is by varIdx ascending (smaller index = closer to root).
package bdd

import (
	"math"
)

// terminalVar is the sentinel variable index used for the True/False leaves.
const terminalVar uint32 = math.MaxUint32

// Node is a BDD node.
type Node struct {
	Var  uint32
	Low  *Node
	High *Node
}

// True and False are the two BDD terminals.
var (
	True  = &Node{Var: terminalVar}
	False = &Node{Var: terminalVar}
)

// IsTerminal reports whether n is True or False.
func IsTerminal(n *Node) bool { return n == True || n == False }

// Mk returns the canonical node (v, lo, hi), applying the BDD reduction rule.
func Mk(v uint32, lo, hi *Node) *Node {
	if lo == hi {
		return lo
	}
	return unique.intern(v, lo, hi)
}

// IthVar returns a BDD representing the positive literal for variable v.
func IthVar(v uint32) *Node { return Mk(v, False, True) }

// NIthVar returns a BDD representing the negative literal for variable v.
func NIthVar(v uint32) *Node { return Mk(v, True, False) }

// cofactor returns (f|v=0, f|v=1) where v is assumed <= f.Var.
func cofactor(f *Node, v uint32) (*Node, *Node) {
	if f.Var == v {
		return f.Low, f.High
	}
	return f, f
}

// topVar returns the smaller of the two variable indices.
func topVar(a, b *Node) uint32 {
	va, vb := a.Var, b.Var
	if va < vb {
		return va
	}
	return vb
}
