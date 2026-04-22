// nqueens solves the n-queens problem using the mtpndd Go library and prints
// the solution count. Mirrors mtpndd/test/nqueens.c.
package main

import (
	"flag"
	"fmt"
	"math"
	"os"
	"runtime"
	"time"

	mt "github.com/Augists/mtpndd-go"
)

var expected = map[int]uint64{
	1: 1, 2: 0, 3: 0, 4: 2, 5: 10, 6: 4, 7: 40, 8: 92,
	9: 352, 10: 724, 11: 2680, 12: 14200, 13: 73712, 14: 365596,
}

func main() {
	var n int
	flag.IntVar(&n, "n", 8, "board size")
	flag.Parse()
	if flag.NArg() >= 1 {
		fmt.Sscanf(flag.Arg(0), "%d", &n)
	}
	if n <= 0 {
		fmt.Fprintf(os.Stderr, "n must be > 0\n")
		os.Exit(1)
	}

	start := time.Now()
	engine := mt.NewEngine()
	fields := make([]*mt.Field, n)
	for i := range n {
		fields[i] = engine.DeclareField(uint32(n))
	}
	engine.GenerateFields()

	formula := mt.True
	for r := range n {
		row := mt.False
		for c := range n {
			row = mt.Or(row, engine.Var(fields[r], uint32(c)))
		}
		formula = mt.And(formula, row)
	}
	for r := range n {
		for c := range n {
			formula = mt.And(formula, buildImplication(engine, fields, r, c))
		}
	}

	sat := mt.SatCount(formula, engine)
	solutions := uint64(math.Round(sat))
	elapsed := time.Since(start)

	tableSize := mt.TableSize()
	fmt.Printf("n=%d  solutions=%d  time=%.3fs  nodes=%d  workers=%d\n",
		n, solutions, elapsed.Seconds(), tableSize, runtime.GOMAXPROCS(0))

	if want, ok := expected[n]; ok && solutions != want {
		fmt.Fprintf(os.Stderr, "mismatch: got %d, want %d\n", solutions, want)
		os.Exit(1)
	}
}

func buildImplication(e *mt.Engine, fields []*mt.Field, row, col int) *mt.Node {
	n := len(fields)
	accum := mt.True
	guardNeg := e.NotVar(fields[row], uint32(col))
	add := func(other *mt.Node) {
		accum = mt.And(accum, mt.Or(guardNeg, other))
	}
	for c := range n {
		if c != col {
			add(e.NotVar(fields[row], uint32(c)))
		}
	}
	for r := range n {
		if r != row {
			add(e.NotVar(fields[r], uint32(col)))
		}
	}
	for r := range n {
		if r == row {
			continue
		}
		du := r - row + col
		if du >= 0 && du < n {
			add(e.NotVar(fields[r], uint32(du)))
		}
		dd := row + col - r
		if dd >= 0 && dd < n {
			add(e.NotVar(fields[r], uint32(dd)))
		}
	}
	return accum
}
