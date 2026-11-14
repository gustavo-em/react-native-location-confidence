cmake_minimum_required(VERSION 3.13)
project(location_confidence_core LANGUAGES CXX)

# The core is written in the C++17 subset but must also build as C++20,
# because React Native compiles iOS pods as C++20.
if(NOT DEFINED LCE_CXX_STANDARD)
  set(LCE_CXX_STANDARD 17)
endif()

option(LCE_BUILD_TESTS "Build host tests" OFF)
option(LCE_SANITIZE "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(LCE_WERROR "Treat warnings as errors (host builds only)" OFF)

add_library(location_confidence_core STATIC
  src/CFacade.cpp
  src/GeoMath.cpp
  src/LocationHistory.cpp
  src/ProximityRelation.cpp
  src/QualityScore.cpp
  src/ReadingChecker.cpp
  src/ReasonCodes.cpp
)

set_target_properties(location_confidence_core PROPERTIES
  CXX_STANDARD ${LCE_CXX_STANDARD}
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
  POSITION_INDEPENDENT_CODE ON
)

# include/ holds only the C facade. Internal C++ headers live in src/.
target_include_directories(location_confidence_core PUBLIC include)

# Keep floating point results identical across arm64 and x86_64:
# no FMA contraction, no fast-math.
target_compile_options(location_confidence_core PRIVATE
  -Wall -Wextra -Wpedantic -ffp-contract=off
)
if(LCE_WERROR)
  target_compile_options(location_confidence_core PRIVATE -Werror)
endif()

if(LCE_SANITIZE)
  target_compile_options(location_confidence_core PUBLIC
    -fsanitize=address,undefined -fno-sanitize-recover=undefined -fno-omit-frame-pointer)
  target_link_options(location_confidence_core PUBLIC -fsanitize=address,undefined)
endif()

if(LCE_BUILD_TESTS)
  enable_testing()
  add_executable(lce_tests
    tests/TestMain.cpp
    tests/TestFacade.cpp
    tests/TestGeoMath.cpp
    tests/TestHistory.cpp
    tests/TestProperties.cpp
    tests/TestReasonCodes.cpp
    tests/TestRelation.cpp
    tests/TestScore.cpp
    tests/TestValidation.cpp
  )
  set_target_properties(lce_tests PROPERTIES
    CXX_STANDARD ${LCE_CXX_STANDARD}
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
  )
  # Tests reach internal headers; consumers of the library do not.
  target_include_directories(lce_tests PRIVATE src tests)
  target_compile_options(lce_tests PRIVATE -Wall -Wextra -ffp-contract=off)
  target_link_libraries(lce_tests PRIVATE location_confidence_core)
  add_test(NAME lce_tests COMMAND lce_tests)
endif()
