// SPDX-License-Identifier: GPL-3.0-only
// Derived in structure from maxajbarlow/AES67_macos_Driver commit a2bba622.
#include "Driver/LXToolDevice.hpp"

#include <aspl/Context.hpp>
#include <aspl/Driver.hpp>
#include <aspl/Plugin.hpp>
#include <memory>

namespace lxtool::aes67 {
class LXToolPlugin final : public aspl::Plugin {
public:
    explicit LXToolPlugin(const std::shared_ptr<aspl::Context>& context) : aspl::Plugin(context) {
        device_ = std::make_shared<LXToolDevice>(context);
        device_->initialize();
        AddDevice(device_);
    }
    std::string GetManufacturer() const override { return "maxpierr"; }
private:
    std::shared_ptr<LXToolDevice> device_;
};
}

extern "C" void* AESBridgePlugInFactory(CFAllocatorRef, CFUUIDRef requestedType) {
    if (!CFEqual(requestedType, kAudioServerPlugInTypeUUID)) return nullptr;
    try {
        auto context = std::make_shared<aspl::Context>();
        auto plugin = std::make_shared<lxtool::aes67::LXToolPlugin>(context);
        return (new aspl::Driver(context, plugin))->GetReference();
    } catch (...) { return nullptr; }
}
