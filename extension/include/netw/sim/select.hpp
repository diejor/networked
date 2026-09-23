#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/string_name.hpp"

namespace netw::sim {

constexpr double EXIT_MARGIN_SQUARED = 1.21;

enum class Policy : uint8_t {
    NONE = 0,
    NEAREST = 1,
    WITHIN = 2,
    ALL = 3,
};

enum class Pick : int8_t {
    AUTOMATIC = -1,
    EXCLUDED = 0,
    CHOSEN = 1,
};

struct Tenure {
    int64_t begin = -1;
    int64_t end = -1;

    bool contains(int64_t p_transition) const;
};

struct Candidate {
    int64_t key = 0;
    int64_t order_key = 0;
    double distance_squared = 0.0;
    Pick pick = Pick::AUTOMATIC;
    bool eligible = false;
    bool contact = false;
};

struct Selected {
    int64_t key = 0;
    int64_t order_key = 0;
    double distance_squared = 0.0;
    Tenure tenure;
    bool chosen = false;
    bool promoted = false;
    bool lingering = false;
    bool present = false;
    bool eligible = false;
};

struct Selection {
    godot::LocalVector<Selected> members;
    int64_t owner_order_key = 0;
    Policy policy = Policy::NONE;
    int count = 0;
    double meters = 0.0;

    void commit(
        const godot::LocalVector<Candidate> &p_candidates,
        int64_t p_frontier
    );
    void release_lingering(int64_t p_floor);
    const Selected *member(int64_t p_key) const;
    int present_count() const;
    int promoted_count() const;
    int lingering_count() const;
};

struct Named {
    godot::RID entity;
    Pick pick = Pick::AUTOMATIC;
};

struct Choice {
    godot::LocalVector<Named> named;
    godot::LocalVector<godot::StringName> layers;
    Policy policy = Policy::NONE;
    int count = 0;
    double meters = 0.0;

    bool declared() const;
    const Named *named_of(const godot::RID &p_entity) const;
};

bool claims_exact(const Choice &p_choice, bool p_stepped);
bool choose(Choice &r_choice, const godot::RID &p_entity, Pick p_pick);
bool forget(Choice &r_choice, const godot::RID &p_entity);
void simulate_nearest(
    Choice &r_choice,
    int p_count,
    const godot::StringName &p_layer
);
void simulate_within(
    Choice &r_choice,
    double p_meters,
    const godot::StringName &p_layer
);
void simulate_all(Choice &r_choice, const godot::StringName &p_layer);
void simulate_none(Choice &r_choice);

struct IdSet {
    godot::LocalVector<uint64_t> ids;

    bool note(uint64_t p_id);
    bool erase(uint64_t p_id);

    uint32_t count() const {
        return ids.size();
    }
};

} // namespace netw::sim
