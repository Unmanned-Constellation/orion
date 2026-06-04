#pragma once

#include <condition_variable>
#include <csignal>
#include <mutex>
#include <thread>

#include <pthread.h>

namespace orion::app
{

/// Blocks the main thread until SIGTERM or SIGINT is received, or until
/// stop() is called programmatically.
///
/// Construct once at the top of main() before spawning any other threads.
/// The constructor calls pthread_sigmask(SIG_BLOCK) on the calling thread;
/// all subsequently created threads inherit the blocked mask, ensuring that
/// SIGTERM and SIGINT are delivered exclusively to the internal watcher thread
/// via sigwait() rather than asynchronously interrupting arbitrary threads.
///
/// Usage:
/// @code
///   int main() {
///       orion::app::ShutdownLatch latch;
///       // ... construct session, service ...
///       latch.wait();  // blocks until SIGTERM/SIGINT or latch.stop()
///   }
/// @endcode
class ShutdownLatch
{
  public:
    ShutdownLatch()
    {
        sigemptyset(&mask_);
        sigaddset(&mask_, SIGINT);
        sigaddset(&mask_, SIGTERM);
        pthread_sigmask(SIG_BLOCK, &mask_, nullptr);

        watcher_ = std::thread([this] {
            auto sig = 0;
            sigwait(&mask_, &sig);
            stop();
        });
    }

    ~ShutdownLatch()
    {
        stop();
        if (watcher_.joinable())
        {
            watcher_.join();
        }
    }

    ShutdownLatch(const ShutdownLatch&)            = delete;
    ShutdownLatch& operator=(const ShutdownLatch&) = delete;
    ShutdownLatch(ShutdownLatch&&)                 = delete;
    ShutdownLatch& operator=(ShutdownLatch&&)      = delete;

    /// Blocks until stop() is called or a shutdown signal is received.
    /// @return Always true; present so callers can use the result in assertions.
    auto wait() const -> bool
    {
        std::unique_lock lock(mu_);
        cv_.wait(lock, [this] { return stopped_; });
        return true;
    }

    /// Returns true if shutdown has been requested.
    /// @return True once stop() has been called or a shutdown signal received.
    [[nodiscard]] auto stopped() const -> bool
    {
        std::lock_guard lock(mu_);
        return stopped_;
    }

    /// Triggers shutdown programmatically. Idempotent.
    void stop()
    {
        auto was_stopped = false;
        {
            std::lock_guard lock(mu_);
            was_stopped = stopped_;
            stopped_    = true;
        }
        cv_.notify_all();

        // If called from outside the watcher thread, kick the watcher out of
        // sigwait so the destructor's join() doesn't hang indefinitely.
        if (!was_stopped && watcher_.joinable() && std::this_thread::get_id() != watcher_.get_id())
        {
            auto sig = SIGTERM;
            pthread_kill(watcher_.native_handle(), sig);
        }
    }

  private:
    /// @brief Signal set containing SIGINT and SIGTERM.
    sigset_t mask_{};
    /// @brief Guards stopped_.
    mutable std::mutex mu_;
    /// @brief Notified by stop(); waited on by wait().
    mutable std::condition_variable cv_;
    /// @brief True once shutdown has been requested.
    bool stopped_{false};
    /// @brief Background thread that calls sigwait() and then stop().
    std::thread watcher_;
};

} // namespace orion::app
