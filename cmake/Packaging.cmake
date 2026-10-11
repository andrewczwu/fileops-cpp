# CPack configuration for the Debian package. The binary is fully static (cmake/StaticBinary.cmake),
# so the package declares no dependencies at all; scripts/package.sh verifies that claim against the
# ELF file before packaging. Documentation and the lintian override are installed by src/cli/CMakeLists.txt.

set(CPACK_PACKAGE_NAME "fileops-cpp")
set(CPACK_PACKAGE_VENDOR "Andrew Wu")
set(CPACK_PACKAGE_CONTACT "Andrew Wu <andrewczwu@gmail.com>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Create, copy, combine and delete files safely")
# Debian control format: continuation lines of the long description start with one space.
# editorconfig-checker-disable
set(CPACK_PACKAGE_DESCRIPTION
  "Command-line tool for creating, copying, combining and deleting files.
 Writes are atomic (temp file + rename), destinations are never overwritten
 without --force, exit codes are stable, and every run can emit structured
 JSON logs. Ships as one fully static executable with no run-time dependencies.")
# editorconfig-checker-enable
set(CPACK_PACKAGE_HOMEPAGE_URL "${PROJECT_HOMEPAGE_URL}")
set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
set(CPACK_STRIP_FILES ON)

# The control file carries the Debian form of the version (FILEOPS_DEB_VERSION, set in the top-level
# CMakeLists.txt); the file name keeps the SemVer form because GitHub rewrites `~` in asset names.
set(CPACK_PACKAGE_VERSION "${FILEOPS_DEB_VERSION}")
set(CPACK_PACKAGE_FILE_NAME "fileops-cpp_${FILEOPS_VERSION}_amd64")

set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "${PROJECT_HOMEPAGE_URL}")
set(CPACK_DEBIAN_COMPRESSION_TYPE "xz")
set(CPACK_DEBIAN_FILE_NAME "${CPACK_PACKAGE_FILE_NAME}.deb")
set(CPACK_DEBIAN_PACKAGE_CONTROL_STRICT_PERMISSION ON)
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "${CPACK_PACKAGE_CONTACT}")

include(CPack)
