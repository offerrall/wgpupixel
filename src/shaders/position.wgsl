fn destination_coordinate(position: u32, offset: i32, extent: u32) -> u32 {
    if (offset < 0) {
        // This also handles INT32_MIN without signed negation overflow.
        let magnitude = u32(-(offset + 1)) + 1u;
        if (position < magnitude) {
            return extent;
        }
        return position - magnitude;
    }
    // Logical dimensions and offsets are limited to INT32_MAX, so this
    // addition fits in u32 even when the resulting coordinate is clipped.
    return position + u32(offset);
}

