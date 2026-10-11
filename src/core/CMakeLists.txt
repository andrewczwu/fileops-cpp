# libfileops_core: the operations, atomic writer and telemetry contract. No third-party dependencies,
# so any host (this CLI, a service, a test) can link it and attach whatever log sink it wants.

add_library(fileops_core STATIC
  atomic_file.cpp
  log.cpp
  operations.cpp)
add_library(fileops::core ALIAS fileops_core)

target_include_directories(fileops_core
  PUBLIC $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(fileops_core PUBLIC cxx_std_20)
target_link_libraries(fileops_core PRIVATE fileops::warnings)
