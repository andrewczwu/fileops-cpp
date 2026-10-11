# Two suites, both GoogleTest:
#   fileops_unit_tests        link libfileops_core and drive it in-process with an in-memory logger
#   fileops_integration_tests spawn the real `fileops` binary and assert on exit codes, stdout, and
#                             the NDJSON log stream (FILEOPS_BIN overrides which binary is exercised)

find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(opentelemetry-cpp REQUIRED) # also sets opentelemetry-cpp_LIB_DIRS_* in this directory scope
include(GoogleTest)

add_library(fileops_test_support INTERFACE)
target_include_directories(fileops_test_support INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/support)
target_link_libraries(fileops_test_support INTERFACE fileops::core GTest::gtest GTest::gtest_main fileops::warnings)

add_executable(fileops_unit_tests
  unit/atomic_file_test.cpp
  unit/combine_test.cpp
  unit/copy_test.cpp
  unit/create_test.cpp
  unit/delete_test.cpp
  unit/observer_contract_test.cpp
  unit/otel_mapping_test.cpp
  unit/telemetry_contract_test.cpp)
# The in-memory exporters ship in the opentelemetry-cpp package but its Conan recipe declares no
# CMake component for them, so they are located by name in the package's library directory. They
# must precede the SDK libraries on the link line (static archives resolve left to right).
find_library(OTEL_IN_MEMORY_SPAN_EXPORTER opentelemetry_exporter_in_memory
  HINTS ${opentelemetry-cpp_LIB_DIRS_DEBUG} ${opentelemetry-cpp_LIB_DIRS_RELEASE} ${opentelemetry-cpp_LIB_DIRS_RELWITHDEBINFO}
  REQUIRED NO_DEFAULT_PATH)
find_library(OTEL_IN_MEMORY_METRIC_EXPORTER opentelemetry_exporter_in_memory_metric
  HINTS ${opentelemetry-cpp_LIB_DIRS_DEBUG} ${opentelemetry-cpp_LIB_DIRS_RELEASE} ${opentelemetry-cpp_LIB_DIRS_RELWITHDEBINFO}
  REQUIRED NO_DEFAULT_PATH)
target_link_libraries(fileops_unit_tests PRIVATE
  fileops_test_support
  ${OTEL_IN_MEMORY_SPAN_EXPORTER} ${OTEL_IN_MEMORY_METRIC_EXPORTER}
  fileops::otel)
gtest_discover_tests(fileops_unit_tests TEST_PREFIX "unit." DISCOVERY_MODE PRE_TEST)

add_executable(fileops_integration_tests
  integration/tool.cpp
  integration/otlp_receiver.cpp
  integration/cli_contract_test.cpp
  integration/command_test.cpp
  integration/otlp_export_test.cpp)
target_link_libraries(fileops_integration_tests PRIVATE fileops_test_support nlohmann_json::nlohmann_json)
add_dependencies(fileops_integration_tests fileops)
# The binary under test defaults to the one this build produced; FILEOPS_BIN in the environment wins.
target_compile_definitions(fileops_integration_tests PRIVATE FILEOPS_DEFAULT_BIN="$<TARGET_FILE:fileops>")
gtest_discover_tests(fileops_integration_tests TEST_PREFIX "integration." DISCOVERY_MODE PRE_TEST)
