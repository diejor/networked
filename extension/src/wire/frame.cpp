#include "netw/wire/frame.hpp"

#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"
#include "netw/wire/describe.hpp"

using namespace godot;

namespace netw::wire {

namespace {

constexpr int PATH_CAP = 1023;

struct FrameHeader {
    uint64_t route = 0;
    uint8_t comp = 0;
    uint8_t channel = 0;
    uint64_t length = 0;

    static constexpr auto wire = describe(
        field<&FrameHeader::route>("route", varuint(5)),
        field<&FrameHeader::comp>("comp", bits(8)),
        field<&FrameHeader::channel>("channel", bits(8)),
        field<&FrameHeader::length>("length", varuint(3))
    );
};

bool path_body(
    const PackedByteArray &p_payload,
    const String &p_path,
    PackedByteArray &r_body
) {
    WriteStream stream;
    PackedByteArray utf8 = p_path.to_utf8_buffer();
    if (!stream.bytes_capped(utf8, PATH_CAP)) {
        return false;
    }
    r_body = stream.to_bytes();
    r_body.append_array(p_payload);
    return true;
}

bool split_path_body(const PackedByteArray &p_body, Frame &r_frame) {
    ReadStream stream(p_body);
    PackedByteArray utf8;
    if (!stream.bytes_capped(utf8, PATH_CAP)) {
        return false;
    }
    PackedByteArray inner;
    if (!stream.raw_bytes(inner, stream.bits_remaining() / 8)) {
        return false;
    }
    r_frame.path = gd::utf8_string(utf8);
    r_frame.payload = inner;
    return true;
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

    if (p_route < 0) {
        NETW_TRACE(
            sys::WIRE,
            "channel %d asked for a frame at route %d, which no varuint "
            "spells",
            int(p_channel),
            int(p_route)
        );
        return PackedByteArray();
    }

    PackedByteArray body;
    if (p_comp == FRAME_COMP_PATH) {
        if (!path_body(p_payload, p_path, body)) {
            return PackedByteArray();
        }
    } else {
        body = p_payload;
    }

    FrameHeader header;
    header.route = uint64_t(p_route);
    header.comp = p_comp;
    header.channel = p_channel;
    header.length = uint64_t(body.size());

    WriteStream stream;
    if (!FrameHeader::wire.run(stream, header)) {
        return PackedByteArray();
    }

    PackedByteArray out = stream.to_bytes();
    out.append_array(body);
    return out;
}

bool frame_unpack_next(ReadStream &p_stream, Frame &r_frame) {
    NETW_ZONE_NC("wire frame unpack next", colors::WIRE);

    FrameHeader header;
    if (!FrameHeader::wire.run(p_stream, header)) {
        NETW_TRACE(sys::WIRE, "a frame opened with a header it cannot hold");
        return false;
    }

    PackedByteArray body;
    if (!p_stream.raw_bytes(body, int64_t(header.length))) {
        NETW_TRACE(
            sys::WIRE,
            "route %d channel %d claimed %d payload bytes the datagram does "
            "not hold",
            int(header.route),
            int(header.channel),
            int(header.length)
        );
        return false;
    }

    r_frame.route = int64_t(header.route);
    r_frame.comp = header.comp;
    r_frame.channel = header.channel;
    r_frame.path = String();
    r_frame.payload = body;

    if (header.comp != FRAME_COMP_PATH) {
        return true;
    }
    if (!split_path_body(body, r_frame)) {
        NETW_TRACE(
            sys::WIRE,
            "route %d carried a path its own body cannot hold",
            int(header.route)
        );
        return false;
    }
    return true;
}

FrameWalk frame_unpack_all(const PackedByteArray &p_framed, int64_t p_from) {
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

    ReadStream stream(
        p_from == 0 ? p_framed : p_framed.slice(int(p_from), p_framed.size())
    );

    Frame frame;
    while (stream.bits_remaining() > 0) {
        if (!frame_unpack_next(stream, frame)) {
            return walk;
        }
        walk.frames.push_back(frame);
    }
    walk.whole = stream.ok();
    return walk;
}

Dictionary frame_spec_records() {
    Dictionary out;
    out["FrameHeader"] = FrameHeader::wire.spec_dump();
    return out;
}

} // namespace netw::wire
