cmake_minimum_required(VERSION 3.13)
project(LocationConfidenceJni LANGUAGES CXX)

# Only JNI_OnLoad is exported from the shared library; the core and its C
# facade stay internal.
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Shared core, compiled from source for each ABI. It does not link against
# React Native: the JNI library only exposes plain JNI functions.
set(LCE_CXX_STANDARD 17)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../cpp ${CMAKE_CURRENT_BINARY_DIR}/lce_core)

add_library(location-confidence SHARED
  src/main/cpp/JniBindings.cpp
)

set_target_properties(location-confidence PROPERTIES
  CXX_STANDARD 17
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)

target_compile_options(location-confidence PRIVATE -Wall -Wextra)

target_link_libraries(location-confidence PRIVATE location_confidence_core)
