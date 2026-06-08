#include <string>

#include <CLI/CLI.hpp>
#include <gtest/gtest.h>

#include "orion/app/service_config.hpp"

using orion::app::addServiceConfig;
using orion::app::ServiceConfig;

TEST(ServiceConfigTest, VehicleIdSetFromFlag)
{
    auto app = CLI::App{"test"};
    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    app.parse("--vehicle-id alpha");

    EXPECT_EQ(cfg.vehicle_id, "alpha");
}

TEST(ServiceConfigTest, LogLevelDefaultsToInfo)
{
    auto app = CLI::App{"test"};
    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    app.parse("--vehicle-id alpha");

    EXPECT_EQ(cfg.log_level, "info");
}

TEST(ServiceConfigTest, LogLevelSetFromFlag)
{
    auto app = CLI::App{"test"};
    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    app.parse("--vehicle-id alpha --log-level debug");

    EXPECT_EQ(cfg.log_level, "debug");
}

TEST(ServiceConfigTest, VehicleIdSetFromEnv)
{
    auto app = CLI::App{"test"};
    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("VEHICLE_ID", "bravo", 1);
    app.parse("");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");

    EXPECT_EQ(cfg.vehicle_id, "bravo");
}

TEST(ServiceConfigTest, FlagOverridesEnv)
{
    auto app = CLI::App{"test"};
    auto cfg = ServiceConfig{};
    addServiceConfig(app, cfg);

    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    setenv("VEHICLE_ID", "bravo", 1);
    app.parse("--vehicle-id alpha");
    // NOLINTNEXTLINE(concurrency-mt-unsafe)
    unsetenv("VEHICLE_ID");

    EXPECT_EQ(cfg.vehicle_id, "alpha");
}
