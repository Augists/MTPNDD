// Package mtpndd implements Multi-Terminal Parallel Network Decision Diagrams
// in pure Go. The v1 surface intentionally covers only boolean NDDs with the
// True and False terminals; multi-terminal (fraction / double) leaves will be
// added later.
package mtpndd

import (
	"math"
	"sort"
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// terminalField marks True/False; a real field ID will never reach this value.
const terminalField uint32 = math.MaxUint32

// Node is an NDD node. Internal state is immutable once interned.
//
// An interior node has fieldID < terminalField and one or more outgoing edges;
// each edge carries a BDD label over the current field's variables and points
// to a child NDD representing the remaining fields. Terminals have no edges.
//
// The edges slice is kept sorted by uintptr(child) ascending so two nodes that
// represent the same edge set are bit-identical (enabling structural hashing).
type Node struct {
	fieldID uint32
	edges   []edge
	hash    uint64
}

// edge is one (child, label) pair in a node's outgoing edge set.
type edge struct {
	child *Node
	label *bdd.Node
}

// True and False are the two NDD terminals.
var (
	True  = &Node{fieldID: terminalField}
	False = &Node{fieldID: terminalField}
)

// IsTerminal reports whether n is True or False.
func IsTerminal(n *Node) bool { return n == True || n == False }

// FieldID returns the field this node branches on, or terminalField for leaves.
func (n *Node) FieldID() uint32 { return n.fieldID }

// --- hashing ---------------------------------------------------------------

// edgeHash mixes one (child, label) pair into a 64-bit digest. The XOR of
// per-edge hashes in a node's edge set is order-independent, so two nodes
// with the same set of edges produce the same cumulative hash regardless of
// insertion order.
func edgeHash(child *Node, label *bdd.Node) uint64 {
	h := uint64(uintptr(unsafe.Pointer(child))) * 0x9E3779B97F4A7C15
	h ^= uint64(uintptr(unsafe.Pointer(label))) * 0xBF58476D1CE4E5B9
	h ^= h >> 33
	h *= 0xFF51AFD7ED558CCD
	h ^= h >> 33
	return h
}

// mixFieldHash folds fieldID into the cumulative edge hash.
func mixFieldHash(field uint32, edgeXor uint64) uint64 {
	h := uint64(field) * 0x94D049BB133111EB
	h ^= edgeXor
	h ^= h >> 32
	return h
}

// --- edge-list canonicalization -------------------------------------------

// sortEdges orders edges by child pointer then label pointer ascending so that
// equal sets canonicalize to equal slices.
func sortEdges(es []edge) {
	sort.Slice(es, func(i, j int) bool {
		ai := uintptr(unsafe.Pointer(es[i].child))
		aj := uintptr(unsafe.Pointer(es[j].child))
		if ai != aj {
			return ai < aj
		}
		return uintptr(unsafe.Pointer(es[i].label)) < uintptr(unsafe.Pointer(es[j].label))
	})
}

// edgesEqual compares two already-sorted edge lists for pointer-identical
// equality.
func edgesEqual(a, b []edge) bool {
	if len(a) != len(b) {
		return false
	}
	for i := range a {
		if a[i].child != b[i].child || a[i].label != b[i].label {
			return false
		}
	}
	return true
}
