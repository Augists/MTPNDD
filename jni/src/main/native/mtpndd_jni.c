#include <jni.h>
#include <stdio.h>
#include <string.h>

#include "mtpndd.h"
#include "mtpndd_common.h"

static void mtpndd_throw_exception(JNIEnv *env, const char *class_name, const char *message)
{
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

static void mtpndd_throw_error(JNIEnv *env, mtpndd_error_t code)
{
    const char *msg = mtpndd_error_string(code);
    mtpndd_throw_exception(env, "org/ants/mtpndd/MtpnddException", msg);
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MtpnddNative_initNative(JNIEnv *env, jclass clazz,
                                             jint nWorkers,
                                             jlong laceDQSize,
                                             jlong bddSize,
                                             jlong mtpnddSize,
                                             jlong opCacheSize)
{
    (void)clazz;
    mtpndd_pal_config_t config = {0};
    config.n_workers = (int32_t)nWorkers;
    config.lace_dqsize = (size_t)laceDQSize;
    config.bdd_nodetable_size = (size_t)bddSize;
    config.mtpndd_nodetable_size = (size_t)mtpnddSize;
    config.op_cache_size = (size_t)opCacheSize;

    mtpndd_error_t err = mtpndd_init(&config);
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MtpnddNative_configureAdvancedNative(JNIEnv *env, jclass clazz,
                                                          jdouble quickGrowth,
                                                          jlong edgeBuckets,
                                                          jlong nodeBuckets,
                                                          jlong gcBuckets)
{
    (void)env;
    (void)clazz;
    g_mtpndd_pal_config.quick_growth_threshold = quickGrowth;
    g_mtpndd_pal_config.edge_bucket_count = (size_t)edgeBuckets;
    g_mtpndd_pal_config.nodetable_bucket_count = (size_t)nodeBuckets;
    g_mtpndd_pal_config.gc_bucket_count = (size_t)gcBuckets;
}

JNIEXPORT void JNICALL
Java_org_ants_mtpndd_MtpnddNative_quitNative(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    mtpndd_error_t err = mtpndd_quit();
    if (err != MTPNDD_SUCCESS) {
        mtpndd_throw_error(env, err);
    }
}

JNIEXPORT jboolean JNICALL
Java_org_ants_mtpndd_MtpnddNative_isInitializedNative(JNIEnv *env, jclass clazz)
{
    (void)env;
    (void)clazz;
    return mtpndd_is_initialized() ? JNI_TRUE : JNI_FALSE;
}

JNIEXPORT jstring JNICALL
Java_org_ants_mtpndd_MtpnddNative_getLastErrorNative(JNIEnv *env, jclass clazz)
{
    (void)clazz;
    mtpndd_error_info_t info = mtpndd_get_last_error();
    char buffer[256];
    const char *message = info.message ? info.message : "Unknown error";
    const char *function = info.function ? info.function : "?";
    snprintf(buffer, sizeof(buffer), "%s (code=%d, at %s:%d)", message, (int)info.code, function, info.line);
    return (*env)->NewStringUTF(env, buffer);
}
