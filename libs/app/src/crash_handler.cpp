#include <array>
#include <atomic>
#include <cstddef>
#include <cstdlib>
#include <string_view>

#include <signal.h> // POSIX sigaction/sigset_t not available in <csignal>
#include <sys/mman.h>
#include <unistd.h>

// Pull in the backward-cpp implementation in this translation unit only.
// Compile definitions are injected by the Backward::Backward CMake target.
#include <backward.hpp>

#include "orion/app/crash_handler.hpp"

namespace orion::app
{

namespace
{

// NOLINTBEGIN(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<bool> g_instance_active{false};
// NOLINTEND(cppcoreguidelines-avoid-non-const-global-variables)

// Number of internal frames to skip so the first printed frame is the fault site.
// Covers: load_here(), handleSignal(), and the kernel signal trampoline.
// Verified by inspection; adjust if compiler optimization changes the frame count.
constexpr std::size_t HANDLER_FRAME_SKIP = 3;

// Async-signal-safe signal name lookup. strsignal() is not async-signal-safe
// and returns descriptions ("Segmentation fault"), not macro names ("SIGSEGV").
constexpr std::array<int, 5>         SIGNAL_NUMBERS = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
constexpr std::array<const char*, 5> SIGNAL_NAMES   = {
    "SIGSEGV", "SIGABRT", "SIGFPE", "SIGILL", "SIGBUS"};

auto signalName(int signo) -> const char*
{
    for (auto i = 0U; i < SIGNAL_NUMBERS.size(); ++i)
    {
        if (SIGNAL_NUMBERS.at(i) ==
            signo) // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        {
            return SIGNAL_NAMES.at(i); // NOLINT(cppcoreguidelines-pro-bounds-constant-array-index)
        }
    }
    return "UNKNOWN";
}

} // namespace

CrashHandler::CrashHandler()
{
    if (g_instance_active.exchange(true))
    {
        write(STDERR_FILENO, "CrashHandler: only one instance allowed per process\n", 52);
        std::abort();
    }

    // Crash signals must not be blocked — a blocked synchronous fault signal
    // produces undefined behaviour (silent hang or lost signal).
    auto current_mask = sigset_t{}; // NOLINT(misc-include-cleaner)
    pthread_sigmask(SIG_BLOCK, nullptr, &current_mask);
    for (auto sig : SIGNALS)
    {
        if (sigismember(&current_mask, sig) != 0)
        {
            write(STDERR_FILENO,
                  "CrashHandler: a crash signal is blocked on the calling thread\n",
                  62);
            std::abort();
        }
    }

    setupAltStack();
    registerHandlers();
}

void CrashHandler::setupAltStack()
{
    const auto page_size = static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
    if (STACK_SIZE % page_size != 0)
    {
        write(STDERR_FILENO,
              "CrashHandler: STACK_SIZE must be a multiple of the system page size\n",
              68);
        std::abort();
    }

    alt_stack_total_size_ = STACK_SIZE + page_size;
    alt_stack_base_       = mmap(
        nullptr, alt_stack_total_size_, PROT_READ | PROT_WRITE, MAP_ANONYMOUS | MAP_PRIVATE, -1, 0);
    if (alt_stack_base_ == MAP_FAILED)
    {
        write(STDERR_FILENO, "CrashHandler: mmap for alternate signal stack failed\n", 53);
        std::abort();
    }

    // Guard page: the last page of the allocation.
    // NOLINTBEGIN(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto* guard_page = static_cast<std::byte*>(alt_stack_base_) + STACK_SIZE;
    // NOLINTEND(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    mprotect(guard_page, page_size, PROT_NONE);

    auto alt_stack     = stack_t{}; // NOLINT(misc-include-cleaner)
    alt_stack.ss_sp    = alt_stack_base_;
    alt_stack.ss_size  = STACK_SIZE;
    alt_stack.ss_flags = 0;
    sigaltstack(&alt_stack, nullptr);
}

void CrashHandler::registerHandlers()
{
    struct sigaction action
    {
    };
    action.sa_sigaction = &CrashHandler::handleSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;

    for (auto i = 0U; i < SIGNALS.size(); ++i)
    {
        sigaction(SIGNALS.at(i), &action, &old_actions_.at(i));
    }
}

CrashHandler::~CrashHandler()
{
    for (auto i = 0U; i < SIGNALS.size(); ++i)
    {
        sigaction(SIGNALS.at(i), &old_actions_.at(i), nullptr);
    }

    auto disabled     = stack_t{};
    disabled.ss_flags = SS_DISABLE;
    sigaltstack(&disabled, nullptr);

    munmap(alt_stack_base_, alt_stack_total_size_);

    g_instance_active.store(false);
}

void CrashHandler::handleSignal(int signo,
                                siginfo_t* /*info*/, // NOLINT(misc-include-cleaner)
                                void* context)
{
    // write() is async-signal-safe; fprintf/std::cerr are not.
    static constexpr auto prefix = std::string_view{"[CrashHandler] caught "};
    static constexpr auto suffix = std::string_view{" - stack trace:\n"};
    write(STDERR_FILENO, prefix.data(), prefix.size());
    const auto* sig_name = signalName(signo);
    write(STDERR_FILENO, sig_name, __builtin_strlen(sig_name));
    write(STDERR_FILENO, suffix.data(), suffix.size());

    auto trace = backward::StackTrace{};
    trace.load_here(64, context);
    trace.skip_n_firsts(HANDLER_FRAME_SKIP);

    auto printer       = backward::Printer{};
    printer.color_mode = backward::ColorMode::never;
    printer.snippet    = false;
    printer.print(trace, stderr);

    // SA_RESETHAND has already reset the disposition to SIG_DFL.
    // Re-raise delivers the default action (core dump or signal-exit).
    static_cast<void>(raise(signo)); // NOLINT(bugprone-unused-return-value)
    _exit(1);                        // fallback if raise() returns
}

} // namespace orion::app
