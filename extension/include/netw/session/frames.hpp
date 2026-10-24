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
    godot::String account;
    godot::String account_name;
    godot::String join_address;
    uint64_t max_players = 0;

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
        ),
        netw::wire::field<&AcceptFrame::account>(
            "account",
            netw::wire::string()
        ),
        netw::wire::field<&AcceptFrame::account_name>(
            "account_name",
            netw::wire::string()
        ),
        netw::wire::field<&AcceptFrame::join_address>(
            "join_address",
            netw::wire::string()
        ),
        netw::wire::field<&AcceptFrame::max_players>(
            "max_players",
            netw::wire::varuint(2)
        )
    );
};

AcceptFrame accept_seen_by(AcceptFrame row, int64_t destination);

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

struct SessionLeave {
    bool returning = false;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SessionLeave::returning>(
            "returning",
            netw::wire::bool1()
        )
    );
};

struct PlayerLeft {
    uint64_t player_id = 0;
    int64_t reason = 0;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&PlayerLeft::player_id>(
            "player_id",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&PlayerLeft::reason>(
            "reason",
            netw::wire::int_range(0, 3)
        )
    );
};

struct SpawnFailed {
    int64_t route = 0;
    godot::String reason;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&SpawnFailed::route>("route", netw::wire::varuint(5)),
        netw::wire::field<&SpawnFailed::reason>("reason", netw::wire::string())
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
    bool has_owner = false;
    bool kept = false;
    godot::PackedByteArray final_state;
    uint64_t peer = 0;
    uint64_t player_id = 0;

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
        netw::wire::field<&ControlApply::has_owner>(
            "has_owner",
            netw::wire::bool1()
        ),
        netw::wire::field<&ControlApply::kept>("kept", netw::wire::bool1()),
        netw::wire::field<&ControlApply::final_state>(
            "final_state",
            netw::wire::bytes_capped(CONTROL_FINAL_STATE_CAP)
        ),
        netw::wire::field<&ControlApply::peer>("peer", netw::wire::varuint(5)),
        netw::wire::field<&ControlApply::player_id>(
            "player_id",
            netw::wire::varuint(5)
        )
    );
};

struct ClaimImage {
    uint64_t observed_revision = 0;
    godot::PackedByteArray state;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ClaimImage::observed_revision>(
            "observed_revision",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ClaimImage::state>(
            "state",
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

struct ResyncHead {
    uint64_t route_floor = 0;
    uint64_t player_high = 0;
    uint64_t leases = 0;
    uint64_t routes = 0;
    bool last = false;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ResyncHead::route_floor>(
            "route_floor",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncHead::player_high>(
            "player_high",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncHead::leases>(
            "leases",
            netw::wire::varuint(1)
        ),
        netw::wire::field<&ResyncHead::routes>(
            "routes",
            netw::wire::varuint(3)
        ),
        netw::wire::field<&ResyncHead::last>("last", netw::wire::bool1())
    );
};

struct ResyncRoute {
    uint64_t route_delta = 0;
    uint64_t anchor = 0;
    uint64_t anchor_author = 0;
    uint64_t control_revision = 0;
    uint64_t epoch = 0;
    bool gone = false;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ResyncRoute::route_delta>(
            "route_delta",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncRoute::anchor>(
            "anchor",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncRoute::anchor_author>(
            "anchor_author",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncRoute::control_revision>(
            "control_revision",
            netw::wire::varuint(5)
        ),
        netw::wire::field<&ResyncRoute::epoch>("epoch", netw::wire::varuint(3)),
        netw::wire::field<&ResyncRoute::gone>("gone", netw::wire::bool1())
    );
};

struct Resync {
    ResyncHead head;
    godot::LocalVector<RouteLease> leases;
    godot::LocalVector<ResyncRoute> routes;
};

struct DenyKey {
    godot::StringName key;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&DenyKey::key>("key", netw::wire::string())
    );
};

struct ConfirmKey {
    godot::StringName key;

    static constexpr auto wire = netw::wire::describe(
        netw::wire::field<&ConfirmKey::key>("key", netw::wire::string())
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

constexpr uint64_t RESYNC_LEASES_MAX = 127;

godot::PackedByteArray resync_write(const Resync &resync);

bool resync_read(const godot::PackedByteArray &bytes, Resync &r_resync);

void resync_chunks(
    const ResyncHead &head,
    const godot::LocalVector<RouteLease> &leases,
    const godot::LocalVector<ResyncRoute> &rows,
    int64_t budget,
    godot::LocalVector<godot::PackedByteArray> &r_frames
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
