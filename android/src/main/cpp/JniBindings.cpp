// JNI bindings for the Location Confidence core.
//
// Natives are registered explicitly in JNI_OnLoad instead of relying on
// Java_* symbol name mangling, so the binding does not depend on the
// Kotlin class keeping its exact mangled method names. The class itself
// is kept by consumer-rules.pro.
//
// No geographic logic lives here: this file only converts arguments and
// calls the C facade (lce.h).
#include <jni.h>

#include "location_confidence/lce.h"

namespace {

constexpr const char* kNativeCoreClass = "com/locationconfidence/core/NativeCore";

jint nativeAbiVersion(JNIEnv* /*env*/, jclass /*clazz*/) {
  return static_cast<jint>(lce_abi_version());
}

const JNINativeMethod kMethods[] = {
    {"nativeAbiVersion", "()I", reinterpret_cast<void*>(nativeAbiVersion)},
};

}  // namespace

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  jclass clazz = env->FindClass(kNativeCoreClass);
  if (clazz == nullptr) {
    return JNI_ERR;
  }

  const jint count = static_cast<jint>(sizeof(kMethods) / sizeof(kMethods[0]));
  const jint status = env->RegisterNatives(clazz, kMethods, count);
  env->DeleteLocalRef(clazz);

  return status == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
