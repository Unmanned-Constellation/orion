#pragma once

// Template helper bodies for Subscriber<T>.
// Included by subscriber.hpp — do not include directly.

#include "orion/transport/message_header.hpp"
#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace orion::transport
{

/// Wraps a typed Subscriber<T>::Callback in a RawCallback that decodes Envelope framing.
///
/// Used internally by Session::subscribe and by FakeSubscriptionHandle in tests.
/// @tparam T  Protobuf message type to decode and deliver.
template <typename T>
auto makeRawCallback(typename Subscriber<T>::Callback callback) -> RawCallback
{
    auto expected_type = std::string{T::descriptor()->full_name()};
    return RawCallback{
        [cb = std::move(callback), expected = std::move(expected_type)](std::string_view bytes) {
            auto env = orion::v1::Envelope{};
            if (!env.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
            {
                return;
            }
            if (env.type_url() != expected)
            {
                return;
            }
            auto msg = T{};
            if (!msg.ParseFromString(env.payload()))
            {
                return;
            }
            auto hdr = MessageHeader{
                .captured_at_ns = env.header().captured_at_ns(),
                .source_id      = env.header().source_id(),
            };
            cb(msg, hdr);
        }};
}

} // namespace orion::transport
