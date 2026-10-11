# A strict, shared warning set applied through an interface target so every project target opts in
# the same way: target_link_libraries(<target> PRIVATE fileops::warnings).

add_library(fileops_warnings INTERFACE)
add_library(fileops::warnings ALIAS fileops_warnings)

set(_fileops_warnings
  -Wall
  -Wextra
  -Wpedantic
  -Wshadow
  -Wconversion
  -Wsign-conversion
  -Wold-style-cast
  -Wcast-align
  -Wunused
  -Woverloaded-virtual
  -Wnon-virtual-dtor
  # -Wnull-dereference is deliberately absent: with -O2 GCC reports it inside libstdc++'s own
  # stream iterators (a documented false positive). Null misuse is caught by UBSan and the
  # clang-analyzer core checks in .clang-tidy instead.
  -Wdouble-promotion
  -Wformat=2
  -Wimplicit-fallthrough)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  list(APPEND _fileops_warnings -Wmisleading-indentation -Wduplicated-cond -Wduplicated-branches -Wlogical-op -Wuseless-cast)
endif()

if(FILEOPS_WARNINGS_AS_ERRORS)
  list(APPEND _fileops_warnings -Werror)
endif()

target_compile_options(fileops_warnings INTERFACE ${_fileops_warnings})
