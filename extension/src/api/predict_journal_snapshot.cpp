#include "netw/api/predict_journal_snapshot.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

void NetwPredictJournal::adopt(
    const predict::Journal &p_journal,
    const Dictionary &p_witness_details
) {
    journal = p_journal;
    witness_details = p_witness_details;
}

int NetwPredictJournal::slot_of(int64_t p_transition) const {
    return journal.index_of(p_transition);
}

int32_t NetwPredictJournal::fnv1a(const PackedByteArray &p_bytes) {
    return predict::fnv1a(p_bytes.ptr(), int(p_bytes.size()));
}

Dictionary NetwPredictJournal::attribution_names() {
    Dictionary out;
    out["UNKNOWN"] = UNKNOWN;
    out["PRE_STATE"] = PRE_STATE;
    out["COMMAND"] = COMMAND;
    out["ENVIRONMENT"] = ENVIRONMENT;
    out["TOPOLOGY"] = TOPOLOGY;
    out["EXECUTION"] = EXECUTION;
    out["CONTACT"] = CONTACT;
    out["CLOSURE"] = CLOSURE;
    return out;
}

Dictionary NetwPredictJournal::operator_names() {
    Dictionary out;
    out["NONE"] = NONE;
    out["REBASE_PROJECTED"] = REBASE_PROJECTED;
    out["REBASE_EXACT"] = REBASE_EXACT;
    out["FULL_CLOSURE"] = FULL_CLOSURE;
    out["TRANSPORT_DELTA"] = TRANSPORT_DELTA;
    out["RESEED"] = RESEED;
    out["RESEED_ALIGN"] = RESEED_ALIGN;
    out["DEMOTE"] = DEMOTE;
    out["DISSIPATE"] = DISSIPATE;
    out["JOINT_REBASE"] = JOINT_REBASE;
    return out;
}

Dictionary NetwPredictJournal::domain_names() {
    Dictionary out;
    out["IN_DOMAIN"] = IN_DOMAIN;
    out["OUT_OF_DOMAIN"] = OUT_OF_DOMAIN;
    return out;
}

Dictionary NetwPredictJournal::state_family_names() {
    Dictionary out;
    out["FAMILY_NONE"] = FAMILY_NONE;
    out["POSE"] = POSE;
    out["MOMENTUM"] = MOMENTUM;
    out["CONTROLLER_LATCH"] = CONTROLLER_LATCH;
    return out;
}

Dictionary NetwPredictJournal::row_at(int64_t p_transition) const {
    Dictionary out;
    const int at = slot_of(p_transition);
    if (at < 0) {
        return out;
    }
    const predict::FamilyFingerprints pre = journal.pre_families_at(at);
    const predict::FamilyFingerprints post = journal.post_families_at(at);
    out["transition"] = journal.transition_at(at);
    out["label"] = journal.label_at(at);
    out["kind"] = journal.kind_at(at);
    out["c_hash"] = journal.c_hash_at(at);
    out["e_digest"] = journal.e_digest_at(at);
    out["pre_fp"] = journal.pre_fp_at(at);
    out["topo_fp"] = journal.topo_fp_at(at);
    out["raw_fp"] = journal.raw_fp_at(at);
    out["witness_fp"] = journal.witness_fp_at(at);
    out["witness_class_bits"] = journal.witness_class_bits_at(at);
    out["aligned_error"] = journal.aligned_error_at(at);
    out["evidence_mask"] = journal.evidence_mask_at(at);
    out["witness_detail"] = witness_details.get(p_transition, Dictionary());
    out["pre_pose_fp"] = pre.pose;
    out["pre_momentum_fp"] = pre.momentum;
    out["pre_controller_fp"] = pre.controller;
    out["post_fp"] = journal.post_fp_at(at);
    out["post_pose_fp"] = post.pose;
    out["post_momentum_fp"] = post.momentum;
    out["post_controller_fp"] = post.controller;
    out["episode_id"] = journal.episode_id_at(at);
    out["write_id"] = journal.write_id_at(at);
    out["operator"] = journal.operator_at(at);
    out["basis"] = journal.basis_at(at);
    out["differing_family"] = journal.differing_family_at(at);
    out["domain"] = journal.domain_at(at);
    out["attribution"] = journal.attribution_at(at);
    out["flags"] = journal.flags_at(at);
    return out;
}

int NetwPredictJournal::attribution_at(int64_t p_transition) const {
    return int(journal.attribution_of(p_transition));
}

int NetwPredictJournal::domain_at(int64_t p_transition) const {
    return int(journal.domain_of(p_transition));
}

int NetwPredictJournal::flags_at(int64_t p_transition) const {
    return journal.flags_of(p_transition);
}

int64_t NetwPredictJournal::last_closed() const {
    return journal.last_closed();
}

int64_t NetwPredictJournal::first_unmatched() const {
    return journal.first_unmatched();
}

int64_t NetwPredictJournal::first_chain_break() const {
    return journal.first_chain_break();
}

void NetwPredictJournal::clear(int64_t p_epoch) {
    journal.clear(p_epoch);
    witness_details.clear();
}

int NetwPredictJournal::size() const {
    return journal.size();
}

int NetwPredictJournal::capacity() const {
    return journal.capacity_limit();
}

int64_t NetwPredictJournal::epoch() const {
    return journal.epoch();
}

#define NETW_JOURNAL_COLUMN(m_name, m_array, m_reader) \
    m_array NetwPredictJournal::m_name() const { \
        m_array out; \
        const int rows = journal.size(); \
        out.resize(rows); \
        auto *values = out.ptrw(); \
        for (int at = 0; at < rows; ++at) { \
            values[at] = journal.m_reader(at); \
        } \
        return out; \
    }

NETW_JOURNAL_COLUMN(transitions, PackedInt64Array, transition_at)
NETW_JOURNAL_COLUMN(labels, PackedInt64Array, label_at)
NETW_JOURNAL_COLUMN(c_hashes, PackedInt32Array, c_hash_at)
NETW_JOURNAL_COLUMN(e_digests, PackedInt32Array, e_digest_at)
NETW_JOURNAL_COLUMN(pre_fps, PackedInt32Array, pre_fp_at)
NETW_JOURNAL_COLUMN(post_fps, PackedInt32Array, post_fp_at)
NETW_JOURNAL_COLUMN(topo_fps, PackedInt32Array, topo_fp_at)
NETW_JOURNAL_COLUMN(raw_fps, PackedInt32Array, raw_fp_at)
NETW_JOURNAL_COLUMN(witness_fps, PackedInt32Array, witness_fp_at)
NETW_JOURNAL_COLUMN(witness_class_bits, PackedByteArray, witness_class_bits_at)
NETW_JOURNAL_COLUMN(kinds, PackedByteArray, kind_at)
NETW_JOURNAL_COLUMN(domains, PackedByteArray, domain_at)
NETW_JOURNAL_COLUMN(attributions, PackedByteArray, attribution_at)
NETW_JOURNAL_COLUMN(evidence_masks, PackedByteArray, evidence_mask_at)
NETW_JOURNAL_COLUMN(flags, PackedByteArray, flags_at)

#undef NETW_JOURNAL_COLUMN

PackedInt32Array NetwPredictJournal::pre_family_fps(int p_family) const {
    PackedInt32Array out;
    const int rows = journal.size();
    out.resize(rows);
    int32_t *values = out.ptrw();
    for (int at = 0; at < rows; ++at) {
        const predict::FamilyFingerprints row = journal.pre_families_at(at);
        values[at] = p_family == POSE ? row.pose
            : p_family == MOMENTUM    ? row.momentum
                                      : row.controller;
    }
    return out;
}

PackedInt32Array NetwPredictJournal::post_family_fps(int p_family) const {
    PackedInt32Array out;
    const int rows = journal.size();
    out.resize(rows);
    int32_t *values = out.ptrw();
    for (int at = 0; at < rows; ++at) {
        const predict::FamilyFingerprints row = journal.post_families_at(at);
        values[at] = p_family == POSE ? row.pose
            : p_family == MOMENTUM    ? row.momentum
                                      : row.controller;
    }
    return out;
}

void NetwPredictJournal::_bind_methods() {
    ClassDB::bind_static_method(
        "NetwPredictJournal",
        D_METHOD("fnv1a", "bytes"),
        &NetwPredictJournal::fnv1a
    );
    ClassDB::bind_static_method(
        "NetwPredictJournal",
        D_METHOD("attribution_names"),
        &NetwPredictJournal::attribution_names
    );
    ClassDB::bind_static_method(
        "NetwPredictJournal",
        D_METHOD("operator_names"),
        &NetwPredictJournal::operator_names
    );
    ClassDB::bind_static_method(
        "NetwPredictJournal",
        D_METHOD("domain_names"),
        &NetwPredictJournal::domain_names
    );
    ClassDB::bind_static_method(
        "NetwPredictJournal",
        D_METHOD("state_family_names"),
        &NetwPredictJournal::state_family_names
    );
    ClassDB::bind_method(
        D_METHOD("row_at", "transition"),
        &NetwPredictJournal::row_at
    );
    ClassDB::bind_method(
        D_METHOD("attribution_at", "transition"),
        &NetwPredictJournal::attribution_at
    );
    ClassDB::bind_method(
        D_METHOD("domain_at", "transition"),
        &NetwPredictJournal::domain_at
    );
    ClassDB::bind_method(
        D_METHOD("flags_at", "transition"),
        &NetwPredictJournal::flags_at
    );
    ClassDB::bind_method(
        D_METHOD("last_closed"),
        &NetwPredictJournal::last_closed
    );
    ClassDB::bind_method(
        D_METHOD("first_unmatched"),
        &NetwPredictJournal::first_unmatched
    );
    ClassDB::bind_method(
        D_METHOD("first_chain_break"),
        &NetwPredictJournal::first_chain_break
    );
    ClassDB::bind_method(
        D_METHOD("clear", "epoch"),
        &NetwPredictJournal::clear
    );
    ClassDB::bind_method(D_METHOD("size"), &NetwPredictJournal::size);
    ClassDB::bind_method(D_METHOD("capacity"), &NetwPredictJournal::capacity);
    ClassDB::bind_method(D_METHOD("epoch"), &NetwPredictJournal::epoch);
    ClassDB::bind_method(
        D_METHOD("transitions"),
        &NetwPredictJournal::transitions
    );
    ClassDB::bind_method(D_METHOD("labels"), &NetwPredictJournal::labels);
    ClassDB::bind_method(D_METHOD("c_hashes"), &NetwPredictJournal::c_hashes);
    ClassDB::bind_method(D_METHOD("e_digests"), &NetwPredictJournal::e_digests);
    ClassDB::bind_method(D_METHOD("pre_fps"), &NetwPredictJournal::pre_fps);
    ClassDB::bind_method(D_METHOD("post_fps"), &NetwPredictJournal::post_fps);
    ClassDB::bind_method(D_METHOD("topo_fps"), &NetwPredictJournal::topo_fps);
    ClassDB::bind_method(D_METHOD("raw_fps"), &NetwPredictJournal::raw_fps);
    ClassDB::bind_method(
        D_METHOD("witness_fps"),
        &NetwPredictJournal::witness_fps
    );
    ClassDB::bind_method(
        D_METHOD("witness_class_bits"),
        &NetwPredictJournal::witness_class_bits
    );
    ClassDB::bind_method(
        D_METHOD("pre_family_fps", "family"),
        &NetwPredictJournal::pre_family_fps
    );
    ClassDB::bind_method(
        D_METHOD("post_family_fps", "family"),
        &NetwPredictJournal::post_family_fps
    );
    ClassDB::bind_method(D_METHOD("kinds"), &NetwPredictJournal::kinds);
    ClassDB::bind_method(D_METHOD("domains"), &NetwPredictJournal::domains);
    ClassDB::bind_method(
        D_METHOD("attributions"),
        &NetwPredictJournal::attributions
    );
    ClassDB::bind_method(
        D_METHOD("evidence_masks"),
        &NetwPredictJournal::evidence_masks
    );
    ClassDB::bind_method(D_METHOD("flags"), &NetwPredictJournal::flags);

    BIND_CONSTANT(CAPACITY_DEFAULT);
    BIND_CONSTANT(ROW_ACKED);
    BIND_CONSTANT(ROW_MATCHED);
    BIND_CONSTANT(ROW_SUBSTITUTED);
    BIND_CONSTANT(ROW_SUPERSEDED);
    BIND_CONSTANT(ROW_DIVERGENT);
    BIND_CONSTANT(ROW_CLOSED);
    BIND_CONSTANT(ROW_CHAIN_BROKEN);
    BIND_CONSTANT(ROW_WITNESS_MATCHED);
    BIND_CONSTANT(EVIDENCE_WITNESS);
    BIND_CONSTANT(EVIDENCE_RAW);

    BIND_ENUM_CONSTANT(UNKNOWN);
    BIND_ENUM_CONSTANT(PRE_STATE);
    BIND_ENUM_CONSTANT(COMMAND);
    BIND_ENUM_CONSTANT(ENVIRONMENT);
    BIND_ENUM_CONSTANT(TOPOLOGY);
    BIND_ENUM_CONSTANT(EXECUTION);
    BIND_ENUM_CONSTANT(CONTACT);
    BIND_ENUM_CONSTANT(CLOSURE);

    BIND_ENUM_CONSTANT(NONE);
    BIND_ENUM_CONSTANT(REBASE_PROJECTED);
    BIND_ENUM_CONSTANT(REBASE_EXACT);
    BIND_ENUM_CONSTANT(FULL_CLOSURE);
    BIND_ENUM_CONSTANT(TRANSPORT_DELTA);
    BIND_ENUM_CONSTANT(RESEED);
    BIND_ENUM_CONSTANT(RESEED_ALIGN);
    BIND_ENUM_CONSTANT(DEMOTE);
    BIND_ENUM_CONSTANT(DISSIPATE);
    BIND_ENUM_CONSTANT(JOINT_REBASE);

    BIND_ENUM_CONSTANT(FAMILY_NONE);
    BIND_ENUM_CONSTANT(POSE);
    BIND_ENUM_CONSTANT(MOMENTUM);
    BIND_ENUM_CONSTANT(CONTROLLER_LATCH);

    BIND_ENUM_CONSTANT(IN_DOMAIN);
    BIND_ENUM_CONSTANT(OUT_OF_DOMAIN);
}

} // namespace netw
