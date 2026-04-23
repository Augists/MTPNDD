// Package main is the cgo c-shared entry point for libmtpnddjni.so — a
// drop-in native-library replacement that the org.ants.mtpndd Java
// wrapper (as shipped by mtpndd-c) can dlopen. All JNI native methods
// declared on MTPNDDEngine are implemented here, backed by mtpndd-go.
//
// See ./README.md for the overall design (handle scheme, no-op ref/deref,
// unsupported methods).
package main

// #cgo CFLAGS: -I/usr/local/java/jdk-23/include -I/usr/local/java/jdk-23/include/linux -I${SRCDIR}
// #include "jni_helpers.h"
import "C"

import (
	"net/http"
	_ "net/http/pprof"
	"os"
	"runtime"
	"sync/atomic"
	"unsafe"

	mtpndd "github.com/Augists/mtpndd-go"
	"github.com/Augists/mtpndd-go/internal/bdd"
)

// main is required for c-shared mode but must never run.
func main() {}

// If PPROF_PORT is set, expose the runtime profiling endpoints on that
// TCP port so external tools can curl http://127.0.0.1:$PPROF_PORT/debug/
// pprof/profile while the JNI is running.
func init() {
	if port := os.Getenv("PPROF_PORT"); port != "" {
		go func() {
			_ = http.ListenAndServe("127.0.0.1:"+port, nil)
		}()
	}
}

// ---------------------------------------------------------------------------
// Handle conversion.
// ---------------------------------------------------------------------------
//
// Go's GC is non-moving and the NDD/BDD unique tables hold strong pointers
// to every canonical node until Reset(), so a jlong = uintptr(*Node) is a
// stable handle for the life of a session. No handle table, no indirection.

func nodeToHandle(n *mtpndd.Node) C.jlong {
	return C.jlong(uintptr(unsafe.Pointer(n)))
}

func handleToNode(h C.jlong) *mtpndd.Node {
	return (*mtpndd.Node)(unsafe.Pointer(uintptr(h)))
}

func bddToHandle(n *bdd.Node) C.jlong {
	return C.jlong(uintptr(unsafe.Pointer(n)))
}

func handleToBDD(h C.jlong) *bdd.Node {
	return (*bdd.Node)(unsafe.Pointer(uintptr(h)))
}

// ---------------------------------------------------------------------------
// Shared engine state. mtpndd-go's Config is process-global (first Init
// wins), so a single Engine suffices for all JNI callers.
// ---------------------------------------------------------------------------

var (
	initialized atomic.Bool
	engine      *mtpndd.Engine

	exceptionClassName = C.CString("org/ants/mtpndd/MTPNDDException")
	unsupportedMsg     = C.CString("not supported in mtpndd-go v1")
)

func throwUnsupported(env *C.JNIEnv) {
	C.jni_throw_class(env, exceptionClassName, unsupportedMsg)
}

func throwMsg(env *C.JNIEnv, msg string) {
	cs := C.CString(msg)
	defer C.free(unsafe.Pointer(cs))
	C.jni_throw_class(env, exceptionClassName, cs)
}

// ---------------------------------------------------------------------------
// init / shutdown helpers
// ---------------------------------------------------------------------------

// mapInitArgs translates the 12-parameter JNI init signature into an
// mtpndd.Config. Several of the parameters (Lace deque, edge bucket count,
// quick growth threshold) don't have mtpndd-go equivalents and are ignored.
func mapInitArgs(
	workers C.jint,
	opCache C.jlong,
	mtpnddTable C.jlong,
	bddTable C.jlong,
	nodeSlab C.jlong,
) mtpndd.Config {
	cfg := mtpndd.DefaultConfig()
	if workers > 0 {
		runtime.GOMAXPROCS(int(workers))
	}
	if v := roundUpPow2(int64(opCache)); v >= 1024 {
		cfg.NDD.OpCacheSize = int(v)
		cfg.BDD.OpCacheSize = int(v)
		cfg.NDD.CacheClearInterval = uint64(v) * 4
		cfg.BDD.CacheClearInterval = uint64(v) * 4
	}
	if v := roundUpPow2(int64(mtpnddTable)); v >= 256 {
		cfg.NDD.InitialShardCap = capForShard(int(v), cfg.NDD.ShardCount)
	}
	if v := roundUpPow2(int64(bddTable)); v >= 256 {
		cfg.BDD.InitialShardCap = capForShard(int(v), cfg.BDD.ShardCount)
	}
	// Intentionally ignore nodeSlabCapacity. In mtpndd-c it's the
	// *batch* size for per-worker refill (small — e.g. 2048); in
	// mtpndd-go SlabChunkSize is the *bulk* allocation unit
	// (default 2^18). Applying mtpndd-c's 2048 here shrinks chunks
	// by 128× and makes the chunk directory overflow at much smaller
	// workloads than intended.
	_ = nodeSlab

	// Disable in-Go goroutine spawning for JNI-driven workloads.
	// JNI callers are already parallel at the Java thread level
	// (sre-ndd runs N Java threads each making independent JNI
	// calls); spawning inside each call then over-subscribes the
	// scheduler and wastes CPU on goroutine lifecycle. Profiling
	// showed 25+ % of CPU in goroutine machinery at w=4 for sre-ndd
	// fattree08 MF=3. Switching to serial-in-Go dropped that
	// workload from 28.9 s to 15.8 s wall.
	cfg.SpawnPairThreshold = 1024
	return cfg
}

func roundUpPow2(v int64) int64 {
	if v <= 0 {
		return 0
	}
	r := int64(1)
	for r < v {
		r <<= 1
	}
	return r
}

// capForShard picks a power-of-two InitialShardCap so that
// shardCount * cap ≥ total, but never less than the compiled-in default.
func capForShard(total, shardCount int) int {
	per := total / shardCount
	if per < 256 {
		return 256
	}
	// Round up to next power of two.
	r := 256
	for r < per {
		r <<= 1
	}
	return r
}
