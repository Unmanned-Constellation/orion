from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain, cmake_layout
from conan.tools.env import VirtualBuildEnv


class OrionConan(ConanFile):
    name = "orion"
    settings = "os", "compiler", "build_type", "arch"

    def requirements(self):
        self.requires("protobuf/5.29.3")
        self.requires("zenoh-c/1.9.0")
        self.requires("zenoh-cpp/1.9.0")
        self.requires("abseil/20240722.0", force=True)
        self.requires("backward-cpp/1.6", options={"stack_details": "dw"})
        self.requires("spdlog/1.17.0")

    def build_requirements(self):
        self.tool_requires("protobuf/5.29.3")
        self.test_requires("gtest/1.17.0")

    def layout(self):
        cmake_layout(self)

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.variables["CMAKE_MAP_IMPORTED_CONFIG_DEBUG"] = "Debug;Release;"
        tc.generate()
        CMakeDeps(self).generate()
        VirtualBuildEnv(self).generate()
