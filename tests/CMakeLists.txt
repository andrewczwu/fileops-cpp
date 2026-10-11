# Two suites, both GoogleTest:
#   fileops_unit_tests        link libfileops_core and drive it in-process with an in-memory logger
#   fileops_integration_tests spawn the real `fileops` binary and assert on exit codes, stdout, and
#                             the NDJSON log stream (FILEOPS_BIN overrides which binary is exercised)

find_package(GTest REQUIRED)
find_package(nlohmann_json REQUIRED)
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
  unit/telemetry_contract_test.cpp)
target_link_libraries(fileops_unit_tests PRIVATE fileops_test_support)
gtest_discover_tests(fileops_unit_tests TEST_PREFIX "unit." DISCOVERY_MODE PRE_TEST)

add_executable(fileops_integration_tests
  integration/tool.cpp
  integration/cli_contract_test.cpp
  integration/command_test.cpp)
target_link_libraries(fileops_integration_tests PRIVATE fileops_test_support nlohmann_json::nlohmann_json)
add_dependencies(fileops_integration_tests fileops)
# The binary under test defaults to the one this build produced; FILEOPS_BIN in the environment wins.
target_compile_definitions(fileops_integration_tests PRIVATE FILEOPS_DEFAULT_BIN="$<TARGET_FILE:fileops>")
gtest_discover_tests(fileops_integration_tests TEST_PREFIX "integration." DISCOVERY_MODE PRE_TEST)
