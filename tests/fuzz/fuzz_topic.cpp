#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

// Fuzz topic key-expression construction from arbitrary bytes.
// Extend this target to call into the real key-expression validator once the
// transport layer exposes one.
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, const size_t size)
{
    const std::string_view raw{reinterpret_cast<const char*>(data), size};
    const std::string      topic{raw};
    // TODO: pass `topic` through orion::transport key-expression validation.
    return 0;
}
