#include <array>
#include <memory>
#include <string>

#include <unistd.h>

#include <CLI/CLI.hpp>
#include <gtest/gtest.h>
#include <limits.h>
#include <spdlog/sinks/null_sink.h>
#include <spdlog/spdlog.h>

#include "orion/app/service_bootstrapper.hpp"
#include "orion/clock/clock.hpp"

namespace
{

auto nullLogger() -> std::shared_ptr<spdlog::logger>
{
    return std::make_shared<spdlog::logger>("null",
                                            std::make_shared<spdlog::sinks::null_sink_st>());
}

struct ServiceBootstrapperFixture : ::testing::Test
{
};

} // namespace

// ── vehicle_id parsing ────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, VehicleIdDefaultsToHostname)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 1>{"test-service"};

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    auto buf = std::array<char, HOST_NAME_MAX + 1>{};
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    gethostname(buf.data(), buf.size());
    EXPECT_EQ(ctx.vehicle_id, std::string{buf.data()});
}

TEST_F(ServiceBootstrapperFixture, VehicleIdFromFlag)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.vehicle_id, "alpha");
}

TEST_F(ServiceBootstrapperFixture, VehicleIdFromEnv)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 1>{"test-service"};

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("VEHICLE_ID", "bravo", 1);
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");

    EXPECT_EQ(ctx.vehicle_id, "bravo");
}

TEST_F(ServiceBootstrapperFixture, VehicleIdFlagOverridesEnv)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("VEHICLE_ID", "bravo", 1);
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");

    EXPECT_EQ(ctx.vehicle_id, "alpha");
}

// ── latch ─────────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LatchIsNotStoppedAfterRun)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    EXPECT_FALSE(ctx.latch->stopped());
}

TEST_F(ServiceBootstrapperFixture, LatchRefIsBootstrappersLatch)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    ctx.latch->stop();

    EXPECT_TRUE(ctx.latch->stopped());
}

// ── logger ────────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LogIsNonNull)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    EXPECT_NE(ctx.log, nullptr);
}

TEST_F(ServiceBootstrapperFixture, InjectedLoggerAppearsInContext)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto injected  = nullLogger();
    auto ctx       = bootstrap.withLogger(injected).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.log.get(), injected.get());
}

// ── log level ─────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LogLevelDefaultsToInfo)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto log       = nullLogger();
    auto ctx       = bootstrap.withLogger(log).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.log->level(), spdlog::level::info);
}

TEST_F(ServiceBootstrapperFixture, LogLevelSetFromFlag)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args =
        std::array<const char*, 5>{"test-service", "--vehicle-id", "alpha", "--log-level", "debug"};
    auto log = nullLogger();
    auto ctx = bootstrap.withLogger(log).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.log->level(), spdlog::level::debug);
}

// ── service_name ─────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, ServiceNameAppearsInContext)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"my-service"};
    auto args      = std::array<const char*, 3>{"my-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.service_name, "my-service");
}

// ── clock ─────────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, ClockIsNonNull)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    EXPECT_NE(ctx.clock, nullptr);
}

TEST_F(ServiceBootstrapperFixture, ClockDefaultsToWallClock)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    EXPECT_NE(std::dynamic_pointer_cast<orion::clock::WallClock>(ctx.clock), nullptr);
}

TEST_F(ServiceBootstrapperFixture, ClockWallFlagCreatesWallClock)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args =
        std::array<const char*, 5>{"test-service", "--vehicle-id", "alpha", "--clock", "wall"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    EXPECT_NE(std::dynamic_pointer_cast<orion::clock::WallClock>(ctx.clock), nullptr);
}

TEST_F(ServiceBootstrapperFixture, ClockSimFlagCreatesSimClock)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args =
        std::array<const char*, 5>{"test-service", "--vehicle-id", "alpha", "--clock", "sim:2.0"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    EXPECT_NE(std::dynamic_pointer_cast<orion::clock::SimClock>(ctx.clock), nullptr);
}

TEST_F(ServiceBootstrapperFixture, ClockCoordinatedFlagCreatesCoordinatedClock)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 5>{
        "test-service", "--vehicle-id", "alpha", "--clock", "coordinated"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());
    EXPECT_NE(std::dynamic_pointer_cast<orion::clock::CoordinatedClock>(ctx.clock), nullptr);
}

// ── withOptions ───────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, ServiceSpecificOptionParsedViaWithOptions)
{
    auto bootstrap   = orion::app::ServiceBootstrapper{"test-service"};
    auto custom_flag = std::string{};
    bootstrap.withOptions(
        [&](CLI::App& app) { app.add_option("--custom", custom_flag, "custom option"); });

    auto args =
        std::array<const char*, 5>{"test-service", "--vehicle-id", "alpha", "--custom", "hello"};
    auto ctx = bootstrap.withLogger(nullLogger()).run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(custom_flag, "hello");
    EXPECT_EQ(ctx.vehicle_id, "alpha");
}
