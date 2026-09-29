#pragma once
#include "wgpupixel.h"

namespace wgpupixel::detail {
struct ShadowGeometry {
    struct {
        ImageSize destination, shadow;
    } requirements;
    Position source_position, shadow_position;
};
ShadowGeometry shadow_geometry(ImageSize source, const DropShadowOptions& options,
                               std::string_view operation);
} // namespace wgpupixel::detail
