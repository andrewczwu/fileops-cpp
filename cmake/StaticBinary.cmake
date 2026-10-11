# Release builds are linked fully statically: libstdc++, libgcc and glibc. The toolchain image is
# Ubuntu 24.04 (glibc 2.39) and its libstdc++ calls glibc 2.36/2.38 symbols (arc4random for
# std::random_device, __isoc23_strtoll for std::stoll), so a dynamically linked binary would refuse
# to start on Debian 12 (2.36) and Ubuntu 22.04 (2.35) even with the C++ runtime linked statically.
# A static binary has no such floor and needs nothing installed on the target.
#
# The usual objections to static glibc do not apply here: the tool never resolves users, hosts or
# locales (no NSS, no iconv, no dlopen), which is exactly what the linker warns about.

# Only the shipped executable is linked this way (src/cli/CMakeLists.txt); the test binaries stay
# dynamic, since GoogleTest's streaming listener references getaddrinfo and would only add noise.

if(FILEOPS_STATIC_BINARY)
  if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
    message(FATAL_ERROR "FILEOPS_STATIC_BINARY is only meaningful on Linux")
  endif()
  message(STATUS "fileops: linking the fileops executable fully statically")
endif()
