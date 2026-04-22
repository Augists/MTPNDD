// nqueens-bench runs the Go mtpndd nqueens solver across a range of board
// sizes and prints one row per size with time, solution count, and peak node
// count. Output format is designed to be diffable against the C version's
// mtpndd_nqueens_benchmark.
package main

import (
	"flag"
	"fmt"
	"math"
	"os"
	"runtime"
	"runtime/pprof"
	"strconv"
	"strings"
	"time"

	mt "github.com/Augists/mtpndd-go"
)

var expected = map[int]uint64{
	1: 1, 2: 0, 3: 0, 4: 2, 5: 10, 6: 4, 7: 40, 8: 92,
	9: 352, 10: 724, 11: 2680, 12: 14200, 13: 73712, 14: 365596,
}

func main() {
	var sizes string
	var workers int
	var cpuProfile string
	flag.StringVar(&sizes, "sizes", "4,6,8,10,12", "comma-separated N values")
	flag.IntVar(&workers, "workers", runtime.GOMAXPROCS(0), "GOMAXPROCS override")
	flag.StringVar(&cpuProfile, "cpuprofile", "", "write cpu profile to this path")
	flag.Parse()

	runtime.GOMAXPROCS(workers)
	if cpuProfile != "" {
		f, err := os.Create(cpuProfile)
		if err != nil {
			fmt.Fprintf(os.Stderr, "cpuprofile: %v\n", err)
			os.Exit(1)
		}
		pprof.StartCPUProfile(f)
		defer pprof.StopCPUProfile()
	}

	fmt.Printf("# workers=%d\n", runtime.GOMAXPROCS(0))
	fmt.Printf("%-3s %-10s %-10s %-10s %-10s\n", "N", "solutions", "expected", "time_s", "nodes")
	for _, s := range strings.Split(sizes, ",") {
		n, err := strconv.Atoi(strings.TrimSpace(s))
		if err != nil || n <= 0 {
			fmt.Fprintf(os.Stderr, "bad size %q: %v\n", s, err)
			os.Exit(1)
		}
		run(n)
	}
}

func run(n int) {
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

	want := "-"
	status := ""
	if w, ok := expected[n]; ok {
		want = strconv.FormatUint(w, 10)
		if w != solutions {
			status = " MISMATCH"
		}
	}
	fmt.Printf("%-3d %-10d %-10s %-10.3f %-10d%s\n",
		n, solutions, want, elapsed.Seconds(), mt.TableSize(), status)
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
