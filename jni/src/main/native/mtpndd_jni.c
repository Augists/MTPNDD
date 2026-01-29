#include <jni.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "mtpndd.h"
#include "mtpndd_common.h"
#include "mtpndd_node.h"
#include "mtpndd_nodetable.h"
#include "sylvan.h"
#include "sylvan_bdd.h"
#include "sylvan_mtbdd.h"

static void mtpndd_throw_exception(JNIEnv *env, const char *class_name, const char *message) {
    jclass ex_class = (*env)->FindClass(env, class_name);
    if (ex_class == NULL) {
        ex_class = (*env)->FindClass(env, "java/lang/RuntimeException");
        if (ex_class == NULL) {
            (*env)->FatalError(env, "Unable to find exception class");
            return;
        }
    }
    (*env)->ThrowNew(env, ex_class, message ? message : "Unknown error");
}

static void mtpndd_throw_error(JNIEnv *env, mtpndd_error_t code) {
    const char *msg = mtpndd_error_string(code);
    mtpndd_throw_exception(env, "org/ants/mtpndd/MTPNDDException", msg);
}

static void mtpndd_throw_last_error(JNIEnv *env) {
    mtpndd_error_info_t info = mtpndd_get_last_error();
    const char *message = info.message ? info.message : mtpndd_error_string(info.code);
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "%s (code=%d, at %s:%d)",
             message, (int)info.code,
             info.function ? info.function : "?",
             info.line);
    mtpndd_throw_exception(env, "org/ants/mtpndd/MTPNDDException", buffer);
}

static jlong mtpndd_ptr_to_jlong(const void *ptr) {
    return (jlong)(intptr_t)ptr;
}

static void *mtpndd_jlong_to_ptr(jlong value) {
    return (void *)(intptr_t)value;
}

static mtpndd_t *mtpndd_node_from_jlong(jlong value) {
    return (mtpndd_t *)mtpndd_jlong_to_ptr(value);
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_initNative(JNIEnv *env, jclass clazz,
                                             jint nWorkers,
                                             jlong laceDQSize,
                                             jlong bddSize,
                                             jlong mtpnddSize,
                                             jlong opCacheSize,
                                             jdouble quickGrowth,
                                             jlong edgeBuckets,
                                             jlong nodeBuckets,
                                             jlong gcBuckets,
                                             jlong nodeSlabCap,
                                             jlong edgeEntrySlabCap,
                                             jlong nodetableEntrySlabCap,
                                             jlong edgeMapSlabCap)
{
    (void)clazz;
    mtpndd_pal_config_t config = {0};
    config.n_workers = (int32_t)nWorkers;
    config.lace_dqsize = (size_t)laceDQSize;
    config.bdd_nodetable_size = (size_t)bddSize;
    config.mtpndd_nodetable_size = (size_t)mtpnddSize;
    config.op_cache_size = (size_t)opCacheSize;
    if (quickGrowth >= 0.0) {
        config.quick_growth_threshold = quickGrowth;
    }
    if (edgeBuckets > 0) {
        config.edge_bucket_count = (size_t)edgeBuckets;
    }
    if (nodeBuckets > 0) {
        config.nodetable_bucket_count = (size_t)nodeBuckets;
    }
    if (gcBuckets > 0) {
        config.gc_bucket_count = (size_t)gcBuckets;
    }
    if (nodeSlabCap > 0) {
        config.node_slab_capacity = (size_t)nodeSlabCap;
    }
    if (edgeEntrySlabCap > 0) {
        config.edge_entry_slab_capacity = (size_t)edgeEntrySlabCap;
    }
    if (nodetableEntrySlabCap > 0) {
        config.nodetable_entry_slab_capacity = (size_t)nodetableEntrySlabCap;
    }
    if (edgeMapSlabCap > 0) {
        config.edge_map_slab_capacity = (size_t)edgeMapSlabCap;
    }

    mtpndd_error_t err = mtpndd_init(&config);
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_quitNative(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    mtpndd_error_t err = mtpndd_quit();
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_isInitializedNative(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return mtpndd_is_initialized() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jint JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_declareFieldNative(JNIEnv *env, jclass clazz, jint bitWidth)
{
    (void)env;
    (void)clazz;
    mtpndd_error_t err = mtpndd_declare_field((uint32_t)bitWidth);
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
        return 0;
    }
    return (jint)g_mtpndd_config.field_count;
}

JNIEXPORT jobject JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getFieldInfoNative(JNIEnv *env, jclass clazz, jint fieldId)
{
    (void)clazz;
    mtpndd_field_info_t *info = mtpndd_get_field_info((uint32_t)fieldId);
    if (!info) {
        mtpndd_throw_last_error(env);
        return NULL;
    }
    jclass infoClass = (*env)->FindClass(env, "org/ants/mtpndd/MTPNDDFieldInfo");
    if (infoClass == NULL) {
        return NULL;
    }
    jmethodID ctor = (*env)->GetMethodID(env, infoClass, "<init>", "(III)V");
    if (ctor == NULL) {
        return NULL;
    }
    return (*env)->NewObject(env, infoClass, ctor,
            (jint)info->field_id,
            (jint)info->bit_width,
            (jint)info->start_var);
}

static jlong mtpndd_wrap_node(JNIEnv *env, mtpndd_t *node) {
    if (!node) {
        mtpndd_throw_last_error(env);
        return 0;
    }
    return mtpndd_ptr_to_jlong(node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getVarNative(JNIEnv *env, jclass clazz, jint fieldId, jint index)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_get_var((uint32_t)fieldId, (uint32_t)index);
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getNotVarNative(JNIEnv *env, jclass clazz, jint fieldId, jint index)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_get_not_var((uint32_t)fieldId, (uint32_t)index);
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_andNative(JNIEnv *env, jclass clazz, jlong left, jlong right)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_and(mtpndd_node_from_jlong(left), mtpndd_node_from_jlong(right));
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_orNative(JNIEnv *env, jclass clazz, jlong left, jlong right)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_or(mtpndd_node_from_jlong(left), mtpndd_node_from_jlong(right));
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_notNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_not(mtpndd_node_from_jlong(handle));
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_diffNative(JNIEnv *env, jclass clazz, jlong left, jlong right)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_diff(mtpndd_node_from_jlong(left), mtpndd_node_from_jlong(right));
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_refNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)clazz;
    if (handle == 0) {
        mtpndd_throw_exception(env, "org/ants/mtpndd/MTPNDDException", "Cannot ref null handle");
        return;
    }
    mtpndd_error_t err = mtpndd_ref(mtpndd_node_from_jlong(handle));
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_derefNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)clazz;
    if (handle == 0) {
        mtpndd_throw_exception(env, "org/ants/mtpndd/MTPNDDException", "Cannot deref null handle");
        return;
    }
    mtpndd_error_t err = mtpndd_deref(mtpndd_node_from_jlong(handle));
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_existNative(JNIEnv *env, jclass clazz, jlong handle, jint fieldId)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_t *node = mtpndd_exist(mtpndd_node_from_jlong(handle), (uint32_t)fieldId);
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jdouble JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_satCountNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)clazz;
    mtpndd_clear_error();
    double result = mtpndd_satcount(mtpndd_node_from_jlong(handle));
    mtpndd_error_info_t info = mtpndd_get_last_error();
    if (info.code != MTPNDD_SUCCESS) {
        mtpndd_throw_last_error(env);
        return 0.0;
    }
    return result;
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getBddVarNative(JNIEnv *env, jclass clazz, jint fieldId, jint index)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_bdd_t value = mtpndd_get_bdd_var((uint32_t)fieldId, (uint32_t)index);
    mtpndd_error_info_t info = mtpndd_get_last_error();
    if (info.code != MTPNDD_SUCCESS) {
        mtpndd_throw_last_error(env);
        return 0;
    }
    sylvan_ref(value);
    return (jlong)value;
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getBddNotVarNative(JNIEnv *env, jclass clazz, jint fieldId, jint index)
{
    (void)clazz;
    mtpndd_clear_error();
    mtpndd_bdd_t value = mtpndd_get_bdd_not_var((uint32_t)fieldId, (uint32_t)index);
    mtpndd_error_info_t info = mtpndd_get_last_error();
    if (info.code != MTPNDD_SUCCESS) {
        mtpndd_throw_last_error(env);
        return 0;
    }
    sylvan_ref(value);
    return (jlong)value;
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddTrueNative(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    sylvan_ref(sylvan_true);
    return (jlong)sylvan_true;
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddFalseNative(JNIEnv *env, jclass clazz) {
    (void)env;
    (void)clazz;
    sylvan_ref(sylvan_false);
    return (jlong)sylvan_false;
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddRefNative(JNIEnv *env, jclass clazz, jlong handle) {
    (void)env;
    (void)clazz;
    if (handle != 0) {
        sylvan_ref((BDD)handle);
    }
    return handle;
}

JNIEXPORT void JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddDerefNative(JNIEnv *env, jclass clazz, jlong handle) {
    (void)env;
    (void)clazz;
    if (handle != 0) {
        sylvan_deref((BDD)handle);
    }
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddAndNative(JNIEnv *env, jclass clazz, jlong left, jlong right) {
    (void)env;
    (void)clazz;
    BDD result = sylvan_and((BDD)left, (BDD)right);
    sylvan_ref(result);
    return (jlong)result;
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddOrNative(JNIEnv *env, jclass clazz, jlong left, jlong right) {
    (void)env;
    (void)clazz;
    BDD result = sylvan_or((BDD)left, (BDD)right);
    sylvan_ref(result);
    return (jlong)result;
}

JNIEXPORT jlong JNICALL Java_org_ants_mtpndd_MTPNDDEngine_bddNotNative(JNIEnv *env, jclass clazz, jlong value) {
    (void)env;
    (void)clazz;
    BDD result = sylvan_not((BDD)value);
    sylvan_ref(result);
    return (jlong)result;
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_fromMtbddNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)clazz;
    mtpndd_t *node = NULL;
    mtpndd_error_t err = mtbdd_to_mtpndd((mtpndd_bdd_t)handle, &node);
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
        return 0;
    }
    return mtpndd_wrap_node(env, node);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_terminalTrueNative(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return mtpndd_ptr_to_jlong(&MTPNDD_TRUE);
}

JNIEXPORT jlong JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_terminalFalseNative(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return mtpndd_ptr_to_jlong(&MTPNDD_FALSE);
}

JNIEXPORT jboolean JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_isTrueNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)env;
    (void)clazz;
    return mtpndd_is_true(mtpndd_node_from_jlong(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_isFalseNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)env;
    (void)clazz;
    return mtpndd_is_false(mtpndd_node_from_jlong(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jboolean JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_isTerminalNative(JNIEnv *env, jclass clazz, jlong handle)
{
    (void)env;
    (void)clazz;
    return mtpndd_is_terminal(mtpndd_node_from_jlong(handle)) ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jobject JNICALL
Java_org_ants_mtpndd_MTPNDDEngine_getStatsNative(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    mtpndd_stats_t *stats = mtpndd_get_stats();
    if (!stats) {
        return NULL;
    }
    jclass statsClass = (*env)->FindClass(env, "org/ants/mtpndd/MTPNDDStats");
    if (statsClass == NULL) {
        return NULL;
    }
    jmethodID ctor = (*env)->GetMethodID(env, statsClass, "<init>", "(JZ[J)V");
    if (ctor == NULL) {
        return NULL;
    }
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    jlong values[] = {
        (jlong)stats->max_edges_per_node,
        (jlong)stats->bdd_nodes_converted,
        (jlong)stats->cache_lookup_hits,
        (jlong)stats->cache_lookup_misses,
        (jlong)stats->gc_runs,
        (jlong)stats->nodes_created_total,
        (jlong)stats->nodes_reused_total,
        (jlong)stats->nodes_collected_last,
        (jlong)stats->edge_insert_total,
        (jlong)stats->edge_collision_total,
        (jlong)stats->nodetable_collision_total,
        (jlong)stats->edge_lock_spin_total,
        (jlong)stats->edge_lock_wait_time_ns,
        (jlong)stats->gc_pause_time_ns,
        (jlong)stats->edge_entry_total,
        (jlong)stats->bdd_nodes_processed_total,
        (jlong)stats->node_pool_acquire_total,
        (jlong)stats->node_pool_release_total,
        (jlong)stats->node_pool_slab_total,
        (jlong)stats->edge_entry_pool_acquire_total,
        (jlong)stats->edge_entry_pool_release_total,
        (jlong)stats->edge_entry_pool_slab_total,
        (jlong)stats->nodetable_entry_pool_acquire_total,
        (jlong)stats->nodetable_entry_pool_release_total,
        (jlong)stats->nodetable_entry_pool_slab_total,
        (jlong)stats->edge_map_pool_acquire_total,
        (jlong)stats->edge_map_pool_release_total,
        (jlong)stats->edge_map_pool_slab_total
    };
    jsize len = (jsize)(sizeof(values) / sizeof(values[0]));
#else
    jsize len = 0;
#endif
    jlongArray arr = (*env)->NewLongArray(env, len);
    if (arr == NULL) {
        return NULL;
    }
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
    (*env)->SetLongArrayRegion(env, arr, 0, len, values);
#endif
    jobject result = (*env)->NewObject(env, statsClass, ctor,
            (jlong)stats->node_count,
#if MTPNDD_LOG_LEVEL >= MTPNDD_LOG_LEVEL_DEBUG
            JNI_TRUE,
#else
            JNI_FALSE,
#endif
            arr);
    return result;
}
