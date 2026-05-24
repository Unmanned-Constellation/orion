#include <cstddef>
#include <cstdint>

#include "orion/v1/envelope.pb.h"

// Feed arbitrary bytes to the Envelope deserializer.
// Asserts no crash or memory error under ASan.
// NOLINTNEXTLINE(readability-identifier-naming) — libFuzzer requires this exact name
extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
    orion::v1::Envelope env;
    env.ParseFromArray(data, static_cast<int>(size));
    return 0;
}
