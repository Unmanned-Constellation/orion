#pragma once

// Template method bodies for Session.
// Included by session.hpp — do not include directly.

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
    return Subscriber<T>(makeSubscriberBackend(topic, makeRawCallback<T>(std::move(callback))));
}

} // namespace orion::transport
