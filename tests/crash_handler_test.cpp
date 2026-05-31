#include <array>

#include <signal.h> // POSIX sigaction/sigset_t not available in <csignal>

#include <gtest/gtest.h>

#include "orion/app/crash_handler.hpp"

// Death tests involving crash signals must use threadsafe style to avoid
// collisions with GoogleTest's internal SIGABRT interceptor.
static const auto
    DEATH_TEST_STYLE_INIT = // NOLINT(cert-err58-cpp,cppcoreguidelines-interfaces-global-init)
    (::testing::FLAGS_gtest_death_test_style = "threadsafe", 0);

// ── Registration / restoration ────────────────────────────────────────────────

TEST(CrashHandlerTest, InstallsHandlerOnConstruction)
{
    struct sigaction act
    {
    };
    sigaction(SIGSEGV, nullptr, &act);
    // Capture the handler pointer before construction — typically SIG_DFL (nullptr).
    // Do NOT compare sa_flags: glibc silently injects SA_RESTORER (0x4000000) when
    // installing any handler via sigaction, so flags differ between the default and
    // restored states even when the handler itself is correctly restored.
    const auto before_handler = act.sa_handler;

    {
        const orion::app::CrashHandler crash;
        sigaction(SIGSEGV, nullptr, &act);
        EXPECT_TRUE((act.sa_flags & SA_SIGINFO) != 0);
        EXPECT_TRUE((act.sa_flags & SA_ONSTACK) != 0);
        EXPECT_TRUE((act.sa_flags & SA_RESETHAND) != 0);
        EXPECT_NE(act.sa_sigaction, nullptr);
    }

    // After destruction the previous handler pointer is restored.
    sigaction(SIGSEGV, nullptr, &act);
    EXPECT_EQ(act.sa_handler, before_handler);
}

TEST(CrashHandlerTest, RestoresAllSignalsOnDestruction)
{
    static constexpr std::array<int, 5> signals = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};

    // Capture handler pointers only — sa_flags cannot be compared reliably because
    // glibc injects SA_RESTORER when any handler is installed via sigaction.
    struct sigaction tmp
    {
    };
    auto before_handlers = std::array<decltype(tmp.sa_handler), 5>{};
    for (auto i = 0U; i < signals.size(); ++i)
    {
        sigaction(signals.at(i), nullptr, &tmp);
        before_handlers.at(i) = tmp.sa_handler;
    }

    {
        const orion::app::CrashHandler crash;
    }

    for (auto i = 0U; i < signals.size(); ++i)
    {
        struct sigaction after
        {
        };
        sigaction(signals.at(i), nullptr, &after);
        EXPECT_EQ(after.sa_handler, before_handlers.at(i))
            << "handler not restored for signal " << signals.at(i);
    }
}

// ── Per-signal handler registration ──────────────────────────────────────────
// Parameterized to avoid a for loop with multiple EXPECT calls, which pushes
// the cognitive complexity of a single TestBody over the project threshold.

class CrashHandlerAllSignalsTest : public ::testing::TestWithParam<int>
{
};

TEST_P(CrashHandlerAllSignalsTest, HandlerIsRegistered)
{
    const orion::app::CrashHandler crash;
    const auto                     sig = GetParam();
    struct sigaction act
    {
    };
    sigaction(sig, nullptr, &act);
    EXPECT_NE(act.sa_sigaction, nullptr) << "handler not installed for signal " << sig;
    EXPECT_TRUE((act.sa_flags & SA_SIGINFO) != 0) << "SA_SIGINFO missing for signal " << sig;
    EXPECT_TRUE((act.sa_flags & SA_ONSTACK) != 0) << "SA_ONSTACK missing for signal " << sig;
    EXPECT_TRUE((act.sa_flags & SA_RESETHAND) != 0) << "SA_RESETHAND missing for signal " << sig;
}

INSTANTIATE_TEST_SUITE_P(AllSignals,
                         CrashHandlerAllSignalsTest,
                         ::testing::Values(SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS));

// ── Single-instance enforcement ───────────────────────────────────────────────

TEST(CrashHandlerDeathTest, SecondInstanceAsserts)
{
    EXPECT_DEATH(
        {
            const orion::app::CrashHandler first;
            const orion::app::CrashHandler second; // must assert
        },
        "");
}

// ── Crash signal produces trace header on stderr ──────────────────────────────

TEST(CrashHandlerDeathTest, SigsegvPrintsTraceHeader)
{
    EXPECT_DEATH(
        {
            const orion::app::CrashHandler crash;
            // Dereference null to trigger a real SIGSEGV rather than raise() so
            // GoogleTest's internal SIGABRT interceptor is not involved.
            auto                  null_ptr = static_cast<volatile int*>(nullptr);
            [[maybe_unused]] auto dead     = *null_ptr;
        },
        "\\[CrashHandler\\] caught SIGSEGV - stack trace:");
}

TEST(CrashHandlerDeathTest, SigfpePrintsTraceHeader)
{
    EXPECT_DEATH(
        {
            const orion::app::CrashHandler crash;
            static_cast<void>(raise(SIGFPE)); // use SIGFPE rather than SIGABRT to avoid collision
        },                                    // with GoogleTest's internal SIGABRT interceptor
        "\\[CrashHandler\\] caught SIGFPE - stack trace:");
}

// ── Blocked signal assertion ──────────────────────────────────────────────────

TEST(CrashHandlerDeathTest, AssertsIfCrashSignalIsBlocked)
{
    EXPECT_DEATH(
        {
            auto mask = sigset_t{};
            sigemptyset(&mask);
            sigaddset(&mask, SIGSEGV);
            pthread_sigmask(SIG_BLOCK, &mask, nullptr);
            const orion::app::CrashHandler crash; // must assert
        },
        "");
}
