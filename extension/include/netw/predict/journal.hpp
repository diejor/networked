#pragma once

/* The packed-column ring recording every transition an engine drove.
 *
 * A row opens when a transition is authored and closes when its produced state
 * is fingerprinted, so a closed row names one drive completely. Evidence is
 * immutable after close while the verdict overlay, attribution and domain may
 * settle later, which is what makes the fingerprint an acceptance test rather
 * than a tolerance.
 *
 * [codeblock]
 * journal.open(transition, Open{ ... });
 * journal.close(transition, post_fp, post_families);
 * const int slot = journal.slot_of(transition);
 * [/codeblock]
 *
 * Every column is addressed by ring slot and a slot is resolved once, so a
 * reader that wants several columns of one row pays one scan rather than one
 * per column.
 */

#include <cstdint>

#include "godot/local_vector.hpp"

namespace netw {

using namespace godot;

namespace predict {

enum class Attribution : uint8_t {
    UNKNOWN = 0,
    PRE_STATE = 1,
    COMMAND = 2,
    ENVIRONMENT = 3,
    TOPOLOGY = 4,
    EXECUTION = 5,
    CONTACT = 6,
    CLOSURE = 7,
};

enum class Operator : uint8_t {
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

enum class DifferingFamily : uint8_t {
    NONE = 0,
    POSE = 1,
    MOMENTUM = 2,
    CONTROLLER_LATCH = 3,
};

enum class Domain : uint8_t {
    IN_DOMAIN = 0,
    OUT_OF_DOMAIN = 1,
};

enum Row : uint8_t {
    ROW_ACKED = 1,
    ROW_MATCHED = 2,
    ROW_SUBSTITUTED = 4,
    ROW_SUPERSEDED = 8,
    ROW_DIVERGENT = 16,
    ROW_CLOSED = 32,
    ROW_CHAIN_BROKEN = 64,
    ROW_WITNESS_MATCHED = 128,
};

// A solve the body slept through witnessed nothing, and one that woke into it
// witnessed a transition the solver started rather than continued. Neither is
// evidence a conditional operator may build on.
enum WitnessState : uint8_t {
    WITNESS_SLEEPING = 1,
    WITNESS_WOKE = 2,
    // Authority has answered for this row's witness. Distinct from
    // ROW_WITNESS_MATCHED, which cannot tell a verdict of "differs" from a
    // verdict that has not arrived, and a conditional operator waits on
    // exactly that difference.
    WITNESS_JUDGED = 4,
};

enum Evidence : uint8_t {
    EVIDENCE_WITNESS = 1,
    EVIDENCE_RAW = 2,
};

constexpr int JOURNAL_CAPACITY_DEFAULT = 256;

// The fingerprint the journal, the wire and every later comparison share. The
// result is signed because that is the width the columns and the wire both
// carry, so a fingerprint never changes representation between being computed,
// stored and sent.
int32_t fnv1a(const uint8_t *p_bytes, int p_size);

// Pose, momentum, then controller and latch, which is the order every family
// column pair is written and read in.
struct FamilyFingerprints {
    int32_t pose = 0;
    int32_t momentum = 0;
    int32_t controller = 0;
};

// The operator write that accounts for a state-chain discontinuity ahead of
// this row, or `NONE` with a `-1` basis when nothing wrote the body.
struct Provenance {
    int32_t episode = 0;
    int32_t write_id = 0;
    Operator op = Operator::NONE;
    int64_t basis = -1;
};

/* What a row carries at the moment it opens, before its drive runs.
 *
 * Grouped rather than passed as ten arguments because every field is written
 * in one place and read in one place, and a call site naming them positionally
 * is a call site where two ints of the same type can be swapped silently.
 */
struct JournalOpen {
    int64_t label = 0;
    uint8_t kind = 0;
    int32_t c_hash = 0;
    int32_t pre_fp = 0;
    FamilyFingerprints pre_families;
    Provenance provenance;
    int32_t topo_fp = 0;
    int32_t raw_fp = 0;
    uint8_t evidence_mask = 0;
};

struct JournalEvidence {
    int32_t pre_fp = 0;
    int32_t c_hash = 0;
    int32_t e_digest = 0;
    int32_t post_fp = 0;
    FamilyFingerprints pre_families;
    FamilyFingerprints post_families;
    int32_t topo_fp = 0;
    int32_t witness_fp = 0;
    int32_t raw_fp = 0;
    uint8_t evidence_mask = 0;
    bool present = false;
};

class Journal {
    int capacity = JOURNAL_CAPACITY_DEFAULT;
    int64_t ring_epoch = -1;
    int count = 0;
    int start = 0;

    LocalVector<int64_t> transitions;
    LocalVector<int64_t> labels;
    LocalVector<int32_t> c_hashes;
    LocalVector<int32_t> e_digests;
    LocalVector<int32_t> pre_fps;
    LocalVector<int32_t> topo_fps;
    LocalVector<int32_t> raw_fps;
    LocalVector<int32_t> witness_fps;
    LocalVector<uint8_t> witness_class_bits;
    LocalVector<uint8_t> witness_realization_bits;
    LocalVector<uint8_t> witness_states;
    LocalVector<float> aligned_errors;
    LocalVector<int32_t> post_fps;
    LocalVector<int32_t> pre_pose_fps;
    LocalVector<int32_t> pre_momentum_fps;
    LocalVector<int32_t> pre_controller_fps;
    LocalVector<int32_t> post_pose_fps;
    LocalVector<int32_t> post_momentum_fps;
    LocalVector<int32_t> post_controller_fps;
    LocalVector<int32_t> episode_ids;
    LocalVector<int32_t> write_ids;
    LocalVector<uint8_t> operators;
    LocalVector<int64_t> bases;
    LocalVector<uint8_t> differing_families;
    LocalVector<uint8_t> evidence_masks;
    LocalVector<uint8_t> kinds;
    LocalVector<uint8_t> domains;
    LocalVector<uint8_t> attributions;
    LocalVector<uint8_t> flags;

    int next_slot();

public:
    explicit Journal(int p_capacity = JOURNAL_CAPACITY_DEFAULT);

    // Newest first, because every caller addresses a recent transition.
    int slot_of(int64_t p_transition) const;

    // Oldest-first index shared by every public column reader.
    int index_of(int64_t p_transition) const;

    void open(int64_t p_transition, const JournalOpen &p_row);
    void mark_solve(
        int64_t p_transition,
        int32_t p_topo_fp,
        int32_t p_witness_fp,
        uint8_t p_evidence_mask,
        uint8_t p_witness_class_bits,
        uint8_t p_witness_realization_bits = 0,
        uint8_t p_witness_state = 0
    );
    void close(
        int64_t p_transition,
        int32_t p_post_fp,
        const FamilyFingerprints &p_post_families
    );
    void mark_chain_broken(int64_t p_transition);
    void mark_ack(int64_t p_transition, bool p_matched);
    void mark_divergent(int64_t p_transition);
    void mark_witness_match(int64_t p_transition, bool p_matched);
    bool witness_judged(int64_t p_transition) const;
    void mark_substituted(int64_t p_transition);
    void mark_superseded(int64_t p_transition);
    void mark_domain(int64_t p_transition, Domain p_domain);
    void mark_e_digest(int64_t p_transition, int32_t p_e_digest);
    void mark_provenance(
        int64_t p_transition,
        int p_episode_id,
        int p_write_id,
        Operator p_operator,
        int64_t p_basis
    );
    void mark_attribution(int64_t p_transition, Attribution p_attribution);
    void mark_aligned_error(int64_t p_transition, double p_error);
    void mark_differing_family(int64_t p_transition, DifferingFamily p_family);

    // The frontier an acknowledgement may claim: an open row holds a zero
    // fingerprint, so claiming it would assert a state no drive has produced.
    int64_t last_closed() const;
    int64_t first_unmatched() const;
    int64_t first_chain_break() const;

    void clear(int64_t p_epoch);

    int size() const {
        return count;
    }

    int capacity_limit() const {
        return capacity;
    }

    int64_t epoch() const {
        return ring_epoch;
    }

    // Oldest first, which is the order every column read shares, so index `i`
    // names one row across all of them.
    int64_t transition_at(int p_index) const;
    int64_t label_at(int p_index) const;
    uint8_t kind_at(int p_index) const;
    int32_t c_hash_at(int p_index) const;
    int32_t e_digest_at(int p_index) const;
    int32_t pre_fp_at(int p_index) const;
    int32_t post_fp_at(int p_index) const;
    int32_t topo_fp_at(int p_index) const;
    int32_t raw_fp_at(int p_index) const;
    int32_t witness_fp_at(int p_index) const;
    uint8_t witness_class_bits_at(int p_index) const;
    uint8_t witness_realization_bits_at(int p_index) const;
    uint8_t witness_state_at(int p_index) const;
    FamilyFingerprints pre_families_at(int p_index) const;
    FamilyFingerprints post_families_at(int p_index) const;
    int32_t episode_id_at(int p_index) const;
    int32_t write_id_at(int p_index) const;
    uint8_t operator_at(int p_index) const;
    int64_t basis_at(int p_index) const;
    uint8_t evidence_mask_at(int p_index) const;
    uint8_t flags_at(int p_index) const;
    uint8_t domain_at(int p_index) const;
    uint8_t attribution_at(int p_index) const;
    uint8_t differing_family_at(int p_index) const;
    float aligned_error_at(int p_index) const;

    // A transition nobody retained is one nobody can show was entitled to
    // exactness, so a forgotten row reads as the label that claims nothing.
    Domain domain_of(int64_t p_transition) const;
    uint8_t flags_of(int64_t p_transition) const;
    Attribution attribution_of(int64_t p_transition) const;
    int32_t post_fp_of(int64_t p_transition) const;
    JournalEvidence evidence_of(int64_t p_transition) const;
};

} // namespace predict

} // namespace netw
