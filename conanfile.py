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

    def requirements(self):
        # Pinned here; conan.lock pins the full resolved graph including transitive packages.
        self.requires("cli11/2.7.2")
        self.requires("nlohmann_json/3.12.0")

    def build_requirements(self):
        self.test_requires("gtest/1.18.0")

    def layout(self):
        # Everything Conan generates lands in <output-folder>/generators so the CMake preset can
        # point at a stable toolchain path.
        self.folders.generators = "generators"
