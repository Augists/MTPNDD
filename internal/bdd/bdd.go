// Package bdd implements a small, parallel, reduced-ordered binary decision
// diagram library with two terminal values (True, False).
package bdd

import (
	"math"
)

// terminalVar is the sentinel variable index used for the True/False leaves.
const terminalVar uint32 = math.MaxUint32

// Node is a BDD node. id is a monotonically-increasing identifier assigned at
// creation; it is used by op caches to validate operand identity without
// needing weak pointers.
type Node struct {
	Var  uint32
	id   uint64
	Low  *Node
	High *Node
}

// ID returns the node's unique id. 0 is reserved for the two terminals.
func (n *Node) ID() uint64 { return n.id }

// True and False are the two BDD terminals.
var (
	True  = &Node{Var: terminalVar, id: 1}
	False = &Node{Var: terminalVar, id: 2}
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

func cofactor(f *Node, v uint32) (*Node, *Node) {
	if f.Var == v {
		return f.Low, f.High
	}
	return f, f
}

func topVar(a, b *Node) uint32 {
	va, vb := a.Var, b.Var
	if va < vb {
		return va
	}
	return vb
}
