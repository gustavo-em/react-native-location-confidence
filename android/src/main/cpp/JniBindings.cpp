// The bridge between Kotlin and the C++ engine on Android.
//
// Kotlin runs inside the Java Virtual Machine (JVM) and cannot call C++
// directly. JNI ("Java Native Interface") is the mechanism that lets it.
// This file is the only place where JNI appears.
//
// How the connection is made:
//
//   1. Kotlin (NativeCore.kt) declares a function with the word `external`,
//      meaning "the body of this function is not in Kotlin".
//   2. Kotlin calls System.loadLibrary("location-confidence"). Android loads
//      the compiled library (the .so file) that contains this code.
//   3. Right after loading it, the JVM calls JNI_OnLoad (at the bottom of
//      this file) by itself.
//   4. JNI_OnLoad hands the JVM a table saying: "the Kotlin function with
//      this name is implemented by this C++ function".
//   5. From then on, calling the Kotlin function runs the C++ function.
//
// The table matches Kotlin and C++ by TEXT (class name, function name and
// signature). Nothing checks these texts when the app is built. If they do
// not match, the error only appears when the app runs. That is also why
// android/consumer-rules.pro forbids the app's code shrinker (R8) from
// renaming the Kotlin class.
//
// No business rule lives here: each function only passes values along to
// the engine's front door (lce.h).
#include <jni.h>

#include "location_confidence/lce.h"

namespace {

// Full name of the Kotlin class, with "/" where Kotlin writes ".".
// Must match:  package com.locationconfidence.core  +  object NativeCore
constexpr const char* KOTLIN_CLASS_NAME = "com/locationconfidence/core/NativeCore";

// C++ side of Kotlin's `external fun nativeAbiVersion(): Int`.
//
// Every JNI function receives two extra parameters it may not need:
//   env   - the toolbox for talking to the JVM
//   clazz - the Kotlin class the function belongs to
jint nativeAbiVersion(JNIEnv* /*env*/, jclass /*clazz*/) {
  return static_cast<jint>(lce_abi_version());
}

// The table handed to the JVM. One row per `external` function in Kotlin:
//   { Kotlin function name, signature, C++ function }
//
// The signature is JNI's short way to write parameter and return types:
//   "()I" = takes nothing "()" and returns an Int "I".
const JNINativeMethod NATIVE_METHODS[] = {
    {"nativeAbiVersion", "()I", reinterpret_cast<void*>(nativeAbiVersion)},
};

}  // namespace

// Called automatically by the JVM right after the library is loaded.
// It must return the JNI version it needs, or JNI_ERR to say "loading failed"
// (Kotlin then sees an UnsatisfiedLinkError, which NativeCore.kt handles).
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* /*reserved*/) {
  // Get the toolbox for talking to the JVM.
  JNIEnv* env = nullptr;
  if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
    return JNI_ERR;
  }

  // Find the Kotlin class by its name.
  jclass kotlinClass = env->FindClass(KOTLIN_CLASS_NAME);
  if (kotlinClass == nullptr) {
    return JNI_ERR;
  }

  // Hand over the table. The JVM checks that every row matches an `external`
  // function of that class and fails if one does not.
  const jint numberOfMethods =
      static_cast<jint>(sizeof(NATIVE_METHODS) / sizeof(NATIVE_METHODS[0]));
  const jint registrationResult =
      env->RegisterNatives(kotlinClass, NATIVE_METHODS, numberOfMethods);

  // We no longer need our reference to the class; give it back to the JVM.
  env->DeleteLocalRef(kotlinClass);

  return registrationResult == JNI_OK ? JNI_VERSION_1_6 : JNI_ERR;
}
