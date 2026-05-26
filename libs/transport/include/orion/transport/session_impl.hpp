#pragma once

// Template method bodies for Session.
// Included by session.hpp — do not include directly.

#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace orion::transport
{

template <typename T>
auto Session::advertise(std::string_view topic) -> Publisher<T>
{
    return Publisher<T>(makePublisherBackend(topic), source_id_);
}

template <typename T>
auto Session::subscribe(std::string_view                 topic,
                        typename Subscriber<T>::Callback callback) -> Subscriber<T>
{
    auto expected_type = std::string{T::descriptor()->full_name()};

    auto raw = RawCallback{
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

    return Subscriber<T>(makeSubscriberBackend(topic, std::move(raw)));
}

} // namespace orion::transport
