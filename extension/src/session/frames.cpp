#include "netw/session/frames.hpp"

#include "netw/api/join_request.hpp"

using namespace godot;

namespace netw::session {

namespace {

constexpr int ROSTER_COUNT_BYTES = 2;
constexpr int64_t RESYNC_HEAD_BYTES = 16;
constexpr int64_t RESYNC_LEASE_BYTES = 7;

} // namespace

PackedByteArray roster_write(const LocalVector<AcceptFrame> &p_rows) {
    wire::WriteStream stream;
    uint64_t count = uint64_t(p_rows.size());
    if (!stream.varuint(count, ROSTER_COUNT_BYTES)) {
        return PackedByteArray();
    }
    for (uint32_t at = 0; at < p_rows.size(); ++at) {
        AcceptFrame row = p_rows[at];
        if (!AcceptFrame::wire.run(stream, row)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool roster_read(
    const PackedByteArray &p_bytes,
    LocalVector<AcceptFrame> &r_rows
) {
    wire::ReadStream stream(p_bytes);
    uint64_t count = 0;
    if (!stream.varuint(count, ROSTER_COUNT_BYTES)) {
        return false;
    }
    LocalVector<AcceptFrame> staged;
    staged.reserve(uint32_t(count));
    for (uint64_t at = 0; at < count; ++at) {
        AcceptFrame row;
        if (!AcceptFrame::wire.run(stream, row)) {
            return false;
        }
        staged.push_back(row);
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        return false;
    }
    r_rows = staged;
    return true;
}

PackedByteArray resync_write(const Resync &p_resync) {
    if (uint64_t(p_resync.leases.size()) > RESYNC_LEASES_MAX) {
        return PackedByteArray();
    }
    wire::WriteStream stream;
    ResyncHead head = p_resync.head;
    head.leases = uint64_t(p_resync.leases.size());
    head.routes = uint64_t(p_resync.routes.size());
    if (!ResyncHead::wire.run(stream, head)) {
        return PackedByteArray();
    }
    for (uint32_t at = 0; at < p_resync.leases.size(); ++at) {
        RouteLease lease = p_resync.leases[at];
        if (!RouteLease::wire.run(stream, lease)) {
            return PackedByteArray();
        }
    }
    for (uint32_t at = 0; at < p_resync.routes.size(); ++at) {
        ResyncRoute route = p_resync.routes[at];
        if (!ResyncRoute::wire.run(stream, route)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool resync_read(const PackedByteArray &p_bytes, Resync &r_resync) {
    wire::ReadStream stream(p_bytes);
    Resync staged;
    if (!ResyncHead::wire.run(stream, staged.head)
        || staged.head.leases > RESYNC_LEASES_MAX) {
        return false;
    }
    for (uint64_t at = 0; at < staged.head.leases; ++at) {
        RouteLease lease;
        if (!RouteLease::wire.run(stream, lease)) {
            return false;
        }
        staged.leases.push_back(lease);
    }
    for (uint64_t at = 0; at < staged.head.routes; ++at) {
        ResyncRoute route;
        if (!ResyncRoute::wire.run(stream, route) || route.route_delta == 0) {
            return false;
        }
        staged.routes.push_back(route);
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        return false;
    }
    r_resync.head = staged.head;
    r_resync.leases = staged.leases;
    r_resync.routes = staged.routes;
    return true;
}

void resync_chunks(
    const ResyncHead &p_head,
    const LocalVector<RouteLease> &p_leases,
    const LocalVector<ResyncRoute> &p_rows,
    int64_t p_budget,
    LocalVector<PackedByteArray> &r_frames
) {
    wire::WriteStream scratch;
    uint32_t at = 0;
    uint64_t absolute = 0;
    do {
        Resync chunk;
        chunk.head = p_head;
        int64_t size = RESYNC_HEAD_BYTES;
        if (r_frames.is_empty()) {
            for (uint32_t lease = 0; lease < p_leases.size(); ++lease) {
                chunk.leases.push_back(p_leases[lease]);
            }
            size += int64_t(p_leases.size()) * RESYNC_LEASE_BYTES;
        }
        while (at < p_rows.size()) {
            ResyncRoute row = p_rows[at];
            const uint64_t next = absolute + row.route_delta;
            if (chunk.routes.is_empty()) {
                row.route_delta = next;
            }
            scratch.clear();
            ResyncRoute::wire.run(scratch, row);
            const int64_t cost = int64_t(scratch.byte_count());
            if (!chunk.routes.is_empty() && size + cost > p_budget) {
                break;
            }
            size += cost;
            chunk.routes.push_back(row);
            absolute = next;
            at += 1;
        }
        chunk.head.last = at >= p_rows.size();
        r_frames.push_back(resync_write(chunk));
    } while (at < p_rows.size());
}

PackedByteArray viewers_write(
    const SceneViewersHead &p_head,
    const LocalVector<uint64_t> &p_members
) {
    if (uint64_t(p_members.size()) > VIEWERS_MAX) {
        return PackedByteArray();
    }
    wire::WriteStream stream;
    SceneViewersHead head = p_head;
    head.count = uint64_t(p_members.size());
    if (!SceneViewersHead::wire.run(stream, head)) {
        return PackedByteArray();
    }
    for (uint32_t at = 0; at < p_members.size(); ++at) {
        SceneViewerRow row;
        row.player_id = p_members[at];
        if (!SceneViewerRow::wire.run(stream, row)) {
            return PackedByteArray();
        }
    }
    if (!stream.align_verify()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool viewers_read(
    const PackedByteArray &p_bytes,
    SceneViewersHead &r_head,
    LocalVector<uint64_t> &r_members
) {
    wire::ReadStream stream(p_bytes);
    SceneViewersHead head;
    if (!SceneViewersHead::wire.run(stream, head)) {
        return false;
    }
    if (head.count > VIEWERS_MAX) {
        return false;
    }
    LocalVector<uint64_t> staged;
    staged.reserve(uint32_t(head.count));
    for (uint64_t at = 0; at < head.count; ++at) {
        SceneViewerRow row;
        if (!SceneViewerRow::wire.run(stream, row)) {
            return false;
        }
        staged.push_back(row.player_id);
    }
    if (!stream.align_verify() || stream.bits_remaining() != 0) {
        return false;
    }
    r_head = head;
    r_members = staged;
    return true;
}

Dictionary frame_spec_records() {
    Dictionary out;
    out["ClockRate"] = ClockRate::wire.spec_dump();
    out["ClockPing"] = ClockPing::wire.spec_dump();
    out["ClockPong"] = ClockPong::wire.spec_dump();
    out["ActionRequest"] = ActionRequest::wire.spec_dump();
    out["SessionReason"] = SessionReason::wire.spec_dump();
    out["KickRequest"] = KickRequest::wire.spec_dump();
    out["SceneReleased"] = SceneReleased::wire.spec_dump();
    out["SceneViewersHead"] = SceneViewersHead::wire.spec_dump();
    out["SceneViewerRow"] = SceneViewerRow::wire.spec_dump();
    out["AcceptFrame"] = AcceptFrame::wire.spec_dump();
    out["JoinFrame"] = JoinFrame::wire.spec_dump();
    out["ControlRequest"] = ControlRequest::wire.spec_dump();
    out["ControlApply"] = ControlApply::wire.spec_dump();
    out["DenyKey"] = DenyKey::wire.spec_dump();
    out["ConfirmKey"] = ConfirmKey::wire.spec_dump();
    out["RouteLease"] = RouteLease::wire.spec_dump();
    out["ResyncHead"] = ResyncHead::wire.spec_dump();
    out["ResyncRoute"] = ResyncRoute::wire.spec_dump();
    return out;
}

} // namespace netw::session
