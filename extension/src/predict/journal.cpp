#include "netw/predict/journal.hpp"

namespace netw {

namespace predict {

namespace {

constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

} // namespace

int32_t fnv1a(const uint8_t *p_bytes, int p_size) {
    uint32_t hash = FNV_OFFSET;
    for (int at = 0; at < p_size; ++at) {
        hash = (hash ^ uint32_t(p_bytes[at])) * FNV_PRIME;
    }
    return int32_t(hash);
}

Journal::Journal(int p_capacity) {
    capacity = p_capacity < 1 ? 1 : p_capacity;
    const uint32_t width = uint32_t(capacity);
    transitions.resize(width);
    labels.resize(width);
    c_hashes.resize(width);
    e_digests.resize(width);
    pre_fps.resize(width);
    topo_fps.resize(width);
    raw_fps.resize(width);
    witness_fps.resize(width);
    witness_class_bits.resize(width);
    witness_realization_bits.resize(width);
    witness_states.resize(width);
    aligned_errors.resize(width);
    post_fps.resize(width);
    pre_pose_fps.resize(width);
    pre_momentum_fps.resize(width);
    pre_controller_fps.resize(width);
    post_pose_fps.resize(width);
    post_momentum_fps.resize(width);
    post_controller_fps.resize(width);
    episode_ids.resize(width);
    write_ids.resize(width);
    operators.resize(width);
    bases.resize(width);
    differing_families.resize(width);
    evidence_masks.resize(width);
    kinds.resize(width);
    domains.resize(width);
    attributions.resize(width);
    flags.resize(width);
}

int Journal::slot_of(int64_t p_transition) const {
    for (int index = count - 1; index >= 0; --index) {
        const int slot = (start + index) % capacity;
        if (transitions[uint32_t(slot)] == p_transition) {
            return slot;
        }
    }
    return -1;
}

int Journal::index_of(int64_t p_transition) const {
    for (int index = count - 1; index >= 0; --index) {
        const int slot = (start + index) % capacity;
        if (transitions[uint32_t(slot)] == p_transition) {
            return index;
        }
    }
    return -1;
}

int Journal::next_slot() {
    if (count < capacity) {
        const int slot = (start + count) % capacity;
        count += 1;
        return slot;
    }
    const int evicted = start;
    start = (start + 1) % capacity;
    return evicted;
}

void Journal::open(int64_t p_transition, const JournalOpen &p_row) {
    if (slot_of(p_transition) >= 0) {
        return;
    }
    const uint32_t slot = uint32_t(next_slot());
    transitions[slot] = p_transition;
    labels[slot] = p_row.label;
    c_hashes[slot] = p_row.c_hash;
    e_digests[slot] = 0;
    pre_fps[slot] = p_row.pre_fp;
    topo_fps[slot] = p_row.topo_fp;
    raw_fps[slot] = p_row.raw_fp;
    witness_fps[slot] = 0;
    witness_class_bits[slot] = 0;
    witness_realization_bits[slot] = 0;
    witness_states[slot] = 0;
    aligned_errors[slot] = 0.0f;
    post_fps[slot] = 0;
    pre_pose_fps[slot] = p_row.pre_families.pose;
    pre_momentum_fps[slot] = p_row.pre_families.momentum;
    pre_controller_fps[slot] = p_row.pre_families.controller;
    post_pose_fps[slot] = 0;
    post_momentum_fps[slot] = 0;
    post_controller_fps[slot] = 0;
    episode_ids[slot] = p_row.provenance.episode;
    write_ids[slot] = p_row.provenance.write_id;
    operators[slot] = uint8_t(p_row.provenance.op);
    bases[slot] = p_row.provenance.basis;
    differing_families[slot] = uint8_t(DifferingFamily::NONE);
    evidence_masks[slot] = p_row.evidence_mask;
    kinds[slot] = p_row.kind;
    domains[slot] = uint8_t(Domain::IN_DOMAIN);
    attributions[slot] = uint8_t(Attribution::UNKNOWN);
    flags[slot] = 0;
}

void Journal::mark_solve(
    int64_t p_transition,
    int32_t p_topo_fp,
    int32_t p_witness_fp,
    uint8_t p_evidence_mask,
    uint8_t p_witness_class_bits,
    uint8_t p_witness_realization_bits,
    uint8_t p_witness_state
) {
    const int slot = slot_of(p_transition);
    if (slot < 0 || (flags[uint32_t(slot)] & ROW_CLOSED) != 0) {
        return;
    }
    topo_fps[uint32_t(slot)] = p_topo_fp;
    witness_fps[uint32_t(slot)] = p_witness_fp;
    witness_class_bits[uint32_t(slot)] = p_witness_class_bits;
    witness_realization_bits[uint32_t(slot)] = p_witness_realization_bits;
    witness_states[uint32_t(slot)] = p_witness_state;
    evidence_masks[uint32_t(slot)] = p_evidence_mask;
}

void Journal::close(
    int64_t p_transition,
    int32_t p_post_fp,
    const FamilyFingerprints &p_post_families
) {
    const int slot = slot_of(p_transition);
    if (slot < 0 || (flags[uint32_t(slot)] & ROW_CLOSED) != 0) {
        return;
    }
    post_fps[uint32_t(slot)] = p_post_fp;
    post_pose_fps[uint32_t(slot)] = p_post_families.pose;
    post_momentum_fps[uint32_t(slot)] = p_post_families.momentum;
    post_controller_fps[uint32_t(slot)] = p_post_families.controller;
    flags[uint32_t(slot)] |= ROW_CLOSED;
}

void Journal::mark_chain_broken(int64_t p_transition) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        flags[uint32_t(slot)] |= ROW_CHAIN_BROKEN;
    }
}

void Journal::mark_ack(int64_t p_transition, bool p_matched) {
    const int slot = slot_of(p_transition);
    if (slot < 0) {
        return;
    }
    uint8_t bits = uint8_t(flags[uint32_t(slot)] | ROW_ACKED);
    bits = uint8_t(bits & ~uint8_t(ROW_MATCHED | ROW_DIVERGENT));
    flags[uint32_t(slot)]
        = uint8_t(bits | (p_matched ? ROW_MATCHED : ROW_DIVERGENT));
}

void Journal::mark_divergent(int64_t p_transition) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        flags[uint32_t(slot)] |= ROW_DIVERGENT;
    }
}

void Journal::mark_witness_match(int64_t p_transition, bool p_matched) {
    const int slot = slot_of(p_transition);
    if (slot < 0) {
        return;
    }
    flags[uint32_t(slot)]
        = uint8_t(flags[uint32_t(slot)] & ~uint8_t(ROW_WITNESS_MATCHED));
    if (p_matched) {
        flags[uint32_t(slot)] |= ROW_WITNESS_MATCHED;
    }
    witness_states[uint32_t(slot)] |= WITNESS_JUDGED;
}

bool Journal::witness_judged(int64_t p_transition) const {
    const int slot = slot_of(p_transition);
    return slot >= 0
        && (witness_states[uint32_t(slot)] & WITNESS_JUDGED) != 0;
}

void Journal::mark_substituted(int64_t p_transition) {
    const int slot = slot_of(p_transition);
    if (slot < 0) {
        return;
    }
    flags[uint32_t(slot)] |= uint8_t(ROW_SUBSTITUTED | ROW_CLOSED);
    // A transition authority ran with a command its owner never authored has
    // an unequal antecedent by definition, so it is never entitled to
    // exactness.
    domains[uint32_t(slot)] = uint8_t(Domain::OUT_OF_DOMAIN);
}

void Journal::mark_superseded(int64_t p_transition) {
    const int slot = slot_of(p_transition);
    if (slot < 0) {
        return;
    }
    flags[uint32_t(slot)] |= uint8_t(ROW_SUBSTITUTED | ROW_SUPERSEDED);
    domains[uint32_t(slot)] = uint8_t(Domain::OUT_OF_DOMAIN);
}

void Journal::mark_domain(int64_t p_transition, Domain p_domain) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        domains[uint32_t(slot)] = uint8_t(p_domain);
    }
}

void Journal::mark_e_digest(int64_t p_transition, int32_t p_e_digest) {
    const int slot = slot_of(p_transition);
    if (slot < 0 || (flags[uint32_t(slot)] & ROW_CLOSED) != 0) {
        return;
    }
    e_digests[uint32_t(slot)] = p_e_digest;
}

void Journal::mark_attribution(
    int64_t p_transition,
    Attribution p_attribution
) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        attributions[uint32_t(slot)] = uint8_t(p_attribution);
    }
}

void Journal::mark_provenance(
    int64_t p_transition,
    int p_episode_id,
    int p_write_id,
    Operator p_operator,
    int64_t p_basis
) {
    const int slot = slot_of(p_transition);
    if (slot < 0) {
        return;
    }
    episode_ids[uint32_t(slot)] = p_episode_id;
    write_ids[uint32_t(slot)] = p_write_id;
    operators[uint32_t(slot)] = uint8_t(p_operator);
    bases[uint32_t(slot)] = p_basis;
    flags[uint32_t(slot)] &= ~uint8_t(ROW_CHAIN_BROKEN);
}

void Journal::mark_aligned_error(int64_t p_transition, double p_error) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        aligned_errors[uint32_t(slot)] = float(p_error);
    }
}

void Journal::mark_differing_family(
    int64_t p_transition,
    DifferingFamily p_family
) {
    const int slot = slot_of(p_transition);
    if (slot >= 0) {
        differing_families[uint32_t(slot)] = uint8_t(p_family);
    }
}

int64_t Journal::last_closed() const {
    int64_t newest = -1;
    for (int index = 0; index < count; ++index) {
        const uint32_t slot = uint32_t((start + index) % capacity);
        if ((flags[slot] & ROW_CLOSED) == 0) {
            break;
        }
        newest = transitions[slot];
    }
    return newest;
}

int64_t Journal::first_unmatched() const {
    for (int index = 0; index < count; ++index) {
        const uint32_t slot = uint32_t((start + index) % capacity);
        if ((flags[slot] & ROW_MATCHED) == 0) {
            return transitions[slot];
        }
    }
    return -1;
}

int64_t Journal::first_chain_break() const {
    for (int index = 0; index < count; ++index) {
        const uint32_t slot = uint32_t((start + index) % capacity);
        if ((flags[slot] & ROW_CHAIN_BROKEN) != 0) {
            return transitions[slot];
        }
    }
    return -1;
}

void Journal::clear(int64_t p_epoch) {
    ring_epoch = p_epoch;
    count = 0;
    start = 0;
}

#define NETW_JOURNAL_READ_AT(m_name, m_column, m_type, m_absent) \
    m_type Journal::m_name(int p_index) const { \
        if (p_index < 0 || p_index >= count) { \
            return m_absent; \
        } \
        return m_type(m_column[uint32_t((start + p_index) % capacity)]); \
    }

NETW_JOURNAL_READ_AT(transition_at, transitions, int64_t, -1)
NETW_JOURNAL_READ_AT(label_at, labels, int64_t, -1)
NETW_JOURNAL_READ_AT(kind_at, kinds, uint8_t, 0)
NETW_JOURNAL_READ_AT(c_hash_at, c_hashes, int32_t, 0)
NETW_JOURNAL_READ_AT(e_digest_at, e_digests, int32_t, 0)
NETW_JOURNAL_READ_AT(pre_fp_at, pre_fps, int32_t, 0)
NETW_JOURNAL_READ_AT(post_fp_at, post_fps, int32_t, 0)
NETW_JOURNAL_READ_AT(topo_fp_at, topo_fps, int32_t, 0)
NETW_JOURNAL_READ_AT(raw_fp_at, raw_fps, int32_t, 0)
NETW_JOURNAL_READ_AT(witness_fp_at, witness_fps, int32_t, 0)
NETW_JOURNAL_READ_AT(
    witness_class_bits_at,
    witness_class_bits,
    uint8_t,
    0
)
NETW_JOURNAL_READ_AT(
    witness_realization_bits_at,
    witness_realization_bits,
    uint8_t,
    0
)
NETW_JOURNAL_READ_AT(witness_state_at, witness_states, uint8_t, 0)
NETW_JOURNAL_READ_AT(episode_id_at, episode_ids, int32_t, 0)
NETW_JOURNAL_READ_AT(write_id_at, write_ids, int32_t, 0)
NETW_JOURNAL_READ_AT(operator_at, operators, uint8_t, 0)
NETW_JOURNAL_READ_AT(basis_at, bases, int64_t, -1)
NETW_JOURNAL_READ_AT(evidence_mask_at, evidence_masks, uint8_t, 0)
NETW_JOURNAL_READ_AT(flags_at, flags, uint8_t, 0)
NETW_JOURNAL_READ_AT(domain_at, domains, uint8_t, 0)
NETW_JOURNAL_READ_AT(attribution_at, attributions, uint8_t, 0)
NETW_JOURNAL_READ_AT(differing_family_at, differing_families, uint8_t, 0)
NETW_JOURNAL_READ_AT(aligned_error_at, aligned_errors, float, 0.0f)

#undef NETW_JOURNAL_READ_AT

FamilyFingerprints Journal::pre_families_at(int p_index) const {
    FamilyFingerprints out;
    if (p_index < 0 || p_index >= count) {
        return out;
    }
    const uint32_t slot = uint32_t((start + p_index) % capacity);
    out.pose = pre_pose_fps[slot];
    out.momentum = pre_momentum_fps[slot];
    out.controller = pre_controller_fps[slot];
    return out;
}

FamilyFingerprints Journal::post_families_at(int p_index) const {
    FamilyFingerprints out;
    if (p_index < 0 || p_index >= count) {
        return out;
    }
    const uint32_t slot = uint32_t((start + p_index) % capacity);
    out.pose = post_pose_fps[slot];
    out.momentum = post_momentum_fps[slot];
    out.controller = post_controller_fps[slot];
    return out;
}

Domain Journal::domain_of(int64_t p_transition) const {
    const int slot = slot_of(p_transition);
    return slot < 0 ? Domain::OUT_OF_DOMAIN : Domain(domains[uint32_t(slot)]);
}

uint8_t Journal::flags_of(int64_t p_transition) const {
    const int slot = slot_of(p_transition);
    return slot < 0 ? uint8_t(0) : flags[uint32_t(slot)];
}

Attribution Journal::attribution_of(int64_t p_transition) const {
    const int slot = slot_of(p_transition);
    return slot < 0 ? Attribution::UNKNOWN
                    : Attribution(attributions[uint32_t(slot)]);
}

int32_t Journal::post_fp_of(int64_t p_transition) const {
    const int slot = slot_of(p_transition);
    return slot < 0 ? 0 : post_fps[uint32_t(slot)];
}

JournalEvidence Journal::evidence_of(int64_t p_transition) const {
    JournalEvidence out;
    const int found = slot_of(p_transition);
    if (found < 0) {
        return out;
    }
    const uint32_t slot = uint32_t(found);
    out.pre_fp = pre_fps[slot];
    out.c_hash = c_hashes[slot];
    out.e_digest = e_digests[slot];
    out.post_fp = post_fps[slot];
    out.pre_families.pose = pre_pose_fps[slot];
    out.pre_families.momentum = pre_momentum_fps[slot];
    out.pre_families.controller = pre_controller_fps[slot];
    out.post_families.pose = post_pose_fps[slot];
    out.post_families.momentum = post_momentum_fps[slot];
    out.post_families.controller = post_controller_fps[slot];
    out.topo_fp = topo_fps[slot];
    out.witness_fp = witness_fps[slot];
    out.raw_fp = raw_fps[slot];
    out.evidence_mask = evidence_masks[slot];
    out.present = true;
    return out;
}

} // namespace predict

} // namespace netw
