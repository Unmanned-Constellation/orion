#pragma once

#include <cstdint>
#include <vector>

#include "orion/perception/perception_backend.hpp"
#include "orion/v1/detection.pb.h"

namespace orion::perception
{

/// Test double for PerceptionBackend.
///
/// start() stores the callback, fires any scripted frames in order, then returns.
/// emit() fires the callback synchronously on the calling thread.
/// stop() clears the callback — subsequent emit() calls are no-ops.
class FakePerceptionBackend final : public PerceptionBackend
{
  public:
    struct ScriptedFrame
    {
        orion::v1::DetectionFrame frame{};
        uint64_t                  captured_at_ns{0};
    };

    explicit FakePerceptionBackend(std::vector<ScriptedFrame> script = {})
        : script_(std::move(script)), callback_{}
    {
    }

    void start(DetectionCallback callback) override
    {
        callback_ = std::move(callback);
        for (const auto& scripted_frame : script_)
        {
            callback_(scripted_frame.frame, scripted_frame.captured_at_ns);
        }
    }

    void stop() override { callback_ = nullptr; }

    void emit(orion::v1::DetectionFrame frame, uint64_t captured_at_ns)
    {
        if (callback_)
        {
            callback_(std::move(frame), captured_at_ns);
        }
    }

  private:
    std::vector<ScriptedFrame> script_;
    DetectionCallback          callback_;
};

} // namespace orion::perception
