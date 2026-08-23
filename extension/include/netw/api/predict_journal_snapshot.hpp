#pragma once

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/variant.hpp"
#include "netw/predict/journal.hpp"

namespace netw {

class NetwPredictJournal : public godot::RefCounted {
    GDCLASS(NetwPredictJournal, godot::RefCounted)

    predict::Journal journal;
    godot::Dictionary witness_details;

    int slot_of(int64_t p_transition) const;

protected:
    static void _bind_methods();

public:
    enum {
        CAPACITY_DEFAULT = predict::JOURNAL_CAPACITY_DEFAULT,
        ROW_ACKED = predict::ROW_ACKED,
        ROW_MATCHED = predict::ROW_MATCHED,
        ROW_SUBSTITUTED = predict::ROW_SUBSTITUTED,
        ROW_SUPERSEDED = predict::ROW_SUPERSEDED,
        ROW_DIVERGENT = predict::ROW_DIVERGENT,
        ROW_CLOSED = predict::ROW_CLOSED,
        ROW_CHAIN_BROKEN = predict::ROW_CHAIN_BROKEN,
        ROW_WITNESS_MATCHED = predict::ROW_WITNESS_MATCHED,
        EVIDENCE_WITNESS = predict::EVIDENCE_WITNESS,
        EVIDENCE_RAW = predict::EVIDENCE_RAW,
    };

    enum Attribution {
        UNKNOWN = 0,
        PRE_STATE = 1,
        COMMAND = 2,
        ENVIRONMENT = 3,
        TOPOLOGY = 4,
        EXECUTION = 5,
        CONTACT = 6,
        CLOSURE = 7,
    };

    enum Operator {
        NONE = 0,
        REBASE_PROJECTED = 1,
        REBASE_EXACT = 2,
        FULL_CLOSURE = 3,
        TRANSPORT_DELTA = 4,
        RESEED = 5,
        RESEED_ALIGN = 6,
        DEMOTE = 7,
        DISSIPATE = 8,
        JOINT_REBASE = 9,
    };

    enum StateFamily {
        FAMILY_NONE = 0,
        POSE = 1,
        MOMENTUM = 2,
        CONTROLLER_LATCH = 3,
    };

    enum Domain {
        IN_DOMAIN = 0,
        OUT_OF_DOMAIN = 1,
    };

    predict::Journal &rows() {
        return journal;
    }

    void adopt(
        const predict::Journal &p_journal,
        const godot::Dictionary &p_witness_details
    );

    static int32_t fnv1a(const godot::PackedByteArray &p_bytes);

    static godot::Dictionary attribution_names();
    static godot::Dictionary operator_names();
    static godot::Dictionary domain_names();
    static godot::Dictionary state_family_names();

    godot::Dictionary row_at(int64_t p_transition) const;
    int attribution_at(int64_t p_transition) const;
    int domain_at(int64_t p_transition) const;
    int flags_at(int64_t p_transition) const;

    int64_t last_closed() const;
    int64_t first_unmatched() const;
    int64_t first_chain_break() const;

    void clear(int64_t p_epoch);
    int size() const;
    int capacity() const;
    int64_t epoch() const;

    godot::PackedInt64Array transitions() const;
    godot::PackedInt64Array labels() const;
    godot::PackedInt32Array c_hashes() const;
    godot::PackedInt32Array e_digests() const;
    godot::PackedInt32Array pre_fps() const;
    godot::PackedInt32Array post_fps() const;
    godot::PackedInt32Array topo_fps() const;
    godot::PackedInt32Array raw_fps() const;
    godot::PackedInt32Array witness_fps() const;
    godot::PackedByteArray witness_class_bits() const;
    godot::PackedInt32Array pre_family_fps(int p_family) const;
    godot::PackedInt32Array post_family_fps(int p_family) const;
    godot::PackedByteArray kinds() const;
    godot::PackedByteArray domains() const;
    godot::PackedByteArray attributions() const;
    godot::PackedByteArray evidence_masks() const;
    godot::PackedByteArray flags() const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredictJournal::Attribution);
VARIANT_ENUM_CAST(netw::NetwPredictJournal::Operator);
VARIANT_ENUM_CAST(netw::NetwPredictJournal::StateFamily);
VARIANT_ENUM_CAST(netw::NetwPredictJournal::Domain);
