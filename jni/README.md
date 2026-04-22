# mtpndd-go JNI bridge

Builds a drop-in replacement `libmtpnddjni.so` that can be loaded by the
same Java jar that mtpndd-c produces (`org.ants.mtpndd.*` classes).
Java-side callers are unchanged; the only difference is that the native
implementation is pure Go instead of C/Sylvan/Lace.

## Handle representation

Each `org.ants.mtpndd.MTPNDD` Java wrapper holds a `long nativePtr`. On
the Go side this is simply `uintptr(unsafe.Pointer(*mtpndd.Node))`. Go's
GC is non-moving and the NDD unique table holds a strong pointer to every
canonical node until `mtpndd.Reset()` is called, so addresses are stable
for the life of a session. No handle table, no per-call lookup, no
indirection.

## Ref counting semantics

mtpndd-c's Java wrapper calls `MTPNDDEngine.ref(node)` / `deref(node)`
to manage native lifetime. mtpndd-go keeps all interned nodes alive until
`Reset()`, so ref/deref are **no-ops** at the JNI layer — we just count
invocations for statistics. Callers get exactly the same observable
behaviour (node pointers remain valid).

## Build

```bash
cd jni
./build.sh
# produces:
#   jni/libmtpnddjni.so
```

Requires `JAVA_HOME` pointing at a JDK (tested with JDK 23). The build
assumes the JNI headers live under `$JAVA_HOME/include` and
`$JAVA_HOME/include/linux`.

To use in sre-ndd:

```bash
cp jni/libmtpnddjni.so ~/sre-ndd/lib/mtpndd/libmtpnddjni.so
# jar from mtpndd-c's feature/c build is reused:
cp ~/mtpndd-c-feature-c/jni/target/mtpndd-java-0.1.0-SNAPSHOT.jar ~/sre-ndd/lib/mtpndd/
```

## Unsupported methods

v1 Go does not provide multi-terminal leaves (fraction / double) or
Phase-1 arithmetic. The corresponding native methods throw
`MTPNDDException("not supported in mtpndd-go v1")`:

- `makeFractionNative`, `makeDoubleNative`
- `isLeafNative`, `isFractionLeafNative`, `isDoubleLeafNative`
- `getNumerNative`, `getDenomNative`, `getDoubleLeafNative`
- `plusNative`, `minusNative`, `timesNative`, `divideNative`
- `leafCountNative`, `abstractPlusNative`, `abstractPlusValidateNative`
- `leafGcNative`

Raw BDD-layer introspection (`bddLowNative`, `bddHighNative`, etc.) is
currently stubbed; will implement if sre-ndd touches that surface.
