from conan import ConanFile
from conan.tools.files import get, copy


class ZenohCxxConan(ConanFile):
    name = "zenoh-cpp"
    version = "1.9.0"
    description = "C++ client library for zenoh"
    license = "EPL-2.0 OR Apache-2.0"
    homepage = "https://github.com/eclipse-zenoh/zenoh-cpp"
    package_type = "header-library"
    no_copy_source = True
    requires = "zenoh-c/1.9.0"

    def source(self):
        get(self,
            "https://github.com/eclipse-zenoh/zenoh-cpp/archive/refs/tags/1.9.0.tar.gz",
            strip_root=True)

    def package(self):
        copy(self, "include/*", self.source_folder, self.package_folder)

    def package_info(self):
        self.cpp_info.bindirs = []
        self.cpp_info.libdirs = []
        self.cpp_info.set_property("cmake_file_name", "zenohcxx")
        self.cpp_info.set_property("cmake_target_name", "zenohcxx::zenohc")
