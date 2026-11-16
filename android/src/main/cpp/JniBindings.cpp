// The bridge between Kotlin and the C++ engine on Android.
//
// Kotlin runs inside the Java Virtual Machine (JVM) and cannot call C++
// directly. JNI ("Java Native Interface") is the mechanism that lets it.
// This file is the only place where JNI appears.
//
// =============================================================================
// HOW THE CONNECTION IS MADE
// =============================================================================
//
//   1. Kotlin (NativeCore.kt) declares functions with the word `external`,
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
// =============================================================================
// HOW THE DATA CROSSES
// =============================================================================
//
// The engine's front door (lce.h) works with C structs. Kotlin has no C
// structs, so every struct crosses as a LIST OF NUMBERS (a DoubleArray):
//
//     Kotlin                         this file                    engine
//     EvaluationSettings   --->   [0.0, 0.0, 100.0, ...]   --->   lce_evaluation_settings
//     Report               <---   [1.0, 0.0, 0.0, ...]     <---   lce_report
//
// The two sides agree on the ORDER of the numbers, and nothing else. This
// file reads and writes them one after the other with NumberReader and
// NumberWriter; CoreEngine.kt does the same, in the same order. Every list
// that comes out of here ends with one fixed number (LAYOUT_CHECK), so that
// Kotlin notices at once if the two sides ever get out of step.
//
// Why numbers and not Kotlin objects? Building a Kotlin object from C++ needs
// the names of its class and of each field, as text, and R8 renames those in
// release builds. A list of numbers has no names to break.
//
// A `double` holds whole numbers exactly up to 2^53, which is far more than
// any id, time or count used here. The reasons of a report are 64 yes/no
// bits; they cross as two numbers of 32 bits each.
//
// No business rule lives here: each function only passes values along to
// the engine's front door (lce.h).
#include <jni.h>

#include <array>
#include <cstddef>
#include <cstdint>

#include "location_confidence/lce.h"

namespace {

// Full name of the Kotlin class, with "/" where Kotlin writes ".".
// Must match:  package com.locationconfidence.core  +  object NativeCore
constexpr const char* KOTLIN_CLASS_NAME = "com/locationconfidence/core/NativeCore";

// The last number of every list this file hands to Kotlin. CoreEngine.kt
// checks it after reading a list. Must be the same number on both sides.
constexpr double LAYOUT_CHECK = 7741.0;

// How many numbers each struct takes when it goes in. CoreEngine.kt writes
// exactly this many, in the order the read functions below use.
constexpr jsize NUMBERS_IN_DEVICE_STATE = 6;
constexpr jsize NUMBERS_IN_SESSION_SETTINGS = 3 + NUMBERS_IN_DEVICE_STATE;
constexpr jsize NUMBERS_IN_EVALUATION_SETTINGS = 13 + NUMBERS_IN_DEVICE_STATE;
constexpr jsize NUMBERS_IN_ONE_READING = 16;

// --- Reading and writing lists of numbers ----------------------------------------
// Everything here lives on the stack, in arrays of a fixed size. This file
// never asks the system for memory, so it has no way to fail for lack of it.

// The longest list of numbers that crosses in one piece (a report has about
// 50 numbers).
constexpr std::size_t MOST_NUMBERS_IN_ONE_LIST = 64;

// A long delivery of readings is copied and handed over this many at a time.
constexpr jint READINGS_COPIED_AT_A_TIME = 16;

// Reads numbers one after the other from a list that came from Kotlin.
class NumberReader {
 public:
  NumberReader(const double* numbers, std::size_t howMany)
      : numbers_(numbers), howMany_(howMany) {}

  double number() {
    const bool thereIsOne = (position_ < howMany_);
    const double value = thereIsOne ? numbers_[position_] : 0.0;
    position_ += 1;
    return value;
  }
  int32_t whole() { return static_cast<int32_t>(number()); }
  int64_t wholeLong() { return static_cast<int64_t>(number()); }
  int32_t yesOrNo() { return number() != 0.0 ? 1 : 0; }

 private:
  const double* numbers_;
  std::size_t howMany_;
  std::size_t position_ = 0;
};

// Collects numbers one after the other for a list that goes to Kotlin.
class NumberWriter {
 public:
  void number(double value) {
    const bool thereIsRoom = (howMany_ < MOST_NUMBERS_IN_ONE_LIST);
    if (thereIsRoom) {
      numbers_[howMany_] = value;
      howMany_ += 1;
    }
  }
  void whole(int64_t value) { number(static_cast<double>(value)); }

  // 64 yes/no bits as two numbers: the lower 32 bits, then the upper 32.
  void bits64(uint64_t bits) {
    whole(static_cast<int64_t>(bits & 0xFFFFFFFFu));
    whole(static_cast<int64_t>(bits >> 32));
  }

  // Turns what was collected into a Kotlin DoubleArray, ending with the
  // check number. Returns null when the JVM has no memory for it.
  jdoubleArray toKotlin(JNIEnv* env) {
    number(LAYOUT_CHECK);
    const jsize howMany = static_cast<jsize>(howMany_);
    jdoubleArray array = env->NewDoubleArray(howMany);
    if (array == nullptr) {
      return nullptr;  // the JVM already raised an OutOfMemoryError
    }
    env->SetDoubleArrayRegion(array, 0, howMany, numbers_.data());
    return array;
  }

 private:
  std::array<double, MOST_NUMBERS_IN_ONE_LIST> numbers_{};
  std::size_t howMany_ = 0;
};

// Copies a Kotlin DoubleArray of exactly `expectedSize` numbers into
// `destination`. Returns false when the array is null or has another size.
bool copyFromKotlin(JNIEnv* env, jdoubleArray array, jsize expectedSize, double* destination) {
  if (array == nullptr) {
    return false;
  }
  const jsize size = env->GetArrayLength(array);
  if (size != expectedSize) {
    return false;
  }
  // "Region" copies the numbers out, so we never hold a pointer into memory
  // that belongs to the JVM.
  env->GetDoubleArrayRegion(array, 0, size, destination);
  return true;
}

// The engine is kept by Kotlin as a plain number (a Long) that is really the
// address of the engine in memory. These two turn one into the other.
lce_engine* toEngine(jlong handle) {
  return reinterpret_cast<lce_engine*>(static_cast<intptr_t>(handle));
}
jlong toHandle(lce_engine* engine) {
  return static_cast<jlong>(reinterpret_cast<intptr_t>(engine));
}

// --- The structs that go in, read in the order Kotlin wrote them -------------------

lce_device_state readDeviceState(NumberReader& reader) {
  lce_device_state device{};
  device.location_permission = reader.whole();
  device.permission_precision = reader.whole();
  device.location_is_turned_on = reader.yesOrNo();
  device.location_provider = reader.whole();
  device.app_is_in_foreground = reader.yesOrNo();
  device.phone_activity_support = reader.whole();
  return device;
}

lce_session_settings readSessionSettings(NumberReader& reader) {
  lce_session_settings settings{};
  settings.keep_collecting_readings = reader.yesOrNo();
  settings.time_between_readings_ms = reader.wholeLong();
  settings.longest_collecting_time_ms = reader.wholeLong();
  settings.device = readDeviceState(reader);
  return settings;
}

lce_evaluation_settings readEvaluationSettings(NumberReader& reader) {
  lce_evaluation_settings settings{};
  settings.destination_latitude = reader.number();
  settings.destination_longitude = reader.number();
  settings.radius_meters = reader.number();
  settings.newest_age_that_counts_as_fresh_ms = reader.wholeLong();
  settings.oldest_age_allowed_ms = reader.wholeLong();
  settings.worst_gps_error_accepted_meters = reader.number();
  settings.fastest_possible_speed_mps = reader.number();
  settings.use_phone_activity = reader.yesOrNo();
  settings.allow_answer_from_history = reader.yesOrNo();
  settings.mock_policy = reader.whole();
  settings.total_time_allowed_ms = reader.wholeLong();
  settings.first_attempt_time_ms = reader.wholeLong();
  settings.extra_time_ms = reader.wholeLong();
  settings.device = readDeviceState(reader);
  return settings;
}

lce_reading readOneReading(NumberReader& reader) {
  lce_reading reading{};
  reading.latitude = reader.number();
  reading.longitude = reader.number();
  reading.has_accuracy = reader.yesOrNo();
  reading.accuracy_meters = reader.number();
  reading.captured_at_ms = reader.wholeLong();
  reading.time_is_reliable = reader.yesOrNo();
  reading.capture_time_was_estimated = reader.yesOrNo();
  reading.only_approximate_permission = reader.yesOrNo();
  reading.has_speed = reader.yesOrNo();
  reading.speed_mps = reader.number();
  reading.has_speed_accuracy = reader.yesOrNo();
  reading.speed_accuracy_mps = reader.number();
  reading.has_heading = reader.yesOrNo();
  reading.heading_degrees = reader.number();
  reading.provider = reader.whole();
  reading.mock_signal = reader.whole();
  return reading;
}

// --- The structs that come out, written in the order Kotlin reads them ---------------

void writeReport(const lce_report& report, NumberWriter& writer) {
  writer.whole(report.request_id);
  writer.whole(report.session_id);
  writer.whole(report.status);
  writer.whole(report.source);

  writer.whole(report.has_position);
  writer.number(report.latitude);
  writer.number(report.longitude);
  writer.number(report.distance_to_destination_meters);
  writer.number(report.gps_error_meters);
  writer.number(report.how_far_phone_could_be_meters);
  writer.whole(report.reading_age_ms);
  writer.whole(report.mock_signal);

  writer.number(report.radius_meters);
  writer.whole(report.age_reliability);
  writer.whole(report.permission_precision);
  writer.whole(report.relation);

  writer.whole(report.quality_score);
  writer.whole(report.has_score_breakdown);
  writer.number(report.accuracy_factor);
  writer.number(report.freshness_factor);
  writer.number(report.movement_factor);
  writer.number(report.consistency_factor);
  writer.whole(report.raw_score);
  writer.whole(report.limits_applied);

  writer.whole(report.activity_now);
  writer.whole(report.activity_confidence_now);
  writer.whole(report.standing_still_since_the_reading);
  writer.whole(report.activity_known_until_now);
  writer.whole(report.activity_known_since_age_ms);
  writer.whole(report.activity_has_gaps);

  writer.bits64(report.reasons);

  writer.whole(report.how_many_steps);
  for (int position = 0; position < LCE_MAX_STEPS_IN_REPORT; ++position) {
    writer.whole(report.steps[position]);
  }

  writer.whole(report.first_attempt_timed_out);
  writer.whole(report.extra_time_was_used);
  writer.whole(report.fallback_cause);

  writer.whole(report.readings_received);
  writer.whole(report.readings_kept);
  writer.whole(report.readings_examined);
  writer.whole(report.gps_jumps_found);
  writer.whole(report.duration_ms);
}

// =============================================================================
// THE C++ SIDE OF EACH `external` FUNCTION OF NativeCore.kt
//
// Every JNI function receives two extra parameters it may not need:
//   env   - the toolbox for talking to the JVM
//   clazz - the Kotlin class the function belongs to
// =============================================================================

jint nativeAbiVersion(JNIEnv* /*env*/, jclass /*clazz*/) {
  return static_cast<jint>(lce_abi_version());
}

jlong nativeCreateEngine(JNIEnv* /*env*/, jclass /*clazz*/) {
  return toHandle(lce_engine_create());  // 0 when there is no memory
}

void nativeDestroyEngine(JNIEnv* /*env*/, jclass /*clazz*/, jlong engine) {
  lce_engine_destroy(toEngine(engine));
}

// Returns [session id, collecting state, reasons (2 numbers), check], or null
// when the list of settings is not what we expected.
jdoubleArray nativeOpenSession(JNIEnv* env,
                               jclass /*clazz*/,
                               jlong engine,
                               jdoubleArray settingsNumbers,
                               jlong nowMs) {
  std::array<double, NUMBERS_IN_SESSION_SETTINGS> numbers{};
  if (!copyFromKotlin(env, settingsNumbers, NUMBERS_IN_SESSION_SETTINGS, numbers.data())) {
    return nullptr;
  }
  NumberReader reader(numbers.data(), numbers.size());
  const lce_session_settings settings = readSessionSettings(reader);

  lce_session_opened opened{};
  if (lce_open_session(toEngine(engine), &settings, nowMs, &opened) != LCE_OK) {
    return nullptr;
  }

  NumberWriter writer;
  writer.whole(opened.session_id);
  writer.whole(opened.collecting);
  writer.bits64(opened.reasons);
  return writer.toKotlin(env);
}

jint nativeClearSessionHistory(JNIEnv* /*env*/, jclass /*clazz*/, jlong engine, jint sessionId) {
  return lce_clear_session_history(toEngine(engine), static_cast<uint32_t>(sessionId));
}

jint nativeCloseSession(JNIEnv* /*env*/,
                        jclass /*clazz*/,
                        jlong engine,
                        jint sessionId,
                        jlong nowMs) {
  return lce_close_session(toEngine(engine), static_cast<uint32_t>(sessionId), nowMs);
}

// Returns the request id (1, 2, 3...) when the evaluation was accepted, or
// minus the error code of lce.h when it was not (for example -2 for
// LCE_ERROR_INVALID_REQUEST).
jlong nativeStartEvaluation(JNIEnv* env,
                            jclass /*clazz*/,
                            jlong engine,
                            jint sessionId,
                            jdoubleArray settingsNumbers,
                            jlong nowMs) {
  std::array<double, NUMBERS_IN_EVALUATION_SETTINGS> numbers{};
  if (!copyFromKotlin(env, settingsNumbers, NUMBERS_IN_EVALUATION_SETTINGS, numbers.data())) {
    return -static_cast<jlong>(LCE_ERROR_NULL_ARGUMENT);
  }
  NumberReader reader(numbers.data(), numbers.size());
  const lce_evaluation_settings settings = readEvaluationSettings(reader);

  uint32_t requestId = 0;
  const int32_t answer = lce_start_evaluation(toEngine(engine), static_cast<uint32_t>(sessionId),
                                              &settings, nowMs, &requestId);

  return answer == LCE_OK ? static_cast<jlong>(requestId) : -static_cast<jlong>(answer);
}

jint nativeCancelEvaluation(JNIEnv* /*env*/,
                            jclass /*clazz*/,
                            jlong engine,
                            jint requestId,
                            jlong nowMs) {
  return lce_cancel_evaluation(toEngine(engine), static_cast<uint32_t>(requestId), nowMs);
}

// `readingsNumbers` holds `howMany` readings, one after the other, each one
// NUMBERS_IN_ONE_READING numbers long. Kotlin makes one call for the whole
// delivery: crossing JNI once per reading would be wasteful.
jint nativeReadingsArrived(JNIEnv* env,
                           jclass /*clazz*/,
                           jlong engine,
                           jdoubleArray readingsNumbers,
                           jint howMany,
                           jlong nowMs) {
  if (howMany <= 0) {
    return howMany == 0 ? LCE_OK : LCE_ERROR_NULL_ARGUMENT;
  }
  if (readingsNumbers == nullptr) {
    return LCE_ERROR_NULL_ARGUMENT;
  }
  const jsize sizeOfTheList = env->GetArrayLength(readingsNumbers);
  if (sizeOfTheList != howMany * NUMBERS_IN_ONE_READING) {
    return LCE_ERROR_NULL_ARGUMENT;
  }

  std::array<double, READINGS_COPIED_AT_A_TIME * NUMBERS_IN_ONE_READING> numbers{};
  std::array<lce_reading, READINGS_COPIED_AT_A_TIME> readings{};

  jint answer = LCE_OK;
  jint nextReading = 0;

  while (nextReading < howMany) {
    jint howManyInThisPiece = howMany - nextReading;
    if (howManyInThisPiece > READINGS_COPIED_AT_A_TIME) {
      howManyInThisPiece = READINGS_COPIED_AT_A_TIME;
    }

    env->GetDoubleArrayRegion(readingsNumbers, nextReading * NUMBERS_IN_ONE_READING,
                              howManyInThisPiece * NUMBERS_IN_ONE_READING, numbers.data());

    NumberReader reader(numbers.data(),
                        static_cast<std::size_t>(howManyInThisPiece * NUMBERS_IN_ONE_READING));
    for (jint count = 0; count < howManyInThisPiece; ++count) {
      readings[static_cast<std::size_t>(count)] = readOneReading(reader);
    }

    answer = lce_readings_arrived(toEngine(engine), readings.data(),
                                  static_cast<uint32_t>(howManyInThisPiece), nowMs);
    nextReading += howManyInThisPiece;
  }

  return answer;
}

jint nativePhoneActivityChanged(JNIEnv* /*env*/,
                                jclass /*clazz*/,
                                jlong engine,
                                jint activity,
                                jint confidence,
                                jlong sinceMs,
                                jlong nowMs) {
  return lce_phone_activity_changed(toEngine(engine), activity, confidence, sinceMs, nowMs);
}

jint nativePhoneActivityUpdatesStopped(JNIEnv* /*env*/,
                                       jclass /*clazz*/,
                                       jlong engine,
                                       jlong nowMs) {
  return lce_phone_activity_updates_stopped(toEngine(engine), nowMs);
}

jint nativeTimePassed(JNIEnv* /*env*/, jclass /*clazz*/, jlong engine, jlong nowMs) {
  return lce_time_passed(toEngine(engine), nowMs);
}

jint nativeAppWentToBackground(JNIEnv* /*env*/, jclass /*clazz*/, jlong engine, jlong nowMs) {
  return lce_app_went_to_background(toEngine(engine), nowMs);
}

jint nativeLocationPermissionWasLost(JNIEnv* /*env*/,
                                     jclass /*clazz*/,
                                     jlong engine,
                                     jlong nowMs) {
  return lce_location_permission_was_lost(toEngine(engine), nowMs);
}

jint nativeLocationProviderFailed(JNIEnv* /*env*/, jclass /*clazz*/, jlong engine, jlong nowMs) {
  return lce_location_provider_failed(toEngine(engine), nowMs);
}

jint nativeSetReadingsAreSimulated(JNIEnv* /*env*/,
                                   jclass /*clazz*/,
                                   jlong engine,
                                   jboolean simulated) {
  return lce_set_readings_are_simulated(toEngine(engine), simulated == JNI_TRUE ? 1 : 0);
}

// Returns [GPS wanted, time between readings, activity updates wanted,
// wake up wanted, wake up at, check].
jdoubleArray nativeWhatThePlatformMustDoNow(JNIEnv* env, jclass /*clazz*/, jlong engine) {
  lce_platform_work work{};
  if (lce_what_the_platform_must_do_now(toEngine(engine), &work) != LCE_OK) {
    return nullptr;
  }

  NumberWriter writer;
  writer.whole(work.location_updates_are_wanted);
  writer.whole(work.time_between_location_updates_ms);
  writer.whole(work.phone_activity_updates_are_wanted);
  writer.whole(work.wake_up_is_wanted);
  writer.whole(work.wake_up_at_ms);
  return writer.toKotlin(env);
}

// Returns one finished report as a list of numbers, or null when no report
// is waiting. Kotlin calls it again and again until it gets null.
jdoubleArray nativeTakeFinishedReport(JNIEnv* env, jclass /*clazz*/, jlong engine) {
  lce_report report{};
  if (lce_take_finished_report(toEngine(engine), &report) != 1) {
    return nullptr;
  }

  NumberWriter writer;
  writeReport(report, writer);
  return writer.toKotlin(env);
}

// The public texts of the reasons, of the score limits and of the steps.
// Kotlin asks for them once and keeps them, so that the texts shown to the
// app are always the ones the engine defines.
//   which = 0: reasons    1: score limits    2: steps
jobjectArray nativeTexts(JNIEnv* env, jclass /*clazz*/, jint which) {
  uint32_t howMany = 0;
  const char* (*nameOf)(int32_t) = nullptr;

  if (which == 0) {
    howMany = lce_reason_count();
    nameOf = lce_reason_name;
  } else if (which == 1) {
    howMany = lce_score_limit_count();
    nameOf = lce_score_limit_name;
  } else if (which == 2) {
    howMany = lce_step_count();
    nameOf = lce_step_name;
  } else {
    return nullptr;
  }

  jclass stringClass = env->FindClass("java/lang/String");
  if (stringClass == nullptr) {
    return nullptr;
  }
  jobjectArray texts = env->NewObjectArray(static_cast<jsize>(howMany), stringClass, nullptr);
  if (texts == nullptr) {
    return nullptr;
  }

  for (uint32_t number = 0; number < howMany; ++number) {
    jstring text = env->NewStringUTF(nameOf(static_cast<int32_t>(number)));
    if (text == nullptr) {
      return nullptr;  // out of memory; the JVM already raised the error
    }
    env->SetObjectArrayElement(texts, static_cast<jsize>(number), text);
    // Give the reference back right away, so a long list cannot use up the
    // small number of references a JNI function is allowed to hold.
    env->DeleteLocalRef(text);
  }
  return texts;
}

// The table handed to the JVM. One row per `external` function in Kotlin:
//   { Kotlin function name, signature, C++ function }
//
// The signature is JNI's short way to write parameter and return types:
//   I = Int    J = Long    Z = Boolean    V = nothing (Unit)
//   [D = DoubleArray        [Ljava/lang/String; = Array<String>
//   "(J[DJ)[D" = takes a Long, a DoubleArray and a Long; returns a DoubleArray
const JNINativeMethod NATIVE_METHODS[] = {
    {"nativeAbiVersion", "()I", reinterpret_cast<void*>(nativeAbiVersion)},
    {"nativeCreateEngine", "()J", reinterpret_cast<void*>(nativeCreateEngine)},
    {"nativeDestroyEngine", "(J)V", reinterpret_cast<void*>(nativeDestroyEngine)},
    {"nativeOpenSession", "(J[DJ)[D", reinterpret_cast<void*>(nativeOpenSession)},
    {"nativeClearSessionHistory", "(JI)I", reinterpret_cast<void*>(nativeClearSessionHistory)},
    {"nativeCloseSession", "(JIJ)I", reinterpret_cast<void*>(nativeCloseSession)},
    {"nativeStartEvaluation", "(JI[DJ)J", reinterpret_cast<void*>(nativeStartEvaluation)},
    {"nativeCancelEvaluation", "(JIJ)I", reinterpret_cast<void*>(nativeCancelEvaluation)},
    {"nativeReadingsArrived", "(J[DIJ)I", reinterpret_cast<void*>(nativeReadingsArrived)},
    {"nativePhoneActivityChanged", "(JIIJJ)I",
     reinterpret_cast<void*>(nativePhoneActivityChanged)},
    {"nativePhoneActivityUpdatesStopped", "(JJ)I",
     reinterpret_cast<void*>(nativePhoneActivityUpdatesStopped)},
    {"nativeTimePassed", "(JJ)I", reinterpret_cast<void*>(nativeTimePassed)},
    {"nativeAppWentToBackground", "(JJ)I", reinterpret_cast<void*>(nativeAppWentToBackground)},
    {"nativeLocationPermissionWasLost", "(JJ)I",
     reinterpret_cast<void*>(nativeLocationPermissionWasLost)},
    {"nativeLocationProviderFailed", "(JJ)I",
     reinterpret_cast<void*>(nativeLocationProviderFailed)},
    {"nativeSetReadingsAreSimulated", "(JZ)I",
     reinterpret_cast<void*>(nativeSetReadingsAreSimulated)},
    {"nativeWhatThePlatformMustDoNow", "(J)[D",
     reinterpret_cast<void*>(nativeWhatThePlatformMustDoNow)},
    {"nativeTakeFinishedReport", "(J)[D", reinterpret_cast<void*>(nativeTakeFinishedReport)},
    {"nativeTexts", "(I)[Ljava/lang/String;", reinterpret_cast<void*>(nativeTexts)},
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
