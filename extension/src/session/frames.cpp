#include "netw/session/frames.hpp"

#include "netw/api/join_request.hpp"

using namespace godot;

namespace netw::session {

namespace {

constexpr int ROSTER_COUNT_BYTES = 2;

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

Dictionary frame_spec_records() {
    Dictionary out;
    out["ClockRate"] = ClockRate::wire.spec_dump();
    out["ClockPing"] = ClockPing::wire.spec_dump();
    out["ClockPong"] = ClockPong::wire.spec_dump();
    out["ActionRequest"] = ActionRequest::wire.spec_dump();
    out["SessionReason"] = SessionReason::wire.spec_dump();
    out["KickRequest"] = KickRequest::wire.spec_dump();
    out["SceneRequest"] = SceneRequest::wire.spec_dump();
    out["SceneResult"] = SceneResult::wire.spec_dump();
    out["SceneReleased"] = SceneReleased::wire.spec_dump();
    out["SceneSeat"] = SceneSeat::wire.spec_dump();
    out["AcceptFrame"] = AcceptFrame::wire.spec_dump();
    out["JoinFrame"] = JoinFrame::wire.spec_dump();
    out["ControlApply"] = ControlApply::wire.spec_dump();
    out["DenyKey"] = DenyKey::wire.spec_dump();
    return out;
}

} // namespace netw::session
