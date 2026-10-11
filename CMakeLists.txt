cmake_minimum_required(VERSION 3.25)

# The version is injected by the build (git tag in CI, 0.0.0 locally); see scripts/ci/version-from-tag.sh.
set(FILEOPS_VERSION "" CACHE STRING "Semantic version of this build (MAJOR.MINOR.PATCH[-pre]); empty means 0.0.0")
if(NOT FILEOPS_VERSION)
  set(FILEOPS_VERSION "0.0.0")
endif()
if(NOT FILEOPS_VERSION MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+(-[0-9A-Za-z.-]+)?$")
  message(FATAL_ERROR "FILEOPS_VERSION must be SemVer (MAJOR.MINOR.PATCH[-pre]), got '${FILEOPS_VERSION}'")
endif()
string(REGEX MATCH "^[0-9]+\\.[0-9]+\\.[0-9]+" FILEOPS_NUMERIC_VERSION "${FILEOPS_VERSION}")

project(fileops
  VERSION ${FILEOPS_NUMERIC_VERSION}
  DESCRIPTION "Create, copy, combine and delete files with atomic writes, stable exit codes and structured logs"
  HOMEPAGE_URL "https://github.com/andrewczwu/fileops-cpp"
  LANGUAGES CXX)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

option(FILEOPS_BUILD_TESTS "Build the unit and integration tests" ON)
option(FILEOPS_ENABLE_SANITIZERS "Build with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(FILEOPS_STATIC_BINARY "Link fully statically (libstdc++, libgcc, glibc) so the binary runs on any x86-64 Linux" OFF)
option(FILEOPS_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" ON)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON) # consumed by clang-tidy
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

include(CompilerWarnings)
include(Sanitizers)
include(StaticBinary)

add_subdirectory(src/core)
add_subdirectory(src/cli)

if(FILEOPS_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

include(Packaging)
