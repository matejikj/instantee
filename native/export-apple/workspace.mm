#include "workspace.hpp"
#include "cf_owner.hpp"

#include <array>
#include <cstring>

#if !__has_feature(objc_arc)
#error "The Apple export adapter requires ARC"
#endif

namespace instantee::spike::apple {

std::expected<void, Error> check_workspace() {
  @autoreleasepool {
    constexpr std::array<UInt8, 4> bytes{0, 127, 128, 255};
    detail::CfOwner<CFDataRef> data{CFDataCreate(
        kCFAllocatorDefault, bytes.data(), static_cast<CFIndex>(bytes.size()))};
    if (!data) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "allocation_failed", "workspace",
                                   "Cannot create native data"});
    }
    if (CFDataGetLength(data.get()) != static_cast<CFIndex>(bytes.size()) ||
        std::memcmp(CFDataGetBytePtr(data.get()), bytes.data(), bytes.size()) !=
            0) {
      return std::unexpected(Error{ExitCode::export_failure,
                                   "native_data_mismatch", "workspace",
                                   "Native data roundtrip failed"});
    }
    return {};
  }
}

} // namespace instantee::spike::apple
