#include "netw/wire/frame.hpp"

#include <cstring>

#include "netw/api/codec.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::wire {

namespace {

constexpr int VARINT_MAX_BYTES = 5;

int varint_width(int64_t p_value) {
    int64_t value = p_value;
    for (int index = 0; index < VARINT_MAX_BYTES; ++index) {
        value >>= 7;
        if (value <= 0) {
            return index + 1;
        }
    }
    return VARINT_MAX_BYTES;
}

int put_varint(uint8_t *p_at, int64_t p_value) {
    int64_t value = p_value;
    for (int index = 0; index < VARINT_MAX_BYTES; ++index) {
        const uint8_t byte = uint8_t(value & 0x7f);
        value >>= 7;
        if (value > 0) {
            p_at[index] = uint8_t(byte | 0x80);
        } else {
            p_at[index] = byte;
            return index + 1;
        }
    }
    return VARINT_MAX_BYTES;
}

PackedByteArray path_prefixed(
    const PackedByteArray &p_payload,
    const String &p_path
) {
    const PackedByteArray path = p_path.to_utf8_buffer();
    const int length_width = varint_width(path.size());

    PackedByteArray out;
    out.resize(length_width + path.size() + p_payload.size());
    uint8_t *at = out.ptrw();
    at += put_varint(at, path.size());
    if (path.size() > 0) {
        memcpy(at, path.ptr(), size_t(path.size()));
        at += path.size();
    }
    if (p_payload.size() > 0) {
        memcpy(at, p_payload.ptr(), size_t(p_payload.size()));
    }
    return out;
}

int byte_count(int64_t p_value) {
    constexpr int64_t COUNT_CEILING = 0x7fffffff;
    if (p_value <= 0) {
        return 0;
    }
    return int(p_value < COUNT_CEILING ? p_value : COUNT_CEILING);
}

} // namespace

PackedByteArray frame_pack(
    int64_t p_route,
    uint8_t p_comp,
    uint8_t p_channel,
    const PackedByteArray &p_payload,
    const String &p_path
) {
    NETW_ZONE_NC("wire frame pack", colors::WIRE);

    const PackedByteArray body = p_comp == FRAME_COMP_PATH
        ? path_prefixed(p_payload, p_path)
        : p_payload;

    const int route_width = varint_width(p_route);
    const int length_width = varint_width(body.size());

    PackedByteArray out;
    out.resize(route_width + 2 + length_width + body.size());
    uint8_t *at = out.ptrw();
    at += put_varint(at, p_route);
    *at++ = p_comp;
    *at++ = p_channel;
    at += put_varint(at, body.size());
    if (body.size() > 0) {
        memcpy(at, body.ptr(), size_t(body.size()));
    }
    return out;
}

bool frame_unpack_next(
    const Ref<NetwBitBufferReader> &p_reader,
    Frame &r_frame
) {
    NETW_ZONE_NC("wire frame unpack next", colors::WIRE);

    NETW_ERR_COND_V(
        p_reader.is_null(),
        false,
        sys::WIRE,
        "a frame walk was handed no reader"
    );

    const int64_t route = NetwCodec::get_safe_varint(p_reader);
    if (route < 0) {
        NETW_TRACE(sys::WIRE, "a frame opened with a corrupt route varint");
        return false;
    }

    const uint8_t comp = uint8_t(p_reader->get_aligned_u8());
    const uint8_t channel = uint8_t(p_reader->get_aligned_u8());
    const int64_t body_length = NetwCodec::get_safe_varint(p_reader);
    if (body_length < 0) {
        NETW_TRACE(
            sys::WIRE,
            "route %d channel %d carried a corrupt body length varint",
            int(route),
            int(channel)
        );
        return false;
    }

    const PackedByteArray body
        = p_reader->get_aligned_bytes(byte_count(body_length));

    r_frame.route = route;
    r_frame.comp = comp;
    r_frame.channel = channel;
    r_frame.path = String();
    r_frame.payload = body;

    if (comp != FRAME_COMP_PATH) {
        return true;
    }

    const Ref<NetwBitBufferReader> body_reader
        = NetwBitBufferReader::create(body);
    const int64_t path_length = NetwCodec::get_safe_varint(body_reader);
    if (path_length < 0) {
        NETW_TRACE(
            sys::WIRE,
            "route %d carried a corrupt path length, so its body stays whole",
            int(route)
        );
        return true;
    }

    r_frame.path
        = gd::utf8_string(body_reader->get_aligned_bytes(
            byte_count(path_length)
        ));
    r_frame.payload
        = body_reader->get_aligned_bytes(body_reader->remaining_bytes());
    return true;
}

FrameWalk frame_unpack_all(
    const PackedByteArray &p_framed,
    int64_t p_from
) {
    NETW_ZONE_NC("wire frame unpack all", colors::WIRE);

    FrameWalk walk;
    NETW_ERR_COND_V(
        p_from < 0 || p_from > p_framed.size(),
        walk,
        sys::WIRE,
        "a frame walk started at %d of %d bytes",
        int(p_from),
        int(p_framed.size())
    );

    const Ref<NetwBitBufferReader> reader = NetwBitBufferReader::create(
        p_from == 0 ? p_framed : p_framed.slice(int(p_from), p_framed.size())
    );

    Frame frame;
    while (reader->remaining_bytes() > 0) {
        if (!frame_unpack_next(reader, frame)) {
            return walk;
        }
        walk.frames.push_back(frame);
    }
    walk.whole = reader->ok();
    return walk;
}

} // namespace netw::wire
