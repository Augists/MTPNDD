// Package mtpndd implements Multi-Terminal Parallel Network Decision Diagrams
// in pure Go. The v1 surface intentionally covers only boolean NDDs with the
// True and False terminals; multi-terminal (fraction / double) leaves will be
// added later.
package mtpndd

import (
	"math"
	"unsafe"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// terminalField marks True/False; a real field ID will never reach this value.
const terminalField uint32 = math.MaxUint32

// Node is an NDD node. Internal state is immutable once interned.
type Node struct {
	fieldID uint32
	id      uint64 // monotonic; enables cheap cache-operand validation without weak pointers
	edges   []edge
	hash    uint64
}

// ID returns the node's unique id. Terminals use small reserved values.
func (n *Node) ID() uint64 { return n.id }

// edge is one (child, label) pair in a node's outgoing edge set.
type edge struct {
	child *Node
	label *bdd.Node
}

// True and False are the two NDD terminals.
var (
	True  = &Node{fieldID: terminalField, id: 1}
	False = &Node{fieldID: terminalField, id: 2}
)

// IsTerminal reports whether n is True or False.
func IsTerminal(n *Node) bool { return n == True || n == False }

// FieldID returns the field this node branches on, or terminalField for leaves.
func (n *Node) FieldID() uint32 { return n.fieldID }

// --- hashing ---------------------------------------------------------------

func edgeHash(child *Node, label *bdd.Node) uint64 {
	h := uint64(uintptr(unsafe.Pointer(child))) * 0x9E3779B97F4A7C15
	h ^= uint64(uintptr(unsafe.Pointer(label))) * 0xBF58476D1CE4E5B9
	h ^= h >> 33
	h *= 0xFF51AFD7ED558CCD
	h ^= h >> 33
	return h
}

func mixFieldHash(field uint32, edgeXor uint64) uint64 {
	h := uint64(field) * 0x94D049BB133111EB
	h ^= edgeXor
	h ^= h >> 32
	return h
}

// sortEdges orders edges by child pointer then label pointer ascending.
// Specialised insertion sort; nearly all edge lists are ≤ 4 items, and
// sort.Slice was reflection-heavy.
func sortEdges(es []edge) {
	if len(es) < 2 {
		return
	}
	for i := 1; i < len(es); i++ {
		cur := es[i]
		curC := uintptr(unsafe.Pointer(cur.child))
		curL := uintptr(unsafe.Pointer(cur.label))
		j := i - 1
		for j >= 0 {
			pC := uintptr(unsafe.Pointer(es[j].child))
			if pC < curC || (pC == curC && uintptr(unsafe.Pointer(es[j].label)) <= curL) {
				break
			}
			es[j+1] = es[j]
			j--
		}
		es[j+1] = cur
	}
}

// edgesEqual compares two already-sorted edge lists.
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
