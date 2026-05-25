#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

#include <zenoh/api/base.hxx>
#include <zenoh/api/bytes.hxx>
#include <zenoh/api/config.hxx>
#include <zenoh/api/keyexpr.hxx>
#include <zenoh/api/publisher.hxx>
#include <zenoh/api/sample.hxx>
#include <zenoh/api/session.hxx>
#include <zenoh/api/subscriber.hxx>
#include <zenoh_concrete.h>

#include "orion/clock/clock.hpp"
#include "orion/transport/config.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/session.hpp"
#include "orion/transport/subscriber.hpp"

namespace orion::transport
{

// ── SessionImpl ───────────────────────────────────────────────────────────────

class SessionImpl
{
  public:
    explicit SessionImpl(zenoh::Session session) : session_(std::move(session)) {}

    zenoh::Session& session() { return session_; }

  private:
    zenoh::Session session_;
};

// ── ZenohPublisherBackend ─────────────────────────────────────────────────────

class ZenohPublisherBackend final : public PublisherBackend
{
  public:
    explicit ZenohPublisherBackend(zenoh::Publisher publisher) : publisher_(std::move(publisher)) {}

    void send(std::string_view bytes) override
    {
        zenoh::ZResult err{};
        publisher_.put(zenoh::Bytes(bytes), zenoh::Publisher::PutOptions::create_default(), &err);
        if (err != Z_OK)
        {
            throw std::runtime_error("ZenohPublisherBackend: put failed");
        }
    }

  private:
    zenoh::Publisher publisher_;
};

// ── ZenohSubscriberBackend ────────────────────────────────────────────────────

class ZenohSubscriberBackend final : public SubscriberBackend
{
  public:
    ZenohSubscriberBackend(zenoh::Subscriber<void> subscriber, RawCallback callback)
        : subscriber_(std::move(subscriber)), callback_(std::move(callback))
    {
    }

  private:
    zenoh::Subscriber<void> subscriber_;
    RawCallback             callback_;
};

// ── Session ───────────────────────────────────────────────────────────────────

Session::Session(std::unique_ptr<SessionImpl> impl) : impl_(std::move(impl)) {}

Session::~Session() = default;

Session Session::create(SessionConfig config, std::shared_ptr<orion::clock::Clock> clock)
{
    zenoh::ZResult err{};
    auto           zenoh_config = config.zenoh_config_path
                                      ? zenoh::Config::from_file(*config.zenoh_config_path, &err)
                                      : zenoh::Config::create_default(&err);
    if (err != Z_OK)
    {
        throw std::runtime_error("Session::create: failed to build Zenoh config");
    }

    auto zenoh_session = zenoh::Session::open(std::move(zenoh_config), {}, &err);
    if (err != Z_OK)
    {
        throw std::runtime_error("Session::create: failed to open Zenoh session");
    }

    auto session       = Session(std::make_unique<SessionImpl>(std::move(zenoh_session)));
    session.clock_     = std::move(clock);
    session.source_id_ = std::move(config.service_name);
    return session;
}

std::unique_ptr<PublisherBackend> Session::makePublisherBackend(std::string_view topic)
{
    zenoh::ZResult err{};
    auto           pub =
        impl_->session().declare_publisher(zenoh::KeyExpr(std::string(topic)),
                                           zenoh::Session::PublisherOptions::create_default(),
                                           &err);
    if (err != Z_OK)
    {
        throw std::runtime_error("Session::makePublisherBackend: declare_publisher failed");
    }
    return std::make_unique<ZenohPublisherBackend>(std::move(pub));
}

std::unique_ptr<SubscriberBackend> Session::makeSubscriberBackend(std::string_view topic,
                                                                  RawCallback      callback)
{
    zenoh::ZResult err{};

    auto on_sample = [callback](const zenoh::Sample& sample) {
        const auto& payload = sample.get_payload();
        auto        str     = payload.as_string();
        callback(std::string_view(str));
    };

    auto subscriber = impl_->session().declare_subscriber(
        zenoh::KeyExpr(std::string(topic)),
        std::move(on_sample),
        []() {},
        zenoh::Session::SubscriberOptions::create_default(),
        &err);
    if (err != Z_OK)
    {
        throw std::runtime_error("Session::makeSubscriberBackend: declare_subscriber failed");
    }

    return std::make_unique<ZenohSubscriberBackend>(std::move(subscriber), std::move(callback));
}

} // namespace orion::transport
