#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/resolve.hpp"
#include "netw/sim/select.hpp"

namespace netw::sim {

enum class Schedule : uint8_t {
    TICK = 0,
    FRAME = 1,
    STEPPED = 2,
    AUTO = 3,
};

struct Declaration {
    godot::Vector<godot::NodePath> bodies;
    Schedule schedule = Schedule::AUTO;
    Replicas replicas = Replicas::PROXY;
    Restore restore = Restore::BUFFERED;
    int max_restore_ticks = 6;
    godot::Callable step;
    bool claim_on_contact = false;
    double release_on_rest = 0.0;
};

struct Row {
    godot::RID entity;
    Declaration declaration;
    Facts facts;
    Mode mode = Mode::NONE;
    Bodies bodies;
    Installs installs;
    godot::Callable control_hook;
    int64_t last_frame_tick = -1;
    bool stepped_refused = false;
    Choice choice;
    Selection selection;
    IdSet selected_by;
    IdSet selecting;
};

Schedule resolved_schedule(Schedule p_declared, bool p_has_body);

using Seed = void (*)(void *p_context, Row &r_row);

class Rows {
    godot::HashMap<uint64_t, Row> rows;
    Seed seed = nullptr;
    void *seed_context = nullptr;

public:
    void seed_with(Seed p_seed, void *p_context);
    Row &ensure(const godot::RID &p_entity);
    Row *find(const godot::RID &p_entity);
    const Row *row_of(const godot::RID &p_entity) const;
    Mode resolve(const godot::RID &p_entity, const Facts &p_facts);
    godot::LocalVector<godot::RID> release(const godot::RID &p_entity);

    bool note_selected(const godot::RID &p_member, const godot::RID &p_subject);
    bool erase_selected(
        const godot::RID &p_member,
        const godot::RID &p_subject
    );
    uint32_t selection_count(const godot::RID &p_member) const;
    godot::LocalVector<godot::RID> drop_selections(const godot::RID &p_subject);
    godot::RID entity_of_key(int64_t p_key) const;
    void choosers(godot::LocalVector<godot::RID> &r_out) const;
    void hold_poses(
        const godot::RID &p_space,
        const godot::LocalVector<godot::RID> &p_members,
        godot::LocalVector<HeldPose> &r_poses
    ) const;

    godot::LocalVector<godot::RID> follow_session_authority(bool p_here);
    godot::LocalVector<godot::RID> holding() const;
    void unpredicted(godot::LocalVector<godot::RID> &r_out) const;
};

} // namespace netw::sim
