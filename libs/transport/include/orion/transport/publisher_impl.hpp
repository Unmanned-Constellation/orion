#pragma once

// Template method bodies for Publisher<T>.
// Included by publisher.hpp — do not include directly.

#include "orion/v1/envelope.pb.h"
#include "orion/v1/header.pb.h"

namespace orion::transport
{

template <typename T>
void Publisher<T>::publish(const T& msg)
{
    orion::v1::Header hdr;
    hdr.set_published_at_ns(clock_->nowNs());
    hdr.set_source_id(source_id_);

    orion::v1::Envelope env;
    *env.mutable_header() = hdr;
    env.set_payload(msg.SerializeAsString());
    env.set_type_url(T::descriptor()->full_name());

    backend_->send(env.SerializeAsString());
}

} // namespace orion::transport
