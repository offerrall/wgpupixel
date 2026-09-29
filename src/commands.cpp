#include "utils/commands.h"

#include <utility>

namespace wgpupixel {
using namespace detail;

Commands::Commands() noexcept = default;
Commands::~Commands() = default;
Commands::Commands(Commands&&) noexcept = default;
Commands& Commands::operator=(Commands&&) noexcept = default;
Commands::Commands(std::unique_ptr<detail::Recording> recording)
    : recording_(std::move(recording)) {}

} // namespace wgpupixel
