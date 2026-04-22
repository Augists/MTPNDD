package main

// #cgo CFLAGS: -I/usr/local/java/jdk-23/include -I/usr/local/java/jdk-23/include/linux -I${SRCDIR}
// #include "jni_helpers.h"
import "C"

import (
	"sync/atomic"
	"unsafe"

	mtpndd "github.com/Augists/mtpndd-go"
	"github.com/Augists/mtpndd-go/internal/bdd"
)

// Java-side bookkeeping. When the Java layer calls declareField, we forward
// to the Engine; the returned field id is cached locally so get(Not)Var can
// pull the pre-materialised literal node from the Engine.
var (
	fields       []*mtpndd.Field
	fieldsMu     atomic.Bool // cheap CAS lock for append
	fieldsFinalized atomic.Bool
)

// -----------------------------------------------------------------------
// Lifecycle.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_initNative
func Java_org_ants_mtpndd_MTPNDDEngine_initNative(
	env *C.JNIEnv, cls C.jclass,
	workers C.jint,
	laceDequeSize C.jlong,
	bddNodeTableSize C.jlong,
	mtpnddNodeTableSize C.jlong,
	operationCacheSize C.jlong,
	quickGrowthThreshold C.jdouble,
	edgeBucketCount C.jlong,
	nodetableBucketCount C.jlong,
	nodeSlabCapacity C.jlong,
	edgeEntrySlabCapacity C.jlong,
	nodetableEntrySlabCapacity C.jlong,
	edgeMapSlabCapacity C.jlong,
) {
	_ = laceDequeSize
	_ = quickGrowthThreshold
	_ = edgeBucketCount
	_ = nodetableBucketCount
	_ = edgeEntrySlabCapacity
	_ = nodetableEntrySlabCapacity
	_ = edgeMapSlabCapacity

	if initialized.Load() {
		return
	}
	cfg := mapInitArgs(workers, operationCacheSize, mtpnddNodeTableSize, bddNodeTableSize, nodeSlabCapacity)
	engine = mtpndd.NewEngineWithConfig(cfg)
	initialized.Store(true)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_quitNative
func Java_org_ants_mtpndd_MTPNDDEngine_quitNative(env *C.JNIEnv, cls C.jclass) {
	if !initialized.Load() {
		return
	}
	mtpndd.Reset()
	fields = fields[:0]
	fieldsFinalized.Store(false)
	initialized.Store(false)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isInitializedNative
func Java_org_ants_mtpndd_MTPNDDEngine_isInitializedNative(env *C.JNIEnv, cls C.jclass) C.jboolean {
	if initialized.Load() {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

// -----------------------------------------------------------------------
// Field declaration.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_declareFieldNative
func Java_org_ants_mtpndd_MTPNDDEngine_declareFieldNative(env *C.JNIEnv, cls C.jclass, bitWidth C.jint) C.jint {
	if engine == nil {
		throwMsg(env, "engine not initialized")
		return -1
	}
	// Simple CAS lock — field declaration is a slow path.
	for !fieldsMu.CompareAndSwap(false, true) {
	}
	f := engine.DeclareField(uint32(bitWidth))
	fields = append(fields, f)
	fieldsMu.Store(false)
	return C.jint(f.ID)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_generateFieldsNative
func Java_org_ants_mtpndd_MTPNDDEngine_generateFieldsNative(env *C.JNIEnv, cls C.jclass) {
	if engine == nil {
		throwMsg(env, "engine not initialized")
		return
	}
	engine.GenerateFields()
	fieldsFinalized.Store(true)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getVarNative
func Java_org_ants_mtpndd_MTPNDDEngine_getVarNative(env *C.JNIEnv, cls C.jclass, fieldID, index C.jint) C.jlong {
	if int(fieldID) >= len(fields) {
		throwMsg(env, "field id out of range")
		return 0
	}
	return nodeToHandle(engine.Var(fields[fieldID], uint32(index)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getNotVarNative
func Java_org_ants_mtpndd_MTPNDDEngine_getNotVarNative(env *C.JNIEnv, cls C.jclass, fieldID, index C.jint) C.jlong {
	if int(fieldID) >= len(fields) {
		throwMsg(env, "field id out of range")
		return 0
	}
	return nodeToHandle(engine.NotVar(fields[fieldID], uint32(index)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getFieldIdNative
func Java_org_ants_mtpndd_MTPNDDEngine_getFieldIdNative(env *C.JNIEnv, cls C.jclass, handle C.jlong) C.jint {
	n := handleToNode(handle)
	return C.jint(n.FieldID())
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getEdgesNative
func Java_org_ants_mtpndd_MTPNDDEngine_getEdgesNative(env *C.JNIEnv, cls C.jclass, handle C.jlong) C.jlongArray {
	// mtpndd-go v1 does not expose edge introspection to Java. Return an
	// empty long[] so callers can detect "no edges" without a null
	// dereference.
	return C.jni_new_long_array(env, 0)
}

// -----------------------------------------------------------------------
// Reference counting — no-ops. mtpndd-go holds every canonical node alive
// until mtpndd.Reset() is called, so pointer validity is independent of
// Java-side ref/deref activity.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_refNative
func Java_org_ants_mtpndd_MTPNDDEngine_refNative(env *C.JNIEnv, cls C.jclass, handle C.jlong) {
	_ = handle
}

//export Java_org_ants_mtpndd_MTPNDDEngine_derefNative
func Java_org_ants_mtpndd_MTPNDDEngine_derefNative(env *C.JNIEnv, cls C.jclass, handle C.jlong) {
	_ = handle
}

// -----------------------------------------------------------------------
// NDD boolean operations.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_andNative
func Java_org_ants_mtpndd_MTPNDDEngine_andNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	return nodeToHandle(mtpndd.And(handleToNode(a), handleToNode(b)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_orNative
func Java_org_ants_mtpndd_MTPNDDEngine_orNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	return nodeToHandle(mtpndd.Or(handleToNode(a), handleToNode(b)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_notNative
func Java_org_ants_mtpndd_MTPNDDEngine_notNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jlong {
	return nodeToHandle(mtpndd.Not(handleToNode(a)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_diffNative
func Java_org_ants_mtpndd_MTPNDDEngine_diffNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	return nodeToHandle(mtpndd.Diff(handleToNode(a), handleToNode(b)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_existNative
func Java_org_ants_mtpndd_MTPNDDEngine_existNative(env *C.JNIEnv, cls C.jclass, a C.jlong, fieldID C.jint) C.jlong {
	return nodeToHandle(mtpndd.Exist(handleToNode(a), uint32(fieldID)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_satCountNative
func Java_org_ants_mtpndd_MTPNDDEngine_satCountNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jdouble {
	return C.jdouble(mtpndd.SatCount(handleToNode(a), engine))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_minZerosNative
func Java_org_ants_mtpndd_MTPNDDEngine_minZerosNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jint {
	// Not implemented in mtpndd-go v1; sre-ndd only calls it for debug
	// output paths. Return 0 as a harmless placeholder.
	_ = a
	return 0
}

// -----------------------------------------------------------------------
// Terminals.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_terminalTrueNative
func Java_org_ants_mtpndd_MTPNDDEngine_terminalTrueNative(env *C.JNIEnv, cls C.jclass) C.jlong {
	return nodeToHandle(mtpndd.True)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_terminalFalseNative
func Java_org_ants_mtpndd_MTPNDDEngine_terminalFalseNative(env *C.JNIEnv, cls C.jclass) C.jlong {
	return nodeToHandle(mtpndd.False)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isTrueNative
func Java_org_ants_mtpndd_MTPNDDEngine_isTrueNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jboolean {
	if handleToNode(a) == mtpndd.True {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isFalseNative
func Java_org_ants_mtpndd_MTPNDDEngine_isFalseNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jboolean {
	if handleToNode(a) == mtpndd.False {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isTerminalNative
func Java_org_ants_mtpndd_MTPNDDEngine_isTerminalNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jboolean {
	if mtpndd.IsTerminal(handleToNode(a)) {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

// -----------------------------------------------------------------------
// Batch ops — tree-reduce for orReduce/andReduce, parallel for andBatch.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_orReduceNative
func Java_org_ants_mtpndd_MTPNDDEngine_orReduceNative(env *C.JNIEnv, cls C.jclass, arr C.jlongArray) C.jlong {
	nodes := readNodeArray(env, arr)
	if len(nodes) == 0 {
		return nodeToHandle(mtpndd.False)
	}
	return nodeToHandle(treeReduce(nodes, mtpndd.Or))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_andReduceNative
func Java_org_ants_mtpndd_MTPNDDEngine_andReduceNative(env *C.JNIEnv, cls C.jclass, arr C.jlongArray) C.jlong {
	nodes := readNodeArray(env, arr)
	if len(nodes) == 0 {
		return nodeToHandle(mtpndd.True)
	}
	return nodeToHandle(treeReduce(nodes, mtpndd.And))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_andBatchNative
func Java_org_ants_mtpndd_MTPNDDEngine_andBatchNative(env *C.JNIEnv, cls C.jclass, lefts, rights C.jlongArray) C.jlongArray {
	l := readNodeArray(env, lefts)
	r := readNodeArray(env, rights)
	if len(l) != len(r) {
		throwMsg(env, "andBatch: lefts and rights must have equal length")
		return C.jni_new_long_array(env, 0)
	}
	out := make([]C.jlong, len(l))
	for i := range l {
		out[i] = nodeToHandle(mtpndd.And(l[i], r[i]))
	}
	return writeLongArray(env, out)
}

func readNodeArray(env *C.JNIEnv, arr C.jlongArray) []*mtpndd.Node {
	n := int(C.jni_get_array_length(env, C.jarray(arr)))
	if n == 0 {
		return nil
	}
	raw := make([]C.jlong, n)
	C.jni_get_long_array_region(env, arr, 0, C.jsize(n), (*C.jlong)(unsafe.Pointer(&raw[0])))
	nodes := make([]*mtpndd.Node, n)
	for i := 0; i < n; i++ {
		nodes[i] = handleToNode(raw[i])
	}
	return nodes
}

func writeLongArray(env *C.JNIEnv, longs []C.jlong) C.jlongArray {
	arr := C.jni_new_long_array(env, C.jsize(len(longs)))
	if len(longs) > 0 {
		C.jni_set_long_array_region(env, arr, 0, C.jsize(len(longs)), (*C.jlong)(unsafe.Pointer(&longs[0])))
	}
	return arr
}

func treeReduce(nodes []*mtpndd.Node, op func(a, b *mtpndd.Node) *mtpndd.Node) *mtpndd.Node {
	for len(nodes) > 1 {
		half := (len(nodes) + 1) / 2
		next := make([]*mtpndd.Node, half)
		for i := 0; i < len(nodes)/2; i++ {
			next[i] = op(nodes[2*i], nodes[2*i+1])
		}
		if len(nodes)%2 == 1 {
			next[len(nodes)/2] = nodes[len(nodes)-1]
		}
		nodes = next
	}
	return nodes[0]
}

// -----------------------------------------------------------------------
// Config knobs that sre-ndd calls directly (mtpndd.twoPhaseThreshold, etc).
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_setSylvanSpawnDepthCutoffNative
func Java_org_ants_mtpndd_MTPNDDEngine_setSylvanSpawnDepthCutoffNative(env *C.JNIEnv, cls C.jclass, depth C.jint) {
	_ = depth // mtpndd-go has no Sylvan; silently accept.
}

//export Java_org_ants_mtpndd_MTPNDDEngine_setTwoPhaseThresholdNative
func Java_org_ants_mtpndd_MTPNDDEngine_setTwoPhaseThresholdNative(env *C.JNIEnv, cls C.jclass, threshold C.jint) {
	_ = threshold // v1 Go does not separate label-filter phase; accept silently.
}

// -----------------------------------------------------------------------
// BDD-layer introspection (used by NDDConfig2spec et al).
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_getBddVarNative
func Java_org_ants_mtpndd_MTPNDDEngine_getBddVarNative(env *C.JNIEnv, cls C.jclass, fieldID, index C.jint) C.jlong {
	if int(fieldID) >= len(fields) {
		throwMsg(env, "field id out of range")
		return 0
	}
	return bddToHandle(bdd.IthVar(engine.BDDVar(fields[fieldID], uint32(index))))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getBddNotVarNative
func Java_org_ants_mtpndd_MTPNDDEngine_getBddNotVarNative(env *C.JNIEnv, cls C.jclass, fieldID, index C.jint) C.jlong {
	if int(fieldID) >= len(fields) {
		throwMsg(env, "field id out of range")
		return 0
	}
	return bddToHandle(bdd.NIthVar(engine.BDDVar(fields[fieldID], uint32(index))))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddTrueNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddTrueNative(env *C.JNIEnv, cls C.jclass) C.jlong {
	return bddToHandle(bdd.True)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddFalseNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddFalseNative(env *C.JNIEnv, cls C.jclass) C.jlong {
	return bddToHandle(bdd.False)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddRefNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddRefNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jlong {
	return h
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddDerefNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddDerefNative(env *C.JNIEnv, cls C.jclass, h C.jlong) {
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddAndNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddAndNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	return bddToHandle(bdd.And(handleToBDD(a), handleToBDD(b)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddOrNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddOrNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	return bddToHandle(bdd.Or(handleToBDD(a), handleToBDD(b)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddNotNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddNotNative(env *C.JNIEnv, cls C.jclass, a C.jlong) C.jlong {
	return bddToHandle(bdd.Not(handleToBDD(a)))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddIsTrueNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddIsTrueNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	if handleToBDD(h) == bdd.True {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddIsFalseNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddIsFalseNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	if handleToBDD(h) == bdd.False {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddVarNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddVarNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jint {
	return C.jint(handleToBDD(h).Var)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddLowNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddLowNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jlong {
	n := handleToBDD(h)
	if n.Low == nil {
		return 0
	}
	return bddToHandle(n.Low)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_bddHighNative
func Java_org_ants_mtpndd_MTPNDDEngine_bddHighNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jlong {
	n := handleToBDD(h)
	if n.High == nil {
		return 0
	}
	return bddToHandle(n.High)
}

//export Java_org_ants_mtpndd_MTPNDDEngine_fromMtbddNative
func Java_org_ants_mtpndd_MTPNDDEngine_fromMtbddNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jlong {
	// mtpndd-go has no distinction between BDD and MTBDD (no MT leaves).
	// Return the handle unchanged.
	return h
}

// -----------------------------------------------------------------------
// Field info + stats — return null for now; sre-ndd only uses these for
// pretty-printing and doesn't crash on null.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_getFieldInfoNative
func Java_org_ants_mtpndd_MTPNDDEngine_getFieldInfoNative(env *C.JNIEnv, cls C.jclass, fieldID C.jint) C.jobject {
	return C.jobject(unsafe.Pointer(nil))
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getStatsNative
func Java_org_ants_mtpndd_MTPNDDEngine_getStatsNative(env *C.JNIEnv, cls C.jclass) C.jobject {
	return C.jni_new_stats(env)
}

// -----------------------------------------------------------------------
// Unsupported surface (MT leaves + Phase-1 arithmetic). v1 Go ships with
// boolean NDDs only; these throw.
// -----------------------------------------------------------------------

//export Java_org_ants_mtpndd_MTPNDDEngine_makeFractionNative
func Java_org_ants_mtpndd_MTPNDDEngine_makeFractionNative(env *C.JNIEnv, cls C.jclass, numer, denom C.jint) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_makeDoubleNative
func Java_org_ants_mtpndd_MTPNDDEngine_makeDoubleNative(env *C.JNIEnv, cls C.jclass, v C.jdouble) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isLeafNative
func Java_org_ants_mtpndd_MTPNDDEngine_isLeafNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	// mtpndd-go has only True/False terminals; non-terminals are the only
	// things that count as non-leaves.
	if mtpndd.IsTerminal(handleToNode(h)) {
		return C.JNI_TRUE
	}
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isFractionLeafNative
func Java_org_ants_mtpndd_MTPNDDEngine_isFractionLeafNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_isDoubleLeafNative
func Java_org_ants_mtpndd_MTPNDDEngine_isDoubleLeafNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	return C.JNI_FALSE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getNumerNative
func Java_org_ants_mtpndd_MTPNDDEngine_getNumerNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jint {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getDenomNative
func Java_org_ants_mtpndd_MTPNDDEngine_getDenomNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jint {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_getDoubleLeafNative
func Java_org_ants_mtpndd_MTPNDDEngine_getDoubleLeafNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jdouble {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_plusNative
func Java_org_ants_mtpndd_MTPNDDEngine_plusNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_minusNative
func Java_org_ants_mtpndd_MTPNDDEngine_minusNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_timesNative
func Java_org_ants_mtpndd_MTPNDDEngine_timesNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_divideNative
func Java_org_ants_mtpndd_MTPNDDEngine_divideNative(env *C.JNIEnv, cls C.jclass, a, b C.jlong) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_leafCountNative
func Java_org_ants_mtpndd_MTPNDDEngine_leafCountNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jlong {
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_abstractPlusNative
func Java_org_ants_mtpndd_MTPNDDEngine_abstractPlusNative(env *C.JNIEnv, cls C.jclass, h C.jlong, fieldID C.jint) C.jlong {
	throwUnsupported(env)
	return 0
}

//export Java_org_ants_mtpndd_MTPNDDEngine_abstractPlusValidateNative
func Java_org_ants_mtpndd_MTPNDDEngine_abstractPlusValidateNative(env *C.JNIEnv, cls C.jclass, h C.jlong) C.jboolean {
	return C.JNI_TRUE
}

//export Java_org_ants_mtpndd_MTPNDDEngine_leafGcNative
func Java_org_ants_mtpndd_MTPNDDEngine_leafGcNative(env *C.JNIEnv, cls C.jclass) C.jlong {
	return 0
}
