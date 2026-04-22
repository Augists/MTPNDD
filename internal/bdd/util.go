package bdd

import "unsafe"

func uintptrOf(n *Node) uintptr { return uintptr(unsafe.Pointer(n)) }

// subproblemBig decides whether an apply on (a, b) is large enough to be
// worth the goroutine round-trip. Spawning a Go goroutine costs on the order
// of 1-5µs whereas a tight BDD apply call costs well under 1µs, so the
// threshold needs to be very conservative: only parallelize when both
// operands are close to the top of the variable ordering (potential subtree
// size on the order of 2^(N-top)).
func subproblemBig(a, b *Node) bool {
	_ = a
	_ = b
	return false // goroutines cost more than BDD subproblems at nqueens scale
}
