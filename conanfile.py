from conan import ConanFile
from conan.tools.cmake import CMakeDeps, CMakeToolchain


class OrionConan(ConanFile):
    name = "orion"
    settings = "os", "compiler", "build_type", "arch"
    requires = (
        "protobuf/6.33.5",
        "zenoh-c/1.9.0",
        "zenoh-cpp/1.9.0",
    )

    def generate(self):
        tc = CMakeToolchain(self)
        tc.generator = "Ninja"
        tc.generate()
        CMakeDeps(self).generate()
