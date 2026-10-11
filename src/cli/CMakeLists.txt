# The `fileops` executable: CLI11 front end over libfileops_core, with text/JSON log sinks.

find_package(CLI11 REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(opentelemetry-cpp REQUIRED)

# The OpenTelemetry mapping is a library of its own so the unit tests can drive it with in-memory
# exporters; the executable links it together with the console sinks.
add_library(fileops_otel STATIC otel.cpp)
add_library(fileops::otel ALIAS fileops_otel)
target_include_directories(fileops_otel PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(fileops_otel
  PUBLIC fileops::core opentelemetry-cpp::opentelemetry-cpp
  PRIVATE fileops::warnings)

add_executable(fileops main.cpp sinks.cpp)
target_link_libraries(fileops PRIVATE fileops::core fileops::otel CLI11::CLI11 nlohmann_json::nlohmann_json fileops::warnings)
target_compile_definitions(fileops PRIVATE FILEOPS_VERSION_STRING="${FILEOPS_VERSION}")
if(FILEOPS_STATIC_BINARY)
  target_link_options(fileops PRIVATE -static) # see cmake/StaticBinary.cmake
endif()

install(TARGETS fileops RUNTIME DESTINATION bin)

# Debian documentation: compressed man page and changelog, DEP-5 copyright. SOURCE_DATE_EPOCH (set by
# the release workflow from the tagged commit) makes the changelog date reproducible.
set(_pkg_src "${PROJECT_SOURCE_DIR}/packaging")
set(_pkg_out "${CMAKE_BINARY_DIR}/packaging")
file(MAKE_DIRECTORY "${_pkg_out}")

string(TIMESTAMP _changelog_date "%a, %d %b %Y %H:%M:%S +0000" UTC)
configure_file("${_pkg_src}/changelog.in" "${_pkg_out}/changelog" @ONLY)

find_program(GZIP_EXECUTABLE gzip REQUIRED)
add_custom_command(
  OUTPUT "${_pkg_out}/fileops.1.gz" "${_pkg_out}/changelog.gz"
  COMMAND ${GZIP_EXECUTABLE} -9 -n -c "${_pkg_src}/fileops.1" > "${_pkg_out}/fileops.1.gz"
  COMMAND ${GZIP_EXECUTABLE} -9 -n -c "${_pkg_out}/changelog" > "${_pkg_out}/changelog.gz"
  DEPENDS "${_pkg_src}/fileops.1" "${_pkg_out}/changelog"
  COMMENT "Compressing man page and changelog"
  VERBATIM)
add_custom_target(fileops_docs ALL DEPENDS "${_pkg_out}/fileops.1.gz" "${_pkg_out}/changelog.gz")

install(FILES "${_pkg_out}/fileops.1.gz" DESTINATION share/man/man1)
install(FILES "${_pkg_out}/changelog.gz" "${_pkg_src}/copyright" DESTINATION share/doc/fileops-cpp)
# lintian flags static binaries by default; the override states why this one is static on purpose.
install(FILES "${_pkg_src}/lintian-overrides" DESTINATION share/lintian/overrides RENAME fileops-cpp)
