#pragma once

/* The session's prediction engines, one pool keyed by an opaque slot.
 *
 * A predicting entity's engine has identity and memory, and there is one per
 * predicting entity rather than one per session. Registering an object per
 * entity would hand GDScript a class that has to dissolve into slot-keyed
 * storage when the shell goes native, so the pool is registered and the
 * engines are rows in it, which is the shape `NetwInterestEngine` already
 * ships.
 *
 * [codeblock]
 * const int64_t slot = pool->open(declaration);
 * pool->rewire(slot, declaration);
 * pool->close(slot);
 * [/codeblock]
 *
 * `supports` is the flip axis. The pool declares which CONFIGURATIONS it can
 * drive, a caller asks before handing it a tick, and outside the declared set
 * it refuses rather than answering. That is what keeps an engine wholly native
 * or wholly GDScript while the port is in flight: there is never a mixed
 * engine, so no per-region hop is paid at runtime.
 */

#include <cstdint>

#include "godot/ref_counted.hpp"
#include "godot/rid.hpp"
#include "godot/variant.hpp"
#include "netw/predict/drive.hpp"
#include "netw/predict/episode_report.hpp"
#include "netw/predict/wiring.hpp"

namespace netw {

using namespace godot;

/* One declaration, built field by field by a shell that still speaks
 * `NetwPropertySetBinding`.
 *
 * Bound rather than passed as a Dictionary because a fixed-key Dictionary
 * across a seam is a second spelling of a record, and this one is read on
 * every rewire.
 */
class NetwPredictDeclaration : public RefCounted {
    GDCLASS(NetwPredictDeclaration, RefCounted)

    friend class NetwPredictionEngine;

    LocalVector<predict::FieldDecl> fields;

protected:
    static void _bind_methods();

public:
    void append_field(
        const StringName &p_key,
        int p_property_class,
        const StringName &p_carry_channel,
        double p_converge_stiffness,
        bool p_teleport_only,
        bool p_reconcile_only,
        double p_epsilon_override,
        double p_teleport_at,
        bool p_angle,
        const Ref<NetwQuantize> &p_quantizer = Ref<NetwQuantize>(),
        int p_type = int(Variant::NIL)
    );

    int field_count() const;
    StringName field_at(int p_index) const;
    void clear();
};

/* One input-consume decision, typed across the binding boundary.
 *
 * The command payload remains the shell's property row. This record decides
 * whether that row is eligible, missing, repeated, or driven.
 */
class NetwPredictConsumePlan : public RefCounted {
    GDCLASS(NetwPredictConsumePlan, RefCounted)

    friend class NetwPredictionEngine;

    predict::ConsumeInputPlan plan;

protected:
    static void _bind_methods();

public:
    bool eligible() const;
    bool missing() const;
    bool run() const;
    bool use_last() const;
    int kind() const;
};

/* What one pass did, as the record the GDScript arm reads it back through.
 *
 * Bound rather than returned as a Dictionary for the reason every other record
 * in this family is: a fixed-key Dictionary across a seam is a second spelling
 * of a struct, and this one is read once per entity per tick.
 */
class NetwPredictDrive : public RefCounted {
    GDCLASS(NetwPredictDrive, RefCounted)

    friend class NetwPredictionEngine;

    predict::DriveRecord record;

protected:
    static void _bind_methods();

public:
    int64_t transition() const {
        return record.transition;
    }

    int64_t label() const {
        return record.label;
    }

    int kind() const {
        return int(record.kind);
    }

    bool ran() const {
        return record.ran;
    }

    bool fresh() const {
        return record.fresh;
    }

    bool held() const {
        return record.held;
    }

    bool clamped() const {
        return record.clamped;
    }
};

class NetwPredictCarryContext : public RefCounted {
    GDCLASS(NetwPredictCarryContext, RefCounted)

    friend class NetwPredictionEngine;

    Dictionary state_at;
    Dictionary input_at;
    double step_delta = 0.0;
    int64_t authored_label = -1;

protected:
    static void _bind_methods();

public:
    Dictionary get_state() const {
        return state_at;
    }

    Dictionary get_input() const {
        return input_at;
    }

    double get_delta() const {
        return step_delta;
    }

    int64_t get_label() const {
        return authored_label;
    }
};

class NetwPredictCarryAttempt : public RefCounted {
    GDCLASS(NetwPredictCarryAttempt, RefCounted)

    friend class NetwPredictionEngine;

    predict::CarryAttempt record;

protected:
    static void _bind_methods();

public:
    Variant value() const {
        return record.value;
    }

    bool evidence() const {
        return record.evidence;
    }

    bool same_type() const {
        return record.probe.same_type;
    }

    bool finite() const {
        return record.probe.finite;
    }

    bool within_envelope() const {
        return record.probe.within_envelope;
    }

    bool pure() const {
        return record.probe.pure;
    }

    bool faithful() const {
        return record.probe.faithful;
    }

    double residual() const {
        return record.residual;
    }

    double tolerance() const {
        return record.tolerance;
    }
};

class NetwPredictReplayEntry : public RefCounted {
    GDCLASS(NetwPredictReplayEntry, RefCounted)

    friend class NetwPredictionEngine;

    predict::ReplayEntry record;

protected:
    static void _bind_methods();

public:
    int64_t index() const {
        return record.index;
    }

    int64_t label() const {
        return record.label;
    }

    Dictionary input() const {
        return record.input;
    }
};

class NetwPredictEvidence : public RefCounted {
    GDCLASS(NetwPredictEvidence, RefCounted)

    friend class NetwPredictionEngine;

    predict::EvidenceRow row;

protected:
    static void _bind_methods();

public:
    void fill(
        int64_t p_pre_fp,
        int64_t p_c_hash,
        int64_t p_e_digest,
        int64_t p_post_fp,
        int64_t p_pre_pose_fp,
        int64_t p_pre_momentum_fp,
        int64_t p_pre_controller_fp,
        int64_t p_post_pose_fp,
        int64_t p_post_momentum_fp,
        int64_t p_post_controller_fp,
        int64_t p_topo_fp,
        int64_t p_witness_fp,
        int64_t p_raw_fp,
        int p_evidence_mask,
        bool p_complete
    );
};

/* A detached typed journal row.
 *
 * The pool remains the only owner. A reader receives one immutable snapshot,
 * so no per-entity object or fixed-key Dictionary becomes a second journal.
 */
class NetwPredictJournalRow : public RefCounted {
    GDCLASS(NetwPredictJournalRow, RefCounted)

    friend class NetwPredictionEngine;

    int64_t transition_value = -1;
    int64_t label_value = -1;
    int kind_value = 0;
    int64_t c_hash_value = 0;
    int64_t e_digest_value = 0;
    int64_t pre_fp_value = 0;
    int64_t topo_fp_value = 0;
    int64_t raw_fp_value = 0;
    int64_t witness_fp_value = 0;
    int witness_class_bits_value = 0;
    double aligned_error_value = 0.0;
    int evidence_mask_value = 0;
    PackedInt32Array pre_families_value;
    int64_t post_fp_value = 0;
    PackedInt32Array post_families_value;
    int episode_id_value = 0;
    int write_id_value = 0;
    int operator_value = 0;
    int64_t basis_value = -1;
    int differing_family_value = 0;
    int domain_value = 0;
    int attribution_value = 0;
    int flags_value = 0;

protected:
    static void _bind_methods();

public:
    int64_t transition() const;
    int64_t label() const;
    int kind() const;
    int64_t c_hash() const;
    int64_t e_digest() const;
    int64_t pre_fp() const;
    int64_t topo_fp() const;
    int64_t raw_fp() const;
    int64_t witness_fp() const;
    int witness_class_bits() const;
    double aligned_error() const;
    int evidence_mask() const;
    PackedInt32Array pre_families() const;
    int64_t post_fp() const;
    PackedInt32Array post_families() const;
    int episode_id() const;
    int write_id() const;
    int op() const;
    int64_t basis() const;
    int differing_family() const;
    int domain() const;
    int attribution() const;
    int flags() const;
};

class NetwPredictRecoveryRequest : public RefCounted {
    GDCLASS(NetwPredictRecoveryRequest, RefCounted)

    friend class NetwPredictionEngine;

    Array predicted;
    Array authority;
    Array current;
    PackedFloat64Array field_errors;
    int64_t basis = -1;
    int64_t current_label = -1;
    int policy = 0;
    double fallback_epsilon = 0.0;
    double fallback_teleport = 0.0;
    int max_restore_ticks = 0;
    int ack_age_ticks = 0;
    int collision_cooldown_ticks = 0;
    double tick_delta = 1.0 / 60.0;
    int domain = 1;
    int attribution = 0;
    bool contact_window = false;
    bool suppressed = false;
    bool pose_unmeasured = false;

protected:
    static void _bind_methods();

public:
    predict::RecoveryRequest build(int p_width) const;
    void fill(
        const Array &p_predicted,
        const Array &p_authority,
        const Array &p_current,
        const PackedFloat64Array &p_field_errors,
        int64_t p_basis,
        int64_t p_current_label,
        int p_policy,
        double p_fallback_epsilon,
        double p_fallback_teleport,
        int p_max_restore_ticks,
        int p_ack_age_ticks,
        int p_collision_cooldown_ticks,
        double p_tick_delta,
        int p_domain,
        int p_attribution,
        bool p_contact_window,
        bool p_suppressed,
        bool p_pose_unmeasured
    );
};

class NetwPredictWritePlan : public RefCounted {
    GDCLASS(NetwPredictWritePlan, RefCounted)

    friend class NetwPredictionEngine;

    predict::WritePlan plan;

protected:
    static void _bind_methods();

public:
    int64_t basis() const;
    int64_t replay_from() const;
    int64_t replay_through() const;
    int operator_kind() const;
    bool teleport() const;
    bool skip() const;
    bool escalated() const;
    bool restore_has(int p_field) const;
    Variant restore_at(int p_field) const;
    bool write_has(int p_field) const;
    Variant write_at(int p_field) const;
};

class NetwPredictVerdict : public RefCounted {
    GDCLASS(NetwPredictVerdict, RefCounted)

    friend class NetwPredictionEngine;

    predict::StateVerdict state;
    predict::AckVerdict ack;
    bool ack_verdict = false;

protected:
    static void _bind_methods();

public:
    int64_t transition() const;
    int64_t recv_tick() const;
    int exact() const;
    int domain() const;
    int attribution() const;
    int differing_family() const;
    bool compared() const;
    bool evidence_complete() const;
    bool corrected() const;
    bool settled() const;
    double divergence() const;
    int meter() const;
    PackedFloat64Array field_errors() const;
};

class NetwPredictJointPlan : public RefCounted {
    GDCLASS(NetwPredictJointPlan, RefCounted)

    friend class NetwPredictionEngine;

    predict::JointPassPlan plan;

protected:
    static void _bind_methods();

public:
    int64_t floor() const;
    int64_t present() const;
    bool heal() const;
    bool valid() const;
    int restore_count() const;
    int64_t restore_slot(int p_index) const;
    bool restore_has(int p_index, int p_field) const;
    Variant restore_at(int p_index, int p_field) const;
    int step_count() const;
    int64_t step_slot(int p_index) const;
    int64_t step_transition(int p_index) const;
    Variant step_command(int p_index) const;
    int step_provenance(int p_index) const;
};

/* The pool. One registered object, N engines, keyed by an opaque slot.
 *
 * A slot is minted here and never reused, so a late row naming a closed slot
 * resolves to nothing rather than to whichever engine took its number.
 */
class NetwPredictionEngine : public RefCounted {
    GDCLASS(NetwPredictionEngine, RefCounted)

    HashMap<int64_t, predict::Slot> rows;
    int64_t next_slot = 1;
    int depth = 0;
    int64_t mutations_refused = 0;

    bool roster_open(const char *p_verb);

    static Ref<NetwPredictEpisodeReport> report_of(
        const predict::Episode &p_episode
    );
    static Ref<NetwPredictWritePlan> plan_of(const predict::WritePlan &p_plan);
    static Ref<NetwPredictJointPlan> joint_plan_of(
        const predict::JointPassPlan &p_plan
    );
    const predict::Slot *row_of(int64_t p_slot) const;
    predict::Slot *mutable_row_of(int64_t p_slot);

    static Object *resolve_owner(predict::Slot &p_row);
    static void resolve_reach(predict::Slot &p_row, const Object *p_owner);
    // The fingerprint the purity bracket compares, over the causal scope alone:
    // a rule is forbidden to write the state a comparison judges, and nothing
    // else about the body is its business.
    static int32_t compared_fingerprint(
        predict::Slot &p_row,
        const Dictionary &p_payload
    );
    // Invokes p_rule once under the run_step discipline and refuses a
    // non-finite result rather than folding it forward. Answers a null Variant
    // for the refusal, which no rule can return by itself.
    Variant call_carry(
        int64_t p_slot,
        const Callable &p_rule,
        const Variant &p_value,
        const predict::ReplayEntry &p_entry,
        const Dictionary &p_state
    );
    /* Replays the rule over one transition the owner already recorded and asks
     * whether it reaches the state that transition actually reached.
     *
     * This is the only check that can see a rule reading the live world instead
     * of the transition's, or one whose arithmetic is simply wrong, because
     * both reproduce the recorded past incorrectly while looking entirely
     * reasonable at the moment of the write. One transition per recovery is
     * enough: a rule that disagrees does so on most of them, and the retirement
     * counter integrates.
     */
    void replay_carry(
        predict::CarryAttempt &r_attempt,
        int64_t p_slot,
        const Callable &p_rule,
        const StringName &p_field,
        int p_field_slot,
        const LocalVector<predict::ReplayEntry> &p_entries,
        bool p_angle,
        double p_divergence_epsilon
    );
    /* Folds the rule across every entry onto the acknowledged value.
     *
     * A step that returns the wrong type, a non-finite value, or lands further
     * from the acknowledged value than a teleport would move the body is not
     * advancing it, whatever it computed. The envelope is THIS field's declared
     * distance, because the rule advances one field in one unit and the entity
     * default is only the right number for it by coincidence.
     */
    void fold_carry(
        predict::CarryAttempt &r_attempt,
        int64_t p_slot,
        const Callable &p_rule,
        int p_field_slot,
        const Variant &p_start,
        const LocalVector<predict::ReplayEntry> &p_entries,
        bool p_angle,
        double p_teleport_default
    );
    static Dictionary capture_through(
        predict::Slot &p_row,
        const predict::FieldCodec &p_codec,
        const LocalVector<uint8_t> &p_reach
    );
    static bool apply_through(
        predict::Slot &p_row,
        const predict::FieldCodec &p_codec,
        const LocalVector<uint8_t> &p_reach,
        const Dictionary &p_payload
    );

protected:
    static void _bind_methods();

public:
    int64_t open(const Ref<NetwPredictDeclaration> &p_declaration);
    void rewire(
        int64_t p_slot,
        const Ref<NetwPredictDeclaration> &p_declaration,
        const Ref<NetwPredictDeclaration> &p_input
        = Ref<NetwPredictDeclaration>()
    );
    void close(int64_t p_slot);

    bool bind_owner(int64_t p_slot, Object *p_owner);
    void unbind_owner(int64_t p_slot);
    bool owner_bound(int64_t p_slot) const;
    bool owner_solves(int64_t p_slot) const;

    Dictionary capture_state(int64_t p_slot);
    Dictionary capture_input(int64_t p_slot);
    bool apply_state(int64_t p_slot, const Dictionary &p_payload);
    bool apply_input(int64_t p_slot, const Dictionary &p_payload);

    void set_simulate(int64_t p_slot, const Callable &p_callable);
    void set_witness(int64_t p_slot, const Callable &p_callable);
    void set_corridor(int64_t p_slot, const Callable &p_callable);
    void set_sensor(
        int64_t p_slot,
        const StringName &p_name,
        const Callable &p_callable
    );
    void set_carry(
        int64_t p_slot,
        const StringName &p_field,
        const Callable &p_callable
    );
    bool has_simulate(int64_t p_slot) const;

    void set_order_key(int64_t p_slot, int64_t p_order_key);
    int64_t order_key_of(int64_t p_slot) const;
    PackedInt64Array ordered_slots() const;

    enum PassPhase {
        PASS_ISLAND_TICK = 0,
        PASS_ISLAND_FRAME = 1,
        PASS_JOINT = 2,
        PASS_TICK = 3,
        PASS_FRAME = 4,
        PASS_FINALIZE_FRAME = 5,
    };

    PackedInt64Array pass_slots(int p_phase) const;

    Dictionary run_step(
        int64_t p_slot,
        const Dictionary &p_input,
        double p_delta,
        int64_t p_tick,
        bool p_fresh
    );

    int pass_depth() const;
    int64_t mutations_refused_count() const;

    Dictionary canonicalize_state(
        int64_t p_slot,
        const Dictionary &p_payload
    ) const;
    Dictionary canonicalize_input(
        int64_t p_slot,
        const Dictionary &p_payload
    ) const;
    Dictionary coast_command(int64_t p_slot) const;

    int64_t sample_environment(int64_t p_slot, int64_t p_epoch);
    Dictionary sensor_samples(int64_t p_slot) const;
    PackedByteArray canonical_state_bytes(
        int64_t p_slot,
        const Dictionary &p_payload
    ) const;
    PackedByteArray canonical_input_bytes(
        int64_t p_slot,
        const Dictionary &p_payload
    ) const;

    bool is_open(int64_t p_slot) const;
    int open_count() const;

    bool supports(
        int p_schedule,
        int p_role,
        int p_correction,
        int p_restore,
        int p_island = 0,
        bool p_carry = false,
        bool p_witness = false
    ) const;

    bool configure(
        int64_t p_slot,
        int p_schedule,
        int p_role,
        int p_correction,
        int p_restore,
        int p_max_restore_ticks = 6,
        int p_island = 0,
        bool p_carry = false,
        bool p_witness = false
    );

    int schedule_of(int64_t p_slot) const;
    int role_of(int64_t p_slot) const;
    int island_of(int64_t p_slot) const;

    Ref<NetwPredictConsumePlan> plan_consume_input(
        int p_schedule,
        bool p_has_input,
        bool p_has_later_input,
        bool p_has_last_input,
        int p_missing_policy
    ) const;

    int field_count(int64_t p_slot) const;
    int field_slot(int64_t p_slot, const StringName &p_key) const;
    StringName field_name(int64_t p_slot, int p_field) const;

    int projection_of(int64_t p_slot, int p_field) const;
    int state_family_of(int64_t p_slot, int p_field) const;
    double converge_rate_of(int64_t p_slot, int p_field) const;
    double epsilon_of(int64_t p_slot, int p_field) const;
    double teleport_of(int64_t p_slot, int p_field) const;
    bool is_pose(int64_t p_slot, int p_field) const;
    bool is_withheld(int64_t p_slot, int p_field) const;
    bool is_trigger_excluded(int64_t p_slot, int p_field) const;
    bool is_vote_excluded(int64_t p_slot, int p_field) const;
    bool is_angle(int64_t p_slot, int p_field) const;
    bool is_causal(int64_t p_slot, int p_field) const;

    void record_input(int64_t p_slot, int64_t p_tick, int64_t p_c_hash);

    Ref<NetwPredictDrive> open_drive(
        int64_t p_slot,
        const Dictionary &p_topology,
        int64_t p_tick,
        int64_t p_frame,
        double p_ticktime,
        int p_quantum,
        bool p_simulating,
        int64_t p_pre_fp,
        int64_t p_pre_pose_fp,
        int64_t p_pre_momentum_fp,
        int64_t p_pre_controller_fp,
        int64_t p_raw_fp = 0,
        int p_evidence_mask = 0
    );

    Ref<NetwPredictDrive> replay_drive(
        int64_t p_slot,
        const Dictionary &p_topology,
        int64_t p_transition,
        int64_t p_label,
        int p_kind,
        int64_t p_tick,
        int64_t p_frame,
        double p_ticktime,
        int p_quantum,
        int64_t p_pre_fp,
        int64_t p_pre_pose_fp,
        int64_t p_pre_momentum_fp,
        int64_t p_pre_controller_fp,
        int64_t p_raw_fp = 0,
        int p_evidence_mask = 0,
        bool p_authoring = false
    );

    bool record_evidence(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_environment_epoch,
        const Dictionary &p_environment,
        const Dictionary &p_topology,
        const PackedStringArray &p_contact_ids,
        const PackedInt32Array &p_witness_classes,
        const PackedInt32Array &p_realizations,
        const PackedByteArray &p_outside_boundary,
        bool p_sleeping
    );

    int resolve_correction(int64_t p_slot, int p_declared) const;

    void record_episode_decision(
        int64_t p_slot,
        int p_operator,
        int64_t p_basis,
        bool p_eligible,
        bool p_applied,
        const Dictionary &p_eligibility
    );

    bool open_episode(int64_t p_slot, int64_t p_transition, int p_attribution);
    void record_episode_divergence(int64_t p_slot, int64_t p_transition);
    int escalation_field_of(
        int64_t p_slot,
        const Array &p_predicted,
        const Array &p_authority,
        const PackedFloat64Array &p_field_errors,
        double p_fallback_epsilon
    ) const;
    int trigger_shape_of(
        int64_t p_slot,
        const PackedFloat64Array &p_field_errors,
        double p_fallback_epsilon
    ) const;
    void record_episode_escalation(int64_t p_slot, int p_trigger_shape);
    void stamp_episode_write_delta(int64_t p_slot, int p_delta_fp);
    int record_episode_write(
        int64_t p_slot,
        int p_operator,
        int64_t p_basis,
        int p_delta_fp,
        const StringName &p_target,
        int p_ack_age,
        int p_trigger_shape,
        bool p_evidence_free,
        bool p_null_operator
    );

    bool episode_active(int64_t p_slot) const;
    int episode_state(int64_t p_slot) const;

    enum EpisodeStat {
        STAT_EPISODE_ID = 0,
        STAT_EPISODE_STATE = 1,
        STAT_EPISODE_OPENED = 2,
        STAT_EPISODE_ATTRIBUTION = 3,
        STAT_EPISODE_NON_CONTRACTION = 4,
        STAT_EPISODE_WITHHELD_NC = 5,
        STAT_EPISODE_EVIDENCE_FREE_NC = 6,
        STAT_EPISODE_NO_TRIGGER_NC = 7,
        STAT_EPISODE_MIXED_TRIGGER_NC = 8,
        STAT_EPISODE_CLOSURE_USED = 9,
        STAT_EPISODE_AGREEMENT_RUN = 10,
        STAT_EPISODE_LAST_COMPARISON = 11,
        STAT_EPISODE_AGREEMENT_WRITE_ID = 12,
        STAT_EPISODE_LAST_WRITE_ID = 13,
        STAT_EPISODE_EVIDENCE_DROPPED = 14,
        STAT_EPISODE_WRITE_COUNT = 15,
        STAT_EPISODE_COMPARISON_COUNT = 16,
        STAT_EPISODE_ACTIVE = 17,
        STAT_EPISODE_TRANSPORT_DECIDED = 18,
        STAT_EPISODE_DISSIPATE_DECIDED = 19,
        STAT_EPISODE_COUNT = 20,
    };

    PackedInt64Array episode_stats(int64_t p_slot) const;
    bool episode_budget_exhausted(int64_t p_slot) const;
    bool episode_operator_pending(int64_t p_slot, int p_operator) const;

    void record_breach(int64_t p_slot, int64_t p_transition);

    bool witness_row_clean(
        int64_t p_slot,
        int64_t p_transition,
        bool p_require_peer
    ) const;

    bool transport_admissible(
        int64_t p_slot,
        bool p_candidate,
        bool p_basis_witness_clean,
        bool p_recent_witness_clean,
        bool p_non_pose_agrees,
        bool p_below_teleport,
        bool p_escalated,
        bool p_observing
    ) const;
    bool dissipate_admissible(
        int64_t p_slot,
        bool p_momentum_active,
        bool p_other_active,
        bool p_basis_witness_clean,
        bool p_recent_witness_clean,
        bool p_escalated,
        bool p_observing,
        int p_meter
    ) const;

    bool dissipate_declared(int64_t p_slot) const;

    bool static_geometry(Object *p_collider) const;
    int witness_class(Object *p_collider, bool p_declared_support) const;

    int judge_carry(
        int64_t p_slot,
        const StringName &p_field,
        bool p_same_type,
        bool p_finite,
        bool p_within_envelope,
        bool p_pure,
        bool p_faithful
    );
    int decline_carry(int64_t p_slot, const StringName &p_field);

    bool carry_eligible(int64_t p_slot, const StringName &p_field) const;
    bool carry_retired(int64_t p_slot, const StringName &p_field) const;
    PackedInt64Array carry_stats(int64_t p_slot, const StringName &p_field)
        const;

    void mark_carry_dirty(int64_t p_slot, int64_t p_transition);
    bool carry_judgeable(int64_t p_slot, int64_t p_transition) const;

    Ref<NetwPredictCarryAttempt> attempt_carry(
        int64_t p_slot,
        const StringName &p_field,
        const Variant &p_acknowledged,
        int64_t p_basis,
        double p_teleport_default,
        double p_divergence_epsilon
    );

    TypedArray<NetwPredictReplayEntry> replay_entries(
        int64_t p_slot,
        int64_t p_basis
    ) const;
    Dictionary state_before(int64_t p_slot, int64_t p_transition) const;

    void close_drive(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_post_fp,
        int64_t p_post_pose_fp,
        int64_t p_post_momentum_fp,
        int64_t p_post_controller_fp
    );

    void mark_domain(int64_t p_slot, int64_t p_transition, int p_domain);
    void mark_chain_broken(int64_t p_slot, int64_t p_transition);
    void mark_provenance(
        int64_t p_slot,
        int64_t p_transition,
        int p_episode_id,
        int p_write_id,
        int p_operator,
        int64_t p_basis
    );
    void mark_attribution(
        int64_t p_slot,
        int64_t p_transition,
        int p_attribution
    );
    void mark_differing_family(
        int64_t p_slot,
        int64_t p_transition,
        int p_family
    );
    void mark_solve(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_topo_fp,
        int64_t p_witness_fp,
        int p_evidence_mask,
        int p_witness_class_bits
    );
    bool witness_judged(int64_t p_slot, int64_t p_transition) const;
    void mark_witness_match(
        int64_t p_slot,
        int64_t p_transition,
        bool p_matched
    );
    void declare_skipped(int64_t p_slot, int64_t p_transition, int64_t p_label);

    void mark_aligned_error(
        int64_t p_slot,
        int64_t p_transition,
        double p_error
    );

    void acknowledge(int64_t p_slot, int64_t p_transition, bool p_matched);

    Ref<NetwPredictVerdict> admit_ack(
        int64_t p_slot,
        int64_t p_transition,
        const Ref<NetwPredictEvidence> &p_evidence,
        bool p_substituted
    );

    Ref<NetwPredictVerdict> compare_state(
        int64_t p_slot,
        int64_t p_recv_tick,
        int64_t p_transition,
        const Array &p_predicted,
        const Array &p_authority,
        const PackedFloat64Array &p_correction_tolerances,
        const PackedFloat64Array &p_meter_tolerances,
        double p_fallback_epsilon,
        bool p_stream_reconstructed,
        bool p_ack_domain_confirmed
    );

    void set_state(int64_t p_slot, const Array &p_state);
    bool state_has(int64_t p_slot, int p_field) const;
    Variant state_at(int64_t p_slot, int p_field) const;
    Ref<NetwPredictWritePlan> recover(
        int64_t p_slot,
        const Ref<NetwPredictRecoveryRequest> &p_request
    );
    void open_recovery_window(int64_t p_slot, int64_t p_label, int p_cooldown);
    void suppress_recovery_until(
        int64_t p_slot,
        int64_t p_label,
        int p_cooldown
    );
    bool escalation_pending(int64_t p_slot) const;
    int64_t recovery_window_until(int64_t p_slot) const;
    int64_t recovery_cooldown_until(int64_t p_slot) const;
    Ref<NetwPredictEpisodeReport> episode(int64_t p_slot) const;
    void enter_quarantine(
        int64_t p_slot,
        int64_t p_transition,
        bool p_stream_reconstructed,
        int p_attribution,
        bool p_demoted
    );
    Ref<NetwPredictWritePlan> quarantine_state(
        int64_t p_slot,
        int64_t p_tick,
        int64_t p_basis,
        const Array &p_payload,
        bool p_whole
    );
    Ref<NetwPredictWritePlan> quarantine_witness(
        int64_t p_slot,
        int64_t p_basis,
        int p_bits
    );
    void confirm_reseed_epoch(int64_t p_slot);
    Ref<NetwPredictWritePlan> align_reseed(
        int64_t p_slot,
        int64_t p_transition,
        const Array &p_payload,
        int64_t p_ignore_through
    );
    bool admit_post_reseed(int64_t p_slot, int64_t p_basis);
    void adopt_alignment(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_ignore_through
    );
    bool finish_probation(int64_t p_slot, bool p_corrected);
    bool quarantine_latched(int64_t p_slot) const;
    int quarantine_clean_run(int64_t p_slot) const;
    int quarantine_target(int64_t p_slot) const;
    bool reseed_align_pending(int64_t p_slot) const;
    bool reseed_epoch_confirmed(int64_t p_slot) const;
    bool probation_pending(int64_t p_slot) const;
    int64_t reseed_ignore_through(int64_t p_slot) const;

    PackedInt64Array island_commit(
        int64_t p_slot,
        int64_t p_owner_order_key,
        const PackedInt64Array &p_members,
        const PackedInt64Array &p_order_keys,
        const PackedFloat64Array &p_distance_squared,
        const PackedInt32Array &p_fidelities,
        const PackedByteArray &p_eligible,
        const PackedByteArray &p_contact,
        int p_promotion,
        int p_promotion_count,
        double p_promotion_meters,
        int64_t p_frontier
    );
    int island_member_count(int64_t p_slot) const;
    bool island_promoted(int64_t p_slot, int64_t p_member) const;
    int64_t tenure_begin(int64_t p_slot) const;
    int64_t tenure_end(int64_t p_slot) const;
    void joint_record(
        int64_t p_slot,
        int64_t p_transition,
        const Array &p_state,
        const Variant &p_command,
        bool p_authored,
        bool p_relayed,
        bool p_predictor_valid
    );
    void joint_note_basis(int64_t p_slot, int64_t p_basis, int p_source);
    void joint_clear(int64_t p_slot);
    Variant joint_command_at(int64_t p_slot, int64_t p_transition) const;
    int joint_provenance_at(int64_t p_slot, int64_t p_transition) const;
    Ref<NetwPredictJointPlan> joint_pass(int64_t p_slot, int64_t p_present);
    PackedInt64Array joint_stats(int64_t p_slot) const;

    enum IslandMode {
        ISLAND_NONE = 0,
        ISLAND_DECLARED = 1,
        ISLAND_JOINT = 2,
    };

    enum CarryVerdictBits {
        CARRY_CARRIED = 0,
        CARRY_DECLINED = 1,
        CARRY_UNFAITHFUL = 2,
        CARRY_RETIRED_ALREADY = 3,
        CARRY_RETIRED_SCHEDULE = 4,
        CARRY_RETIRED_IMPURE = 5,
        CARRY_RETIRED_INFIDELITY = 6,
    };

    enum JointFloorSource {
        JOINT_FLOOR_ACK = 0,
        JOINT_FLOOR_STATE = 1,
        JOINT_FLOOR_RELAY = 2,
        JOINT_FLOOR_EPOCH = 3,
    };

    enum JointStat {
        STAT_JOINT_PASSES = 0,
        STAT_JOINT_MEMBERS = 1,
        STAT_JOINT_CELLS_RELAYED = 2,
        STAT_JOINT_CELLS_SUBSTITUTED = 3,
        STAT_JOINT_HEAL_SNAPS = 4,
        STAT_JOINT_LINGER_HELD = 5,
        STAT_JOINT_FLOOR = 6,
        STAT_JOINT_PRESENT = 7,
        STAT_JOINT_MAX_DEPTH = 8,
        STAT_JOINT_FLOOR_ACK_MOVES = 9,
        STAT_JOINT_FLOOR_STATE_MOVES = 10,
        STAT_JOINT_FLOOR_RELAY_MOVES = 11,
        STAT_JOINT_FLOOR_EPOCH_MOVES = 12,
        STAT_JOINT_COUNT = 13,
    };

    int journal_size(int64_t p_slot) const;
    int64_t journal_epoch(int64_t p_slot) const;
    PackedInt64Array journal_transitions(int64_t p_slot) const;
    Ref<NetwPredictJournalRow> journal_row(
        int64_t p_slot,
        int64_t p_transition
    ) const;

    int journal_slot_of(int64_t p_slot, int64_t p_transition) const;

    int64_t journal_transition_at(int64_t p_slot, int p_index) const;
    int64_t journal_label_at(int64_t p_slot, int p_index) const;
    int journal_kind_at(int64_t p_slot, int p_index) const;
    int64_t journal_c_hash_at(int64_t p_slot, int p_index) const;
    int64_t journal_pre_fp_at(int64_t p_slot, int p_index) const;
    int64_t journal_post_fp_at(int64_t p_slot, int p_index) const;
    int64_t journal_environment_at(int64_t p_slot, int p_index) const;
    int64_t journal_topology_at(int64_t p_slot, int p_index) const;
    int64_t journal_raw_at(int64_t p_slot, int p_index) const;
    int64_t journal_witness_at(int64_t p_slot, int p_index) const;
    int journal_witness_class_at(int64_t p_slot, int p_index) const;
    PackedInt32Array journal_pre_families_at(
        int64_t p_slot,
        int p_index
    ) const;
    PackedInt32Array journal_post_families_at(
        int64_t p_slot,
        int p_index
    ) const;
    int journal_episode_id_at(int64_t p_slot, int p_index) const;
    int journal_write_id_at(int64_t p_slot, int p_index) const;
    int journal_operator_at(int64_t p_slot, int p_index) const;
    int64_t journal_basis_at(int64_t p_slot, int p_index) const;
    int journal_evidence_at(int64_t p_slot, int p_index) const;
    int journal_flags_at(int64_t p_slot, int p_index) const;
    int journal_domain_at(int64_t p_slot, int p_index) const;
    int journal_attribution_at(int64_t p_slot, int p_index) const;
    int journal_differing_family_at(int64_t p_slot, int p_index) const;
    double journal_aligned_error_at(int64_t p_slot, int p_index) const;
    int64_t journal_last_closed(int64_t p_slot) const;
    int64_t journal_first_unmatched(int64_t p_slot) const;
    int64_t journal_first_chain_break(int64_t p_slot) const;
    void journal_clear(int64_t p_slot, int64_t p_epoch);
    void tape_reset(int64_t p_slot, int64_t p_epoch);
    PackedByteArray build_ack_frame(
        int64_t p_slot,
        int p_epoch,
        int64_t p_ack,
        int64_t p_owner_ack_floor
    ) const;

    int tape_size(int64_t p_slot) const;
    PackedInt64Array tape_span(int64_t p_slot) const;
    int64_t tape_label_of(int64_t p_slot, int64_t p_index) const;
    bool tape_is_fresh(int64_t p_slot, int64_t p_index) const;
    void tape_prepare_tick(int64_t p_slot, int64_t p_tick);
    void tape_author(int64_t p_slot, int64_t p_label, bool p_fresh);

    void bind_timeline(int64_t p_slot, const Ref<NetwTimeline> &p_timeline);
    Ref<NetwTimeline> entry_history(int64_t p_slot) const;
    void trim_history(int64_t p_slot, int64_t p_ack);

    bool command_admit(
        int64_t p_slot,
        int64_t p_transition,
        int64_t p_label,
        bool p_fresh,
        const Dictionary &p_command
    );
    bool command_has(int64_t p_slot, int64_t p_transition) const;
    int64_t command_label_of(int64_t p_slot, int64_t p_transition) const;
    bool command_is_fresh(int64_t p_slot, int64_t p_transition) const;
    Dictionary command_payload_of(int64_t p_slot, int64_t p_transition) const;
    int command_depth_from(int64_t p_slot, int64_t p_cursor) const;
    PackedInt64Array command_transitions(int64_t p_slot) const;

    /* The drive counters, in one array a law and a plot both index by
     * [enum DriveStat].
     *
     * A verb per counter would be nine verbs answering one question, and the
     * telemetry reader wants them together anyway.
     */
    enum DriveStat {
        STAT_DRIVE_SEQ = 0,
        STAT_LAST_DRIVE_LABEL = 1,
        STAT_LAST_DRIVE_KIND = 2,
        STAT_TAPE_EPOCH = 3,
        STAT_TAPE_INDEX = 4,
        STAT_ACK_AGE_TICKS = 5,
        STAT_AUTHORING_CLAMPED = 6,
        STAT_SPECULATION_HELD = 7,
        STAT_CHAIN_BREAKS = 8,
        STAT_QUANTUM_STEPS = 9,
        STAT_QUANTUM_DECLARED = 10,
        STAT_QUANTUM_FAULTS = 11,
        // A resolve that found its owner gone. The slot unbound itself and the
        // pass continued without property I/O, so this is the only trace.
        STAT_OWNER_LOST = 12,
        STAT_COUNT = 13,
    };

    void record_idle_drive(int64_t p_slot, int64_t p_label, int p_kind);
    void record_authoring_clamp(int64_t p_slot);
    void record_speculation_hold(int64_t p_slot);
    void refresh_ack_age(int64_t p_slot);
    int mark_authority_ack(int64_t p_slot, int64_t p_transition);
    enum DriveCursor {
        CURSOR_TAPE_EPOCH = 0,
        CURSOR_NEXT_TAPE_ENTRY = 1,
        CURSOR_LAST_DRIVEN_ENTRY = 2,
        CURSOR_LATEST_INPUT_TICK = 3,
        CURSOR_LAST_DRIVEN_INPUT_TICK = 4,
        CURSOR_LAST_FRAME_TRANSITION_TICK = 5,
        CURSOR_COUNT = 6,
    };

    bool journal_has(int64_t p_slot, int64_t p_transition) const;
    PackedInt64Array drive_cursors(int64_t p_slot) const;
    PackedInt64Array drive_stats(int64_t p_slot) const;

    enum CompareStat {
        STAT_COMPARISONS_RAN = 0,
        STAT_COMPARISONS_SKIPPED = 1,
        STAT_FP_VERIFIED = 2,
        STAT_FP_MISMATCHES = 3,
        STAT_FIRST_DIVERGENT_TRANSITION = 4,
        STAT_COMPARE_COUNT = 5,
    };

    PackedInt64Array compare_stats(int64_t p_slot) const;
};

} // namespace netw

VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DriveStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::DriveCursor);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::CompareStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::IslandMode);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::PassPhase);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::CarryVerdictBits);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::EpisodeStat);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::JointFloorSource);
VARIANT_ENUM_CAST(netw::NetwPredictionEngine::JointStat);
