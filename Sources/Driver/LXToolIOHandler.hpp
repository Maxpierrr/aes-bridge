// SPDX-License-Identifier: GPL-3.0-only
#pragma once

#include "Core/SharedAudioMemory.hpp"

#include <aspl/Client.hpp>
#include <aspl/IORequestHandler.hpp>
#include <aspl/Stream.hpp>
#include <atomic>
#include <memory>

namespace lxtool::aes67 {

class LXToolIOHandler final : public aspl::IORequestHandler {
public:
    explicit LXToolIOHandler(SharedAudioBlock* bridge) noexcept : bridge_(bridge) {}
    void attach(SharedAudioBlock* bridge) noexcept { bridge_.store(bridge, std::memory_order_release); }
    void OnReadClientInput(const std::shared_ptr<aspl::Client>&, const std::shared_ptr<aspl::Stream>&,
        Float64, Float64, void* bytes, UInt32 bytesCount) override;
    void OnWriteClientOutput(const std::shared_ptr<aspl::Client>&, const std::shared_ptr<aspl::Stream>&,
        Float64, Float64, const Float32* frames, UInt32 frameCount, UInt32 channelCount) override;
private:
    std::atomic<SharedAudioBlock*> bridge_;
};

} // namespace lxtool::aes67
