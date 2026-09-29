#include "runtime.h"
#include <algorithm>
#include <cstring>

namespace wgpupixel {
Error::Error(ErrorCode code, std::string_view operation, std::string_view parameter,
             std::string_view message) noexcept
    : code_(code) {
    const auto copy = [](auto& destination, std::string_view source) {
        const auto count = std::min(source.size(), destination.size() - 1);
        if (count) {
            std::memcpy(destination.data(), source.data(), count);
        }
    };
    copy(operation_, operation);
    copy(parameter_, parameter);
    copy(message_, message);
}
ErrorCode Error::code() const noexcept {
    return code_;
}
std::string_view Error::operation() const noexcept {
    return operation_.data();
}
std::string_view Error::parameter() const noexcept {
    return parameter_.data();
}
const char* Error::what() const noexcept {
    return message_.data();
}
namespace detail {
[[noreturn]] void fail(ErrorCode code, std::string_view operation, std::string_view parameter,
                       std::string_view message) {
    throw Error(code, operation, parameter, message);
}
WGPUStringView sv(std::string_view value) noexcept {
    return {value.data(), value.size()};
}
} // namespace detail
} // namespace wgpupixel
