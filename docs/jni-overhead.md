# JNI overhead: what pprof measures, what it actually costs

Written after a round of benchmarking on sre-ndd fattree08 MF=3 w=4.
The initial read of pprof suggested "60 % of CPU is JNI crossing" —
that turned out to be a misreading. This doc records the correct
picture so future perf work aims at the right target.

## What `cgocallback` is

`runtime.cgocallback` is the Go runtime's entry point for
C-calling-Go transitions. JNI native methods loaded from
`libmtpnddjni.so` are exactly that path: Java → JNI → cgo-generated
C trampoline → `cgocallback` → our exported Go function.

One call sequence:

1. Java invokes the native method; JVM dispatches into
   `libmtpnddjni.so`.
2. cgo's generated C stub `_cgoexp_<hash>_Java_..._andNative`
   calls `crosscall2` → `cgocallback` (Go assembly).
3. `cgocallback` → `cgocallbackg` → `cgocallbackg1` (Go code):
   - Bind the C thread to a `g` (goroutine struct). New `g` on first
     call per thread; reused afterward.
   - Switch from the C stack to the Go stack; save C registers.
   - Update GC state — the thread was "external" (syscall-like) while
     in Java, gets marked "in Go" so write barriers re-enable.
   - Install a defer frame.
4. Actual Go work runs (`mtpndd.And` → `bdd.And` → ...).
5. Return: reverse every one of the above steps.

Measured fixed cost per crossing on modern x86: ~0.3–1 µs.

## How `go tool pprof` attributes samples

`runtime/pprof` uses `SIGPROF` at 100 Hz per thread. Each signal
records the current goroutine stack.

- **flat**: samples where that frame was the top of stack × 10 ms.
- **cum**: samples where that frame appeared anywhere in the stack
  × 10 ms (so it includes the time all its callees spent).

Critical consequence: `cgocallback` sits at the bottom of every Go
stack reachable from Java, so its **cum** always covers every bit
of Go work done on behalf of JNI callers, not just the transition
itself.

## The misread

From the sre-ndd fattree08 MF=3 w=4 profile, 10 s capture,
total samples 11.76 s (117 % on 6 cores):

```
cgocallback    flat 0.13 s   cum 7.13 s   (60 %)
cgocallbackg   flat 0.13 s   cum 7.12 s
cgocallbackg1  flat 0.12 s   cum 6.95 s
_cgoexp_*_orNative                 cum 2.87 s
_cgoexp_*_andNative                cum 1.88 s
_cgoexp_*_notNative                cum 1.13 s
_cgoexp_*_satCountNative           cum 0.74 s
```

Reading "cum 7.13 s = 60 % JNI overhead" is wrong. The 7.13 s is
**everything Go did inside Java's call**, including `mtpndd.And`,
`bdd.cacheGet` (which alone is 2.12 s flat), `bdd.intern`, the slab
allocator, etc. All of it is nested under `cgocallback`.

## The actual crossing cost

Flat time in the three transition frames is the real "this sample
caught the CPU executing cgocallback machinery itself":

```
0.13 + 0.13 + 0.12 = 0.38 s ≈ 3.2 % of 11.76 s
```

Add ~1–2 percent for hidden costs attributed elsewhere (GC write
barrier re-enable, cgo-escape book-keeping): **true transition
overhead ≤ 5 %**.

Per-call cost estimate: fattree08 MF=3 issues roughly 3.5 M JNI calls
per run (≈ 1 M satCount + 1.5 M And + 0.7 M Or + ... based on Java
operation counters). At 0.5 µs/call that is 1.75 s on a 15 s run, or
**10–12 % wall-time**. Consistent with the pprof flat number when
you allow for GC/stack-switch costs that don't sample cleanly into
`cgocallback.flat`.

## Why it still looks huge in `cum %`

60 % of samples sitting under `cgocallback` just means 60 % of wall
time is spent **inside Go code invoked by Java**. The remaining 40 %
is Java + JVM:

```
runtime._ExternalCode  cum 4.04 s   # pure Java/JVM execution
[libjvm.so]            flat 1.26 s  # JVM internals
<unknown>              flat 1.81 s  # JIT code, unresolved frames
```

`_ExternalCode` is Go's pprof way of saying "the sample was taken
while this thread was executing non-Go code" — usually JVM interpreter
or JIT output.

## Existing batch APIs on the Go side

`jni/exports.go` already exports three batch operations:

- **`andReduceNative(long[] arr) → long`** — tree-reduce with `And`,
  corresponds to Java `MTPNDD.andReduce(MTPNDD[])`.
- **`orReduceNative(long[] arr) → long`** — same with `Or`.
- **`andBatchNative(long[] lefts, long[] rights) → long[]`** —
  pair-wise `And(lefts[i], rights[i])`.

Each collapses up to N per-call crossings into 1 per-batch crossing.
Implementation in `jni/exports.go:237–308`.

## Usage on the sre-ndd side

As of 2026-04-23, sre-ndd uses the batch APIs in exactly **two**
places (`util/NDDUtil.java:41` and `:77`, both `andReduce`/
`orReduce`). Most call sites are still per-op accumulators, e.g.:

```java
// NDDACLWrapper.java:568–598
for (...) { result = MTPNDD.or(result, one); }   // N crossings

// NDDManager.java:534, 565–567
MTPNDD left  = MTPNDD.ref(MTPNDD.and(notVar, low));
MTPNDD right = MTPNDD.ref(MTPNDD.and(var, high));
MTPNDD result = MTPNDD.ref(MTPNDD.or(left, right));
```

The first pattern converts directly to a single
`MTPNDD.orReduce(list.toArray())`. The second (fixed-size fan-in)
doesn't benefit — 2 crossings become 1 at best, hardly worth the
array allocation.

`andBatchNative` appears unused from Java side — no current call
site collects independent (lhs, rhs) pairs into two arrays.

## Projected win from aggressive batching

Assume we convert all accumulator loops in the four wrapper classes
to `*Reduce` calls. Optimistic estimate: halve the call count, from
3.5 M to ~1.7 M. Save ~0.9 s of crossing at 0.5 µs/call, on a 15 s
run — **~6 % wall-time**. Real gain likely 3–8 % after accounting
for new per-batch array marshalling overhead (readNodeArray does one
`GetLongArrayRegion` + one `make + loop` per call).

It's a modest win and the blast radius is sre-ndd Java code, not
mtpndd-go. Worth doing if we're chasing SRE numbers, not worth
doing for n-queens or any Go-native caller (they don't cross JNI).

## Takeaways for future perf work

1. **Ignore the `cum 60 %`**. The pprof shape means "60 % of time
   is spent running our Go code from Java" — it doesn't say how much
   is transition vs Go work.
2. **Aim at flat time** inside the actual Go hot functions
   (`cacheGet`, `intern`, `mk`) — that's where the µs savings live.
3. **JNI crossing is real but ≤ 12 % wall-time on the worst workload
   measured.** Batching can recover maybe half of it with Java-side
   changes. The bigger wins so far have come from algorithmic fixes
   inside Go (SatCount memoisation 10×, spawn-threshold 2×,
   op-cache wipe removal 2.5 %).
