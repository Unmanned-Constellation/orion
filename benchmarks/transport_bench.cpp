#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <string>
#include <thread>

#include <benchmark/benchmark.h>

#include "orion/transport/config.hpp"
#include "orion/transport/message_header.hpp"
#include "orion/transport/publisher.hpp"
#include "orion/transport/session.hpp"
#include "orion/transport/subscriber.hpp"
#include "orion/v1/envelope.pb.h"

namespace
{

using orion::transport::MessageHeader;
using orion::transport::Session;
using orion::transport::SessionConfig;

constexpr int THROUGHPUT_BATCH = 1000;

auto nowNs() -> uint64_t
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch())
                                     .count());
}

void zenohLatency(benchmark::State& state)
{
    const auto payload_bytes = static_cast<size_t>(state.range(0));

    auto session = Session::create(SessionConfig{
        .vehicle_id   = "bench",
        .service_name = "transport",
    });

    auto recv_ns = std::atomic<uint64_t>{0}; // NOLINT(misc-const-correctness)
    auto sub     = session.subscribe<orion::v1::Envelope>(
        "orion/bench/transport/latency",
        [&](const orion::v1::Envelope& /*msg*/, const MessageHeader& /*hdr*/) {
            recv_ns.store(nowNs(), std::memory_order_release);
        });

    auto pub = session.advertise<orion::v1::Envelope>("orion/bench/transport/latency");
    auto msg = orion::v1::Envelope{};
    msg.set_payload(std::string(payload_bytes, '\0'));

    for (auto _ : state) // NOLINT(readability-identifier-length,clang-analyzer-deadcode.DeadStores)
    {
        recv_ns.store(0, std::memory_order_release);
        const auto send_ns = nowNs();
        pub.publish(msg, send_ns);

        while (recv_ns.load(std::memory_order_acquire) == 0)
        {
            std::this_thread::yield();
        }

        const auto latency_ns = recv_ns.load(std::memory_order_relaxed) - send_ns;
        state.SetIterationTime(static_cast<double>(latency_ns) * 1e-9);
    }

    state.SetBytesProcessed(state.iterations() * static_cast<int64_t>(payload_bytes));
}

void zenohThroughput(benchmark::State& state)
{
    const auto payload_bytes = static_cast<size_t>(state.range(0));

    auto session = Session::create(SessionConfig{
        .vehicle_id   = "bench",
        .service_name = "transport",
    });

    auto recv_count = std::atomic<int>{0}; // NOLINT(misc-const-correctness)
    auto sub        = session.subscribe<orion::v1::Envelope>(
        "orion/bench/transport/throughput",
        [&](const orion::v1::Envelope& /*msg*/, const MessageHeader& /*hdr*/) {
            recv_count.fetch_add(1, std::memory_order_relaxed);
        });

    auto pub = session.advertise<orion::v1::Envelope>("orion/bench/transport/throughput");
    auto msg = orion::v1::Envelope{};
    msg.set_payload(std::string(payload_bytes, '\0'));

    for (auto _ : state) // NOLINT(readability-identifier-length,clang-analyzer-deadcode.DeadStores)
    {
        recv_count.store(0, std::memory_order_release);
        const auto batch_start = std::chrono::steady_clock::now();

        for (auto i = 0; i < THROUGHPUT_BATCH; ++i)
        {
            pub.publish(msg, 0);
        }

        const auto deadline = batch_start + std::chrono::seconds{5};
        while (recv_count.load(std::memory_order_acquire) < THROUGHPUT_BATCH &&
               std::chrono::steady_clock::now() < deadline)
        {
            std::this_thread::yield();
        }

        const auto elapsed =
            std::chrono::duration<double>(std::chrono::steady_clock::now() - batch_start).count();
        state.SetIterationTime(elapsed);
    }

    state.SetItemsProcessed(state.iterations() * THROUGHPUT_BATCH);
    state.SetBytesProcessed(state.iterations() * THROUGHPUT_BATCH *
                            static_cast<int64_t>(payload_bytes));
}

} // namespace

BENCHMARK(
    zenohLatency) // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
    ->UseManualTime()
    ->Arg(16)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(65536)
    ->Unit(benchmark::kMicrosecond);

BENCHMARK(
    zenohThroughput) // NOLINT(cppcoreguidelines-avoid-non-const-global-variables,readability-identifier-naming)
    ->UseManualTime()
    ->Arg(16)
    ->Arg(256)
    ->Arg(1024)
    ->Arg(65536)
    ->Unit(benchmark::kMillisecond);
