#include <array>
#include <string>

#include <unistd.h>

#include <CLI/CLI.hpp>
#include <gtest/gtest.h>
#include <limits.h>
#include <spdlog/spdlog.h>

#include "orion/app/logger_factory.hpp"
#include "orion/app/service_bootstrapper.hpp"

namespace
{

struct ServiceBootstrapperFixture : ::testing::Test
{
    void TearDown() override { orion::app::LoggerFactory::shutdown(); }
};

} // namespace

// ── vehicle_id parsing ────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, VehicleIdDefaultsToHostname)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 1>{"test-service"};

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");
    auto ctx = bootstrap.run(static_cast<int>(args.size()), args.data());

    auto buf = std::array<char, HOST_NAME_MAX + 1>{};
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    gethostname(buf.data(), buf.size());
    EXPECT_EQ(ctx.vehicle_id, std::string{buf.data()});
}

TEST_F(ServiceBootstrapperFixture, VehicleIdFromFlag)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx       = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.vehicle_id, "alpha");
}

TEST_F(ServiceBootstrapperFixture, VehicleIdFromEnv)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 1>{"test-service"};

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("VEHICLE_ID", "bravo", 1);
    auto ctx = bootstrap.run(static_cast<int>(args.size()), args.data());
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
    auto ctx = bootstrap.run(static_cast<int>(args.size()), args.data());
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");

    EXPECT_EQ(ctx.vehicle_id, "alpha");
}

// ── latch ─────────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LatchIsNotStoppedAfterRun)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx       = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_FALSE(ctx.latch->stopped());
}

TEST_F(ServiceBootstrapperFixture, LatchRefIsBootstrappersLatch)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx       = bootstrap.run(static_cast<int>(args.size()), args.data());

    ctx.latch->stop();

    EXPECT_TRUE(ctx.latch->stopped());
}

// ── logger ────────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LogIsNonNull)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx       = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_NE(ctx.log, nullptr);
}

// ── log level ─────────────────────────────────────────────────────────────────

TEST_F(ServiceBootstrapperFixture, LogLevelDefaultsToInfo)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args      = std::array<const char*, 3>{"test-service", "--vehicle-id", "alpha"};
    auto ctx       = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.log->level(), spdlog::level::info);
}

TEST_F(ServiceBootstrapperFixture, LogLevelSetFromFlag)
{
    auto bootstrap = orion::app::ServiceBootstrapper{"test-service"};
    auto args =
        std::array<const char*, 5>{"test-service", "--vehicle-id", "alpha", "--log-level", "debug"};
    auto ctx = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(ctx.log->level(), spdlog::level::debug);
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
    auto ctx = bootstrap.run(static_cast<int>(args.size()), args.data());

    EXPECT_EQ(custom_flag, "hello");
    EXPECT_EQ(ctx.vehicle_id, "alpha");
}
