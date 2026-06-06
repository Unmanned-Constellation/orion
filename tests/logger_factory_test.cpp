#include <sstream>
#include <string>

#include <gtest/gtest.h>
#include <spdlog/sinks/ostream_sink.h>
#include <spdlog/spdlog.h>

#include "orion/app/logger_factory.hpp"

// Each test reinitialises LoggerFactory with a fresh ostream sink so output
// is synchronous and deterministic — no async thread-pool timing to contend with.

namespace
{

// Initialise LoggerFactory with a synchronous ostream sink for the duration
// of a test. Tears down on destruction so the next test gets a clean slate.
struct LoggerFactoryFixture : ::testing::Test
{
    std::ostringstream                              buf;
    std::shared_ptr<spdlog::sinks::ostream_sink_st> sink;

    void SetUp() override
    {
        sink = std::make_shared<spdlog::sinks::ostream_sink_st>(buf);
        orion::app::LoggerFactory::initForTest(sink);
    }

    void TearDown() override { orion::app::LoggerFactory::shutdown(); }

    auto output() const -> std::string { return buf.str(); }
};

} // namespace

// ── Init / get ────────────────────────────────────────────────────────────────

TEST_F(LoggerFactoryFixture, GetReturnsNonNull)
{
    auto log = orion::app::LoggerFactory::get("test.foo");
    EXPECT_NE(log, nullptr);
}

TEST_F(LoggerFactoryFixture, GetReturnsSamePointerForSameName)
{
    auto lhs = orion::app::LoggerFactory::get("test.foo");
    auto rhs = orion::app::LoggerFactory::get("test.foo");
    EXPECT_EQ(lhs, rhs);
}

// ── Hierarchy ─────────────────────────────────────────────────────────────────

TEST_F(LoggerFactoryFixture, DistinctNamesProduceDistinctLoggers)
{
    auto parent = orion::app::LoggerFactory::get("test.foo");
    auto child  = orion::app::LoggerFactory::get("test.foo.bar");
    EXPECT_NE(parent, child);
}

TEST_F(LoggerFactoryFixture, LoggerNameAppearsInOutput)
{
    auto log = orion::app::LoggerFactory::get("test.foo.bar");
    log->info("hello");
    orion::app::LoggerFactory::flush();
    EXPECT_NE(output().find("test.foo.bar"), std::string::npos);
}

// ── Level propagation ─────────────────────────────────────────────────────────

TEST_F(LoggerFactoryFixture, InfoSuppressedAfterSetLevelWarn)
{
    auto log = orion::app::LoggerFactory::get("test.level");
    orion::app::LoggerFactory::setLevel(spdlog::level::warn);
    log->info("should not appear");
    orion::app::LoggerFactory::flush();
    EXPECT_EQ(output().find("should not appear"), std::string::npos);
}

TEST_F(LoggerFactoryFixture, WarnAppearsAfterSetLevelWarn)
{
    auto log = orion::app::LoggerFactory::get("test.level");
    orion::app::LoggerFactory::setLevel(spdlog::level::warn);
    log->warn("should appear");
    orion::app::LoggerFactory::flush();
    EXPECT_NE(output().find("should appear"), std::string::npos);
}

// ── Pre-init death test ───────────────────────────────────────────────────────

TEST(LoggerFactoryDeathTest, GetBeforeInitAsserts)
{
    // No fixture — factory is not initialised.
    EXPECT_DEATH(orion::app::LoggerFactory::get("test.uninit"), "");
}

// ── Flush ─────────────────────────────────────────────────────────────────────

TEST_F(LoggerFactoryFixture, MessagesAppearAfterFlush)
{
    auto log = orion::app::LoggerFactory::get("test.flush");
    log->info("flushed message");
    orion::app::LoggerFactory::flush();
    EXPECT_NE(output().find("flushed message"), std::string::npos);
}

// ── Name injection ────────────────────────────────────────────────────────────

TEST_F(LoggerFactoryFixture, InjectedNameAppearsInOutput)
{
    // Simulates a component receiving its name at construction and logging under it.
    const std::string injected = "autonomy.stabilizer.pid";
    auto              log      = orion::app::LoggerFactory::get(injected);
    log->info("derivative term saturated");
    orion::app::LoggerFactory::flush();
    EXPECT_NE(output().find(injected), std::string::npos);
}
