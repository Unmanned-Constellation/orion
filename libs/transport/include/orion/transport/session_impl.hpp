#pragma once

// Template method bodies for Session.
// Included by session.hpp — do not include directly.

#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace orion::transport
{

template <typename T>
Publisher<T> Session::advertise(std::string_view topic)
{
    return Publisher<T>(makePublisherBackend(topic), clock_, source_id_);
}

template <typename T>
Subscriber<T> Session::subscribe(std::string_view topic, typename Subscriber<T>::Callback callback)
{
    const std::string expected_type = T::descriptor()->full_name();

    RawCallback raw = [cb       = std::move(callback),
                       expected = std::move(expected_type)](std::string_view bytes) {
        orion::v1::Envelope env;
        if (!env.ParseFromArray(bytes.data(), static_cast<int>(bytes.size())))
        {
            return;
        }
        if (env.type_url() != expected)
        {
            return;
        }
        T msg;
        if (!msg.ParseFromString(env.payload()))
        {
            return;
        }
        MessageHeader hdr{
            .published_at_ns = env.header().published_at_ns(),
            .source_id       = env.header().source_id(),
        };
        cb(msg, hdr);
    };

    return Subscriber<T>(makeSubscriberBackend(topic, std::move(raw)));
}

} // namespace orion::transport
