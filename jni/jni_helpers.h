#ifndef MTPNDD_JNI_HELPERS_H
#define MTPNDD_JNI_HELPERS_H

#include <jni.h>
#include <stdlib.h>
#include <string.h>

// jni_throw_class looks up cls_name as a JNI binary class name and throws
// the given message. Falls back to java/lang/RuntimeException if the
// requested class can't be found. Returns 0 on success, -1 on failure.
static inline int jni_throw_class(JNIEnv *env, const char *cls_name, const char *msg) {
    jclass cls = (*env)->FindClass(env, cls_name);
    if (cls == NULL) {
        cls = (*env)->FindClass(env, "java/lang/RuntimeException");
        if (cls == NULL) {
            return -1;
        }
    }
    (*env)->ThrowNew(env, cls, msg);
    return 0;
}

static inline jlongArray jni_new_long_array(JNIEnv *env, jsize len) {
    return (*env)->NewLongArray(env, len);
}

static inline void jni_set_long_array_region(JNIEnv *env, jlongArray arr, jsize start, jsize len, const jlong *buf) {
    (*env)->SetLongArrayRegion(env, arr, start, len, buf);
}

static inline void jni_get_long_array_region(JNIEnv *env, jlongArray arr, jsize start, jsize len, jlong *buf) {
    (*env)->GetLongArrayRegion(env, arr, start, len, buf);
}

static inline jsize jni_get_array_length(JNIEnv *env, jarray arr) {
    return (*env)->GetArrayLength(env, arr);
}

// jni_new_stats constructs a zeroed org.ants.mtpndd.MTPNDDStats Java
// object. mtpndd-go v1 does not collect per-op instrumentation; every
// field is returned as 0 so callers that only aggregate see a consistent
// empty snapshot.
static inline jobject jni_new_stats(JNIEnv *env) {
    jclass cls = (*env)->FindClass(env, "org/ants/mtpndd/MTPNDDStats");
    if (cls == NULL) return NULL;
    jmethodID ctor = (*env)->GetMethodID(env, cls, "<init>", "(JZ[J)V");
    if (ctor == NULL) return NULL;
    jlongArray metrics = (*env)->NewLongArray(env, 64); // >= IDX_AND_PENDING_FLUSH_TOTAL+1
    return (*env)->NewObject(env, cls, ctor, (jlong)0, JNI_FALSE, metrics);
}

#endif
