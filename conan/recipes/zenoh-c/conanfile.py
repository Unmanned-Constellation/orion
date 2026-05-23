from conan import ConanFile
from conan.tools.files import get, copy

class ZenohCConan(ConanFile):
    name = "zenoh-c"
    version = "1.9.0"
    description = "C client library for zenoh"
    license = "EPL-2.0 OR Apache-2.0"
    homepage = "https://github.com/eclipse-zenoh/zenoh-c"
    package_type = "shared-library"
    settings = "os", "arch"

    def source(self):
        # Map Conan's architecture strings to Zenoh's release naming convention
        arch_map = {
            "x86_64": "x86_64",
            "armv8": "aarch64"
        }
        
        target_arch = arch_map.get(str(self.settings.arch))
        if not target_arch:
            raise Exception(f"Unsupported architecture: {self.settings.arch}")

        url = f"https://github.com/eclipse-zenoh/zenoh-c/releases/download/{self.version}/zenoh-c-{self.version}-{target_arch}-unknown-linux-gnu-standalone.zip"
        
        get(self, url, strip_root=False)

    def package(self):
        copy(self, "include/*", self.source_folder, self.package_folder)
        copy(self, "lib/libzenohc.so*", self.source_folder, self.package_folder)

    def package_info(self):
        self.cpp_info.libs = ["zenohc"]
        self.cpp_info.set_property("cmake_file_name", "zenohc")
        self.cpp_info.set_property("cmake_target_name", "zenohc::lib")