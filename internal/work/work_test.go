package work

import (
	"sync/atomic"
	"testing"
)

func TestGoRuns(t *testing.T) {
	var n atomic.Int32
	f := Go(func() { n.Add(1) })
	f.Wait()
	if n.Load() != 1 {
		t.Fatalf("want 1, got %d", n.Load())
	}
}

func TestManyGo(t *testing.T) {
	const N = 10000
	var n atomic.Int32
	futures := make([]*Future, N)
	for i := range futures {
		futures[i] = Go(func() { n.Add(1) })
	}
	for _, f := range futures {
		f.Wait()
	}
	if n.Load() != N {
		t.Fatalf("want %d, got %d", N, n.Load())
	}
}
