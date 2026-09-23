#include "netw/predict/journal_snapshot.hpp"

using namespace godot;

namespace netw::predict {

int32_t fnv1a(const PackedByteArray &p_bytes) {
    return fnv1a(p_bytes.ptr(), int(p_bytes.size()));
}

void JournalSnapshot::adopt(
    const Journal &p_journal,
    const Dictionary &p_witness_details
) {
    journal = p_journal;
    witness_details = p_witness_details;
}

Dictionary JournalSnapshot::row_at(int64_t p_transition) const {
    Dictionary out;
    const int at = journal.index_of(p_transition);
    if (at < 0) {
        return out;
    }
    const FamilyFingerprints pre = journal.pre_families_at(at);
    const FamilyFingerprints post = journal.post_families_at(at);
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

PackedInt64Array JournalSnapshot::transitions() const {
    PackedInt64Array out;
    const int rows = journal.size();
    out.resize(rows);
    int64_t *values = out.ptrw();
    for (int at = 0; at < rows; ++at) {
        values[at] = journal.transition_at(at);
    }
    return out;
}

int64_t JournalSnapshot::last_closed() const {
    return journal.last_closed();
}

int JournalSnapshot::size() const {
    return journal.size();
}

int64_t JournalSnapshot::epoch() const {
    return journal.epoch();
}

} // namespace netw::predict
