package bdd

import "testing"

func TestTerminals(t *testing.T) {
	if Not(True) != False || Not(False) != True {
		t.Fatal("terminal negation")
	}
	if And(True, True) != True || And(True, False) != False {
		t.Fatal("AND on terminals")
	}
	if Or(False, False) != False || Or(True, False) != True {
		t.Fatal("OR on terminals")
	}
}

func TestSingleVariable(t *testing.T) {
	x := IthVar(0)
	nx := NIthVar(0)
	if And(x, nx) != False {
		t.Fatal("x AND !x should be False")
	}
	if Or(x, nx) != True {
		t.Fatal("x OR !x should be True")
	}
	if Not(Not(x)) != x {
		t.Fatal("double negation should canonicalize")
	}
}

func TestThreeVarIdentities(t *testing.T) {
	x := IthVar(0)
	y := IthVar(1)
	z := IthVar(2)

	// Distributivity: (x OR y) AND z == (x AND z) OR (y AND z)
	left := And(Or(x, y), z)
	right := Or(And(x, z), And(y, z))
	if left != right {
		t.Fatal("distributivity failed")
	}

	// De Morgan: NOT (x AND y) == (NOT x) OR (NOT y)
	dm1 := Not(And(x, y))
	dm2 := Or(Not(x), Not(y))
	if dm1 != dm2 {
		t.Fatal("de morgan failed")
	}
}

func TestSatCount(t *testing.T) {
	x := IthVar(0)
	y := IthVar(1)

	if got := SatCount(x, 2); got != 2 {
		t.Fatalf("satcount(x) over 2 vars = %v, want 2", got)
	}
	if got := SatCount(And(x, y), 2); got != 1 {
		t.Fatalf("satcount(x AND y) = %v, want 1", got)
	}
	if got := SatCount(Or(x, y), 2); got != 3 {
		t.Fatalf("satcount(x OR y) = %v, want 3", got)
	}
	if got := SatCount(True, 3); got != 8 {
		t.Fatalf("satcount(True) over 3 vars = %v, want 8", got)
	}
	if got := SatCount(False, 3); got != 0 {
		t.Fatalf("satcount(False) = %v, want 0", got)
	}
}

func TestExist(t *testing.T) {
	x := IthVar(0)
	y := IthVar(1)
	f := And(x, y)
	// ∃x. x AND y = y
	if Exist(f, 0) != y {
		t.Fatal("exist x of (x AND y) should be y")
	}
	// ∃y. x AND y = x
	if Exist(f, 1) != x {
		t.Fatal("exist y of (x AND y) should be x")
	}
}
