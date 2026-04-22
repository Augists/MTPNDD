// Package work provides a minimal spawn/sync primitive on top of goroutines.
//
// A call to Go may run fn in a new goroutine (fast path) or inline on the
// caller's goroutine (backpressure path) depending on whether a semaphore
// slot is available. The returned Future.Wait blocks until fn has finished.
//
// Callers are expected to check SpawnWorthwhile before deciding whether to
// parallelize a recursive subproblem; inline execution of Go is still correct
// but wastes a Future allocation.
package work

import (
	"runtime"
	"sync"
)

var (
	maxInflight = runtime.GOMAXPROCS(0) * 2
	sem         = make(chan struct{}, maxInflight)
)

// Future represents an in-flight Go call. Wait returns once fn has finished.
type Future struct {
	done chan struct{}
}

var inlineFuture = &Future{}

var futurePool = sync.Pool{
	New: func() any { return &Future{done: make(chan struct{}, 1)} },
}

// Go runs fn asynchronously if a worker slot is free; otherwise runs fn
// synchronously and returns a completed Future. Wait must be called exactly
// once on the returned Future.
func Go(fn func()) *Future {
	select {
	case sem <- struct{}{}:
	default:
		fn()
		return inlineFuture
	}
	f := futurePool.Get().(*Future)
	go func() {
		defer func() {
			<-sem
			f.done <- struct{}{}
		}()
		fn()
	}()
	return f
}

// Wait blocks until the Future's fn has finished.
func (f *Future) Wait() {
	if f == inlineFuture {
		return
	}
	<-f.done
	futurePool.Put(f)
}

// Workers returns the current GOMAXPROCS setting. Callers use it to gate
// spawning decisions.
func Workers() int { return runtime.GOMAXPROCS(0) }
