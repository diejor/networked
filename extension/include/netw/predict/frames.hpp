#pragma once

/* Prediction's four declared wire lanes.
 *
 * COMMAND carries a contiguous transition window and one schema-planned code
 * row for every fresh transition. ACK carries authority's oldest evidence
 * prefix. RELAY is the admitted COMMAND byte sequence unchanged, and the
 * subscription request is one boolean bit.
 *
 * Protocol identity is established before any of these frames are admitted,
 * so no payload repeats a version byte. Every variable section is bounded by
 * the count in its typed header and a decoder accepts only a complete frame
 * with no unread residue.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/wire/code_row.hpp"
#include "netw/wire/describe.hpp"
#include "netw/wire/plan.hpp"
#include "netw/wire/registry.hpp"

namespace netw::predict {

/* The ids are spelled here as well as in the registry, because the registry
 * composes every family's channels and cannot depend on one of them. Nothing
 * else is restated: `admit_frame` reads each lane's direction off the
 * registry, and a law pins these ids to the declarations they name.
 */
constexpr uint8_t CHANNEL_COMMAND = 35;
constexpr uint8_t CHANNEL_ACK = 36;
constexpr uint8_t CHANNEL_RELAY = 37;
constexpr uint8_t CHANNEL_RELAY_REQUEST = 38;

constexpr int64_t SERVER_PEER = 1;

bool is_lane(uint8_t p_channel);

constexpr uint8_t ACK_SUBSTITUTED = 1U << 0;
constexpr uint8_t ACK_SUPERSEDED = 1U << 1;
constexpr int ACK_WITNESS_SHIFT = 2;
constexpr uint8_t ACK_WITNESS_MASK = 0x1c;
constexpr int ACK_RECORD_MAX = 21;
constexpr int ACK_FRAME_BYTES_MAX = 1150;
constexpr int COMMAND_TRANSITION_MAX = 255;

struct TransitionWire {
    int64_t index = 0;
    int64_t label = 0;
    bool fresh = false;
};

struct CommandEvidenceWire {
    uint8_t evidence_mask = 0;
    int32_t pre_fp = 0;
    int32_t post_fp = 0;
    int32_t e_digest = 0;
    int32_t topo_fp = 0;
    int32_t witness_fp = 0;
    int32_t pre_pose_fp = 0;
    int32_t pre_momentum_fp = 0;
    int32_t pre_controller_fp = 0;
    int32_t post_pose_fp = 0;
    int32_t post_momentum_fp = 0;
    int32_t post_controller_fp = 0;
    int32_t raw_fp = 0;
};

struct AckEvidenceWire {
    uint8_t evidence_mask = 0;
    int32_t pre_fp = 0;
    int32_t c_hash = 0;
    int32_t e_digest = 0;
    int32_t post_fp = 0;
    int32_t topo_fp = 0;
    int32_t witness_fp = 0;
    int32_t pre_pose_fp = 0;
    int32_t pre_momentum_fp = 0;
    int32_t pre_controller_fp = 0;
    int32_t post_pose_fp = 0;
    int32_t post_momentum_fp = 0;
    int32_t post_controller_fp = 0;
    int32_t raw_fp = 0;
    uint8_t flags = 0;
};

struct CommandFrame {
    uint8_t epoch = 0;
    int64_t ack_of_acks = -1;
    godot::LocalVector<TransitionWire> transitions;
    godot::LocalVector<wire::CodeRow> payloads;
    godot::LocalVector<CommandEvidenceWire> evidence;
};

struct AckFrame {
    uint8_t epoch = 0;
    uint64_t base = 0;
    godot::LocalVector<AckEvidenceWire> records;
};

godot::PackedByteArray encode_command(
    const CommandFrame &p_frame,
    const wire::WirePlan &p_input_plan
);

bool decode_command(
    const godot::PackedByteArray &p_bytes,
    const wire::WirePlan &p_input_plan,
    CommandFrame &r_frame
);

godot::PackedByteArray encode_ack(const AckFrame &p_frame);
bool decode_ack(const godot::PackedByteArray &p_bytes, AckFrame &r_frame);

godot::PackedByteArray encode_relay_request(bool p_subscribed);
bool decode_relay_request(
    const godot::PackedByteArray &p_bytes,
    bool &r_subscribed
);

// `controller` is the peer the addressed entity answers to.
struct FrameOrigin {
    int64_t sender = 0;
    int64_t controller = 0;
    bool receiver_is_server = false;
};

/* Whether one inbound prediction frame may be admitted.
 *
 * A refused route outranks the frame's own verdict, so a drop is attributed to
 * the route rather than to the sender. The authorship rule comes from the
 * registry's declared direction, so a lane cannot be gated one way and
 * declared another.
 */
godot::Error admit_frame(
    const wire::WireRegistry &p_registry,
    uint8_t p_channel,
    const FrameOrigin &p_origin,
    bool p_payload_empty,
    godot::Error p_route_verdict
);

} // namespace netw::predict
