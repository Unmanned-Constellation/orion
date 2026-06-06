#pragma once

#include <array>
#include <cstddef>

#include <signal.h> // POSIX sigaction/siginfo_t not available in <csignal>

namespace orion::app
{

/// RAII crash handler that prints a symbolized stack trace to stderr on
/// SIGSEGV, SIGABRT, SIGFPE, SIGILL, or SIGBUS before the process exits.
///
/// Registers signal actions at construction and restores the previous handlers
/// at destruction, making it composable in test harnesses.
///
/// Construct immediately after ShutdownLatch in main():
/// @code
///   int main() {
///       orion::app::ShutdownLatch latch;  // blocks SIGINT/SIGTERM first
///       orion::app::CrashHandler  crash;  // registers crash signal actions
///       // ...
///   }
/// @endcode
///
/// At most one instance may exist per process. A second construction asserts
/// false. The alternate signal stack is allocated via mmap (bypasses the heap
/// allocator, which may itself be corrupt at crash time).
class CrashHandler
{
  public:
    /// Allocates the alternate signal stack, registers crash signal actions,
    /// and saves the previous handlers for restoration at destruction.
    ///
    /// Asserts that no crash signal is currently blocked on the calling thread
    /// (blocking a synchronous fault signal produces undefined behaviour).
    /// Asserts that no other CrashHandler instance exists in this process.
    CrashHandler();

    /// Restores previous signal handlers and releases the alternate stack.
    ~CrashHandler();

    CrashHandler(const CrashHandler&)   = delete;
    auto operator=(const CrashHandler&) = delete;
    CrashHandler(CrashHandler&&)        = delete;
    auto operator=(CrashHandler&&)      = delete;

  private:
    /// @brief SA_SIGINFO handler — runs on the alternate stack.
    /// @param signo    Signal number received.
    /// @param info     Signal metadata provided by the kernel.
    /// @param context  Opaque CPU context pointer (unused).
    static void handleSignal(int signo, siginfo_t* info, void* context);

    /// @brief Allocates and registers the alternate signal stack via mmap.
    void setupAltStack();
    /// @brief Installs SA_SIGINFO actions for each signal in SIGNALS.
    void registerHandlers();

    /// @brief Crash signals handled by this handler.
    static constexpr std::array<int, 5> SIGNALS = {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS};
    /// @brief Size of the alternate signal stack in bytes.
    static constexpr std::size_t STACK_SIZE = 128UL * 1024UL; // 128 KB

    /// @brief Base address of the mmap-allocated alternate stack region.
    void* alt_stack_base_{nullptr};
    /// @brief Total size of the mmap region, including the guard page.
    std::size_t alt_stack_total_size_{0};
    /// @brief Previous signal actions, restored in the destructor.
    std::array<struct sigaction, 5> old_actions_{};
};

} // namespace orion::app
