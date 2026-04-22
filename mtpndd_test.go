package mtpndd

import "testing"

// expectedNQueens[N] is the known count of solutions to the N-queens problem.
var expectedNQueens = map[int]uint64{
	1: 1, 2: 0, 3: 0, 4: 2, 5: 10, 6: 4, 7: 40, 8: 92,
	9: 352, 10: 724, 11: 2680, 12: 14200,
}

func newEngineN(n int) (*Engine, []*Field) {
	e := NewEngine()
	fields := make([]*Field, n)
	for i := 0; i < n; i++ {
		fields[i] = e.DeclareField(uint32(n))
	}
	e.GenerateFields()
	return e, fields
}

func TestBasicIdentities(t *testing.T) {
	e, fields := newEngineN(2)

	x00 := e.Var(fields[0], 0) // row 0, col 0
	x01 := e.Var(fields[0], 1)
	x10 := e.Var(fields[1], 0)

	if And(x00, False) != False {
		t.Fatal("x AND False != False")
	}
	if Or(x00, True) != True {
		t.Fatal("x OR True != True")
	}
	if And(x00, x00) != x00 {
		t.Fatal("x AND x != x")
	}
	if Or(x00, x00) != x00 {
		t.Fatal("x OR x != x")
	}
	if Not(Not(x00)) != x00 {
		t.Fatal("double negation")
	}
	if And(x00, x01) == False {
		// AND of two different cells in the same row should not be False
		// (both could be 1 simultaneously in the raw encoding).
		t.Fatal("x00 AND x01 should not be False")
	}
	if And(x00, x10) == False {
		t.Fatal("x00 AND x10 should not be False (different fields)")
	}
}

// buildRowAtLeastOne builds OR(cell0, cell1, ..., cellN-1) for a row.
func buildRowAtLeastOne(e *Engine, f *Field) *Node {
	accum := False
	for c := uint32(0); c < f.BitWidth; c++ {
		accum = Or(accum, e.Var(f, c))
	}
	return accum
}

// buildCellImplication builds the constraint: if cell (row,col) has a queen,
// then no queen on the same row, same column, or diagonals.
func buildCellImplication(e *Engine, fields []*Field, row, col int) *Node {
	n := len(fields)
	accum := True
	guardNeg := e.NotVar(fields[row], uint32(col))
	addImp := func(other *Node) {
		imp := Or(guardNeg, other)
		accum = And(accum, imp)
	}
	// Same row, other columns.
	for c := 0; c < n; c++ {
		if c == col {
			continue
		}
		addImp(e.NotVar(fields[row], uint32(c)))
	}
	// Same column, other rows.
	for r := 0; r < n; r++ {
		if r == row {
			continue
		}
		addImp(e.NotVar(fields[r], uint32(col)))
	}
	// Diagonals.
	for r := 0; r < n; r++ {
		if r == row {
			continue
		}
		du := r - row + col
		if du >= 0 && du < n {
			addImp(e.NotVar(fields[r], uint32(du)))
		}
		dd := row + col - r
		if dd >= 0 && dd < n {
			addImp(e.NotVar(fields[r], uint32(dd)))
		}
	}
	return accum
}

func solveNQueens(t *testing.T, n int) uint64 {
	t.Helper()
	e, fields := newEngineN(n)
	formula := True
	for r := 0; r < n; r++ {
		formula = And(formula, buildRowAtLeastOne(e, fields[r]))
	}
	for r := 0; r < n; r++ {
		for c := 0; c < n; c++ {
			formula = And(formula, buildCellImplication(e, fields, r, c))
		}
	}
	return uint64(SatCount(formula, e) + 0.5)
}

func TestNQueensSmall(t *testing.T) {
	for _, n := range []int{1, 2, 3, 4, 5, 6, 7, 8} {
		got := solveNQueens(t, n)
		want := expectedNQueens[n]
		if got != want {
			t.Errorf("N=%d: got %d solutions, want %d", n, got, want)
		}
	}
}

func TestNQueensLarger(t *testing.T) {
	if testing.Short() {
		t.Skip("skipping in -short mode")
	}
	for _, n := range []int{9, 10} {
		got := solveNQueens(t, n)
		want := expectedNQueens[n]
		if got != want {
			t.Errorf("N=%d: got %d solutions, want %d", n, got, want)
		}
	}
}
