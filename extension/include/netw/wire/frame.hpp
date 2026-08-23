#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/bit_buffer.hpp"

namespace netw::wire {

constexpr uint8_t FRAME_COMP_PATH = 255;

struct Frame {
    int64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    godot::String path;
    godot::PackedByteArray payload;
};

struct FrameWalk {
    godot::LocalVector<Frame> frames;
    bool whole = false;
};

godot::PackedByteArray frame_pack(
    int64_t p_route,
    uint8_t p_comp,
    uint8_t p_channel,
    const godot::PackedByteArray &p_payload,
    const godot::String &p_path
);

bool frame_unpack_next(
    const godot::Ref<NetwBitBufferReader> &p_reader,
    Frame &r_frame
);

FrameWalk frame_unpack_all(
    const godot::PackedByteArray &p_framed,
    int64_t p_from
);

} // namespace netw::wire
