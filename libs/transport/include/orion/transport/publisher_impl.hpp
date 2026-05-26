#pragma once

// Template method bodies for Publisher<T>.
// Included by publisher.hpp — do not include directly.

#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace orion::transport
{

template <typename T>
void Publisher<T>::publish(const T& msg, uint64_t captured_at_ns)
{
    auto hdr = orion::v1::Header{};
    hdr.set_captured_at_ns(captured_at_ns);
    hdr.set_source_id(source_id_);

    auto env              = orion::v1::Envelope{};
    *env.mutable_header() = hdr;
    env.set_payload(msg.SerializeAsString());
    env.set_type_url(T::descriptor()->full_name());

    backend_->send(env.SerializeAsString());
}

} // namespace orion::transport
