"""Conan 2 recipe: declares the dependency graph and generates the CMake toolchain and package configs.

Usage (see the Makefile for the canonical commands):
    conan install . --output-folder build/<preset> --build=missing --lockfile conan.lock \
        --profile:host conan/profiles/<profile> --settings build_type=Release
    cmake --preset <preset> && cmake --build --preset <preset>
"""

from conan import ConanFile


class FileopsRecipe(ConanFile):
    name = "fileops"
    settings = "os", "compiler", "build_type", "arch"
    generators = "CMakeDeps", "CMakeToolchain"

    # OpenTelemetry: traces, metrics and logs over OTLP/HTTP only. gRPC would add grpc and its
    # toolchain for no gain; the collector speaks both. libcurl resolves names with c-ares so DNS
    # works inside the fully static release binary (static glibc cannot load NSS modules).
    default_options = {
        "opentelemetry-cpp/*:with_otlp_http": True,
        "opentelemetry-cpp/*:with_otlp_grpc": False,
        "opentelemetry-cpp/*:with_otlp_file": False,
        "opentelemetry-cpp/*:with_zipkin": False,
        "opentelemetry-cpp/*:with_prometheus": False,
        "opentelemetry-cpp/*:with_elasticsearch": False,
        "libcurl/*:with_c_ares": True,
        # curl refuses both resolvers at once; c-ares is the one that works in a static binary.
        "libcurl/*:with_threaded_resolver": False,
    }

    def requirements(self):
        # Pinned here; conan.lock pins the full resolved graph including transitive packages.
        self.requires("cli11/2.7.2")
        # force: opentelemetry-cpp asks for an older nlohmann_json; one version for the whole graph.
        self.requires("nlohmann_json/3.12.0", force=True)
        self.requires("opentelemetry-cpp/1.26.0")

    def build_requirements(self):
        self.test_requires("gtest/1.18.0")

    def layout(self):
        # Everything Conan generates lands in <output-folder>/generators so the CMake preset can
        # point at a stable toolchain path.
        self.folders.generators = "generators"
