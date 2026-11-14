cmake_minimum_required(VERSION 3.13)
project(location_confidence_core LANGUAGES CXX)

# The core is written in the C++17 subset but must also build as C++20,
# because React Native compiles iOS pods as C++20.
if(NOT DEFINED LCE_CXX_STANDARD)
  set(LCE_CXX_STANDARD 17)
endif()

add_library(location_confidence_core STATIC
  src/CFacade.cpp
)

set_target_properties(location_confidence_core PROPERTIES
  CXX_STANDARD ${LCE_CXX_STANDARD}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

target_include_directories(location_confidence_core PUBLIC include)

# Keep floating point results identical across arm64 and x86_64:
# no FMA contraction, no fast-math.
target_compile_options(location_confidence_core PRIVATE
  -Wall -Wextra -ffp-contract=off
)

option(LCE_BUILD_TESTS "Build host tests" OFF)
if(LCE_BUILD_TESTS)
  enable_testing()
  add_executable(lce_abi_probe tests/AbiProbe.cpp)
  set_target_properties(lce_abi_probe PROPERTIES
    CXX_STANDARD ${LCE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  target_link_libraries(lce_abi_probe PRIVATE location_confidence_core)
  add_test(NAME lce_abi_probe COMMAND lce_abi_probe)
endif()
