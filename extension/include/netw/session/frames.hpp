#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::session {

struct ClockRate {
    uint64_t tickrate = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ClockRate::tickrate>(
            "tickrate",
            netw::wire::varuint(2)
        )
    );
};

struct ClockPing {
    uint64_t origin = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ClockPing::origin>("origin", netw::wire::bits(32))
    );
};

struct ClockPong {
    uint64_t origin = 0;
    uint64_t tick = 0;
    uint64_t phase = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ClockPong::origin>("origin", netw::wire::bits(32)),
        netw::wire::field<&ClockPong::tick>("tick", netw::wire::bits(32)),
        netw::wire::field<&ClockPong::phase>("phase", netw::wire::bits(8))
    );
};

struct ActionRequest {
    godot::StringName method;
    int64_t view_tick = 0;
    godot::PackedByteArray data;
    godot::StringName key;
    int64_t timing = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ActionRequest::method>(
            "method",
            netw::wire::string()
        ),
        netw::wire::field<&ActionRequest::view_tick>(
            "view_tick",
            netw::wire::svarint(5)
        ),
        netw::wire::field<&ActionRequest::data>(
            "data",
            netw::wire::bytes_capped(4095)
        ),
        netw::wire::field<&ActionRequest::key>("key", netw::wire::string()),
        netw::wire::field<&ActionRequest::timing>(
            "timing",
            netw::wire::int_range(0, 3)
        )
    );
};

struct SessionReason {
    godot::String reason;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SessionReason::reason>(
            "reason",
            netw::wire::string()
        )
    );
};

struct KickRequest {
    int64_t peer = 0;
    godot::String reason;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&KickRequest::peer>("peer", netw::wire::svarint(5)),
        netw::wire::field<&KickRequest::reason>("reason", netw::wire::string())
    );
};

struct SceneRequest {
    uint64_t request_id = 0;
    godot::String path;
    int64_t scope = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneRequest::request_id>(
            "request_id",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneRequest::path>("path", netw::wire::string()),
        netw::wire::field<&SceneRequest::scope>("scope", netw::wire::svarint(2))
    );
};

struct SceneResult {
    uint64_t request_id = 0;
    int64_t code = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneResult::request_id>(
            "request_id",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneResult::code>("code", netw::wire::svarint(5))
    );
};

struct SceneReleased {
    int64_t route = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneReleased::route>(
            "route",
            netw::wire::varuint(5)
        )
    );
};

struct SceneSeat {
    int64_t route = 0;
    int64_t peer = 0;
    bool present = false;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneSeat::route>("route", netw::wire::varuint(5)),
        netw::wire::field<&SceneSeat::peer>("peer", netw::wire::svarint(5)),
        netw::wire::field<&SceneSeat::present>("present", netw::wire::bool1())
    );
};

struct ControlApply {
    uint64_t controller = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ControlApply::controller>(
            "controller",
            netw::wire::varuint(5)
        )
    );
};

struct DenyKey {
    godot::StringName key;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&DenyKey::key>("key", netw::wire::string())
    );
};

template <class Record>
godot::PackedByteArray frame_write(const Record &p_record) {
    netw::wire::WriteStream stream;
    Record staged = p_record;
    if (!Record::wire.run(stream, staged) || !stream.align_verify()) {
        return godot::PackedByteArray();
    }
    return stream.to_bytes();
}

template <class Record>
bool frame_read(const godot::PackedByteArray &p_bytes, Record &r_record) {
    netw::wire::ReadStream stream(p_bytes);
    if (!Record::wire.run(stream, r_record) || !stream.align_verify()) {
        return false;
    }
    return stream.bits_remaining() == 0;
}

godot::PackedByteArray roster_write(
    const godot::LocalVector<AcceptFrame> &rows
);

bool roster_read(
    const godot::PackedByteArray &bytes,
    godot::LocalVector<AcceptFrame> &r_rows
);

godot::Dictionary frame_spec_records();

} // namespace netw::session
