from conan import ConanFile
from conan.tools.files import get, copy


class ZenohCConan(ConanFile):
    name = "zenoh-c"
    version = "1.9.0"
    description = "C client library for zenoh"
    license = "EPL-2.0 OR Apache-2.0"
    homepage = "https://github.com/eclipse-zenoh/zenoh-c"
    package_type = "shared-library"
    # Pre-built binaries are platform-specific but not compiler/build-type specific.
    settings = "os", "arch"

    def source(self):
        # The standalone zip has no root directory — lib/ and include/ are top-level.
        get(self,
            "https://github.com/eclipse-zenoh/zenoh-c/releases/download/1.9.0/zenoh-c-1.9.0-x86_64-unknown-linux-gnu-standalone.zip",
            strip_root=False)

    def package(self):
        copy(self, "include/*", self.source_folder, self.package_folder)
        copy(self, "lib/libzenohc.so*", self.source_folder, self.package_folder)

    def package_info(self):
        self.cpp_info.libs = ["zenohc"]
        self.cpp_info.set_property("cmake_file_name", "zenohc")
        self.cpp_info.set_property("cmake_target_name", "zenohc::lib")
