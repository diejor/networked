#pragma once

/* A replay group's deterministic scope and scheduling facts.
 *
 * The roster is ordered by the stable entity key the shell supplies. Promotion
 * opens and closes member tenure, and departed members linger until the replay
 * floor passes their last transition. The pass itself consumes the oldest
 * unincorporated basis and visits transitions before members.
 */

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/predict/compare.hpp"

namespace netw::predict {

using godot::LocalVector;

constexpr double ISLAND_EXIT_MARGIN_SQUARED = 1.21;

enum class Promotion : uint8_t {
    NONE = 0,
    NEAREST = 1,
    WITHIN = 2,
    ALL = 3,
};

enum class Fidelity : int8_t {
    UNDECLARED = -1,
    PROXY = 0,
    SIMULATED = 1,
};

enum class CellProvenance : uint8_t {
    COAST = 0,
    SUBSTITUTED = 1,
    RELAYED = 2,
    AUTHORED = 3,
};

struct JointFloorDecision {
    int64_t floor = -1;
    bool heal = false;
};

struct Tenure {
    int64_t begin = -1;
    int64_t end = -1;

    bool contains(int64_t p_transition) const;
};

struct IslandCandidate {
    int64_t slot = 0;
    int64_t order_key = 0;
    double distance_squared = 0.0;
    Fidelity fidelity = Fidelity::UNDECLARED;
    bool eligible = false;
    // Bodies the owner is touching this transition. A handoff across a live
    // contact changes the simulation under it, so the promotion a contacting
    // member already holds is deferred rather than applied.
    bool contact = false;
};

struct IslandMember {
    int64_t slot = 0;
    int64_t order_key = 0;
    double distance_squared = 0.0;
    Tenure tenure;
    bool explicit_simulation = false;
    bool promoted = false;
    bool lingering = false;
    bool present = false;
    bool eligible = false;
};

struct Island {
    LocalVector<IslandMember> members;
    int64_t owner_order_key = 0;
    Promotion promotion = Promotion::NONE;
    int promotion_count = 0;
    double promotion_meters = 0.0;

    void commit(
        const LocalVector<IslandCandidate> &p_candidates,
        int64_t p_frontier
    );
    void release_lingering(int64_t p_floor);
    const IslandMember *member(int64_t p_slot) const;
    int present_count() const;
    int promoted_count() const;
    int lingering_count() const;
};

struct JointStateRecord {
    StateRow state;
    int64_t transition = -1;
};

struct JointCommandRecord {
    godot::Variant command;
    int64_t transition = -1;
    CellProvenance provenance = CellProvenance::COAST;
};

struct JointTrack {
    LocalVector<JointStateRecord> states;
    LocalVector<JointCommandRecord> commands;
    Tenure tenure;
    int64_t basis = -1;
    int64_t relay_floor = -1;
    int64_t epoch_floor = -1;

    void record(
        int64_t p_transition,
        const StateRow &p_state,
        const godot::Variant &p_command,
        CellProvenance p_provenance
    );
    const StateRow *state_at(int64_t p_transition) const;
    const JointStateRecord *newest_state_at_or_before(
        int64_t p_transition
    ) const;
    const JointCommandRecord *command_at(int64_t p_transition) const;
    int64_t history_floor() const;
    bool moved() const;
    void clear_floors();
    // A re-keyed epoch renumbers every transition, so a cell retained across
    // one names a transition that no longer exists.
    void clear_cells();
};

struct JointRestore {
    StateRow state;
    int64_t slot = 0;
};

struct JointStep {
    godot::Variant command;
    int64_t slot = 0;
    int64_t transition = -1;
    CellProvenance provenance = CellProvenance::COAST;
};

struct JointPassPlan {
    LocalVector<JointRestore> restores;
    LocalVector<JointStep> steps;
    int64_t floor = -1;
    int64_t present = -1;
    bool heal = false;
    bool valid = false;

    JointPassPlan() = default;
    JointPassPlan(const JointPassPlan &p_other);
    JointPassPlan &operator=(const JointPassPlan &p_other);
};

struct JointStats {
    int passes = 0;
    int members = 0;
    int cells_relayed = 0;
    int cells_substituted = 0;
    int heal_snaps = 0;
    int linger_held = 0;
    int64_t floor = -1;
    int64_t present = -1;
    int max_depth = 0;
    int floor_ack_moves = 0;
    int floor_state_moves = 0;
    int floor_relay_moves = 0;
    int floor_epoch_moves = 0;
};

JointFloorDecision joint_floor(
    const LocalVector<int64_t> &p_bases,
    const LocalVector<int64_t> &p_relay_floors,
    int64_t p_epoch_floor,
    int64_t p_history_floor,
    int64_t p_present
);

CellProvenance joint_cell(
    bool p_authored,
    bool p_relayed,
    bool p_predictor_valid
);

} // namespace netw::predict
