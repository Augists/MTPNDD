package mtpndd

import (
	"fmt"
	"sync"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// Field is one logical bit-field in the NDD ordering. Fields are appended via
// Engine.DeclareField and finalized by Engine.GenerateFields. The finalized
// order is the variable ordering used by every NDD/BDD operation thereafter.
type Field struct {
	ID       uint32
	BitWidth uint32

	// bddVarBase is the first BDD variable index used by this field;
	// BDD variables bddVarBase .. bddVarBase+BitWidth-1 belong to this field.
	bddVarBase uint32

	varNodes    []*Node // positive literal per bit
	notVarNodes []*Node // negative literal per bit
}

// Engine owns field declarations and any cross-operation state. One engine
// per process is sufficient for the v1 benchmark; multiple engines in the
// same process share the package-level unique tables, which is fine so long
// as fields are not re-declared after GenerateFields.
type Engine struct {
	mu        sync.Mutex
	fields    []*Field
	finalized bool
	totalBits uint32
}

// NewEngine returns a fresh engine. Field declaration must precede any NDD
// operations.
func NewEngine() *Engine { return &Engine{} }

// DeclareField appends a new field of the given bit width. Returns the field
// handle; IDs are assigned in declaration order starting at 0.
func (e *Engine) DeclareField(bitWidth uint32) *Field {
	if bitWidth == 0 {
		panic("mtpndd: field bit width must be > 0")
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.finalized {
		panic("mtpndd: cannot declare field after GenerateFields")
	}
	f := &Field{
		ID:         uint32(len(e.fields)),
		BitWidth:   bitWidth,
		bddVarBase: e.totalBits,
	}
	e.fields = append(e.fields, f)
	e.totalBits += bitWidth
	return f
}

// GenerateFields finalizes the field list and materializes the positive and
// negative literal NDD nodes for each bit.
func (e *Engine) GenerateFields() {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.finalized {
		return
	}
	for _, f := range e.fields {
		f.varNodes = make([]*Node, f.BitWidth)
		f.notVarNodes = make([]*Node, f.BitWidth)
		for b := uint32(0); b < f.BitWidth; b++ {
			bddVar := f.bddVarBase + b
			// Positive literal: fieldID=f, edges={TRUE -> BDD(var)}.
			f.varNodes[b] = mk(f.ID, []edge{{child: True, label: bdd.IthVar(bddVar)}})
			f.notVarNodes[b] = mk(f.ID, []edge{{child: True, label: bdd.NIthVar(bddVar)}})
		}
	}
	e.finalized = true
}

// Finalized reports whether GenerateFields has been called.
func (e *Engine) Finalized() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.finalized
}

// Field returns the field with the given id.
func (e *Engine) Field(id uint32) *Field {
	if int(id) >= len(e.fields) {
		panic(fmt.Sprintf("mtpndd: field id %d out of range", id))
	}
	return e.fields[id]
}

// NumFields returns the number of declared fields.
func (e *Engine) NumFields() int { return len(e.fields) }

// TotalBits returns the total number of BDD variables across all fields.
func (e *Engine) TotalBits() uint32 { return e.totalBits }

// Var returns the NDD representing "bit `bit` of field `f` is 1".
func (e *Engine) Var(f *Field, bit uint32) *Node {
	if !e.finalized {
		panic("mtpndd: call GenerateFields before Var")
	}
	return f.varNodes[bit]
}

// NotVar returns the NDD representing "bit `bit` of field `f` is 0".
func (e *Engine) NotVar(f *Field, bit uint32) *Node {
	if !e.finalized {
		panic("mtpndd: call GenerateFields before NotVar")
	}
	return f.notVarNodes[bit]
}

// BDDVar returns the raw BDD variable index for bit `bit` of field `f`.
func (e *Engine) BDDVar(f *Field, bit uint32) uint32 {
	return f.bddVarBase + bit
}
