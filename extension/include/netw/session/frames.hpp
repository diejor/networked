#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/stream.hpp"

namespace netw::session {

struct AcceptFrame {
    int64_t peer_id = 0;
    godot::StringName username;
    uint64_t player_id = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&AcceptFrame::peer_id>(
            "peer_id",
            netw::wire::svarint(5)
        ),
        netw::wire::field<&AcceptFrame::username>(
            "username",
            netw::wire::string()
        ),
        netw::wire::field<&AcceptFrame::player_id>(
            "player_id",
            netw::wire::varuint(5)
        )
    );
};

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
    int64_t source_route = 0;
    int64_t source_epoch = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneRequest::request_id>(
            "request_id",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneRequest::path>("path", netw::wire::string()),
        netw::wire::field<&SceneRequest::scope>("scope", netw::wire::svarint(2)),
        netw::wire::field<&SceneRequest::source_route>(
            "source_route",
            netw::wire::svarint(4)
        ),
        netw::wire::field<&SceneRequest::source_epoch>(
            "source_epoch",
            netw::wire::svarint(2)
        )
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

struct SceneViewersHead {
    int64_t route = 0;
    uint64_t epoch = 0;
    uint64_t generation = 0;
    uint64_t revision = 0;
    uint64_t count = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneViewersHead::route>(
            "route",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneViewersHead::epoch>(
            "epoch",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneViewersHead::generation>(
            "generation",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneViewersHead::revision>(
            "revision",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&SceneViewersHead::count>(
            "count",
            netw::wire::varuint(2)
        )
    );
};

struct SceneViewerRow {
    uint64_t player_id = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SceneViewerRow::player_id>(
            "player_id",
            netw::wire::varuint(5)
        )
    );
};

constexpr int CONTROL_FINAL_STATE_CAP = 1024;

struct ControlRequest {
    uint64_t op = 0;
    uint64_t observed_revision = 0;
    uint64_t issued_tick = 0;
    uint64_t source_route = 0;
    uint64_t successor = 0;
    uint8_t kind = 0;
    uint8_t hold = 0;
    godot::PackedByteArray final_state;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ControlRequest::op>("op", netw::wire::varuint(5)),
        netw::wire::field<&ControlRequest::observed_revision>(
            "observed_revision",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlRequest::issued_tick>(
            "issued_tick",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlRequest::source_route>(
            "source_route",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlRequest::successor>(
            "successor",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlRequest::kind>("kind", netw::wire::bits(1)),
        netw::wire::field<&ControlRequest::hold>("hold", netw::wire::bits(2)),
        netw::wire::field<&ControlRequest::final_state>(
            "final_state",
            netw::wire::bytes_capped(CONTROL_FINAL_STATE_CAP)
        )
    );
};

struct ControlApply {
    uint64_t controller = 0;
    uint64_t revision = 0;
    uint64_t op = 0;
    bool tenure_changed = false;
    uint8_t hold = 0;
    uint8_t outcome = 0;
    godot::PackedByteArray final_state;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ControlApply::controller>(
            "controller",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlApply::revision>(
            "revision",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ControlApply::op>("op", netw::wire::varuint(5)),
        netw::wire::field<&ControlApply::tenure_changed>(
            "tenure_changed",
            netw::wire::bool1()
        ),
        netw::wire::field<&ControlApply::hold>("hold", netw::wire::bits(2)),
        netw::wire::field<&ControlApply::outcome>(
            "outcome",
            netw::wire::bits(2)
        ),
        netw::wire::field<&ControlApply::final_state>(
            "final_state",
            netw::wire::bytes_capped(CONTROL_FINAL_STATE_CAP)
        )
    );
};

struct RouteLease {
    uint64_t base = 0;
    uint64_t count = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&RouteLease::base>("base", netw::wire::varuint(5)),
        netw::wire::field<&RouteLease::count>("count", netw::wire::varuint(2))
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

constexpr uint64_t VIEWERS_MAX = 1023;

godot::PackedByteArray viewers_write(
    const SceneViewersHead &head,
    const godot::LocalVector<uint64_t> &members
);

bool viewers_read(
    const godot::PackedByteArray &bytes,
    SceneViewersHead &r_head,
    godot::LocalVector<uint64_t> &r_members
);

godot::Dictionary frame_spec_records();

} // namespace netw::session
