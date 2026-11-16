# Builds the JNI library for THIS computer (not for Android), so the Kotlin
# unit tests can run the real engine through the real JNI code without a phone
# or an emulator.
#
#   Kotlin test (on the computer's JVM)
#     -> NativeCore.kt
#       -> JniBindings.cpp        the same file the Android build compiles
#         -> the C++ engine       the same sources
#
# android/build.gradle runs this before the unit tests and points
# java.library.path at the folder where the library ends up.
cmake_minimum_required(VERSION 3.13)
project(LocationConfidenceHostJni LANGUAGES CXX)

# The JDK brings jni.h for the computer.
find_package(JNI REQUIRED)

set(LCE_CXX_STANDARD 17)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../../../../cpp ${CMAKE_CURRENT_BINARY_DIR}/lce_core)

add_library(location-confidence SHARED
  ${CMAKE_CURRENT_SOURCE_DIR}/../../main/cpp/JniBindings.cpp
)

set_target_properties(location-confidence PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_include_directories(location-confidence PRIVATE ${JNI_INCLUDE_DIRS})

# The JDK's jni.h declares the names in the table of native methods as
# `char*` where Android's declares `const char*`. That makes the computer's
# compiler warn about every row; the Android build has no such warning.
target_compile_options(location-confidence PRIVATE -Wall -Wextra -Wno-writable-strings)

target_link_libraries(location-confidence PRIVATE location_confidence_core)
