#pragma once

#include <cstdint>

#include "godot/callable.hpp"
#include "godot/hash_map.hpp"
#include "godot/local_vector.hpp"
#include "godot/rid.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/contact.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/resolve.hpp"
#include "netw/sim/select.hpp"
#include "netw/store/peers.hpp"

namespace netw {
class NetwEntity;
} // namespace netw

namespace netw::sim {

struct Steering {
    int64_t controller = 0;
    int64_t hold = 0;
    bool pending = false;
    bool claiming = false;
    bool immediate = false;

    bool operator==(const Steering &p_other) const {
        return controller == p_other.controller && hold == p_other.hold
            && pending == p_other.pending && claiming == p_other.claiming
            && immediate == p_other.immediate;
    }
};

bool steered_here(const Steering &p_steering, int64_t p_local_peer);

bool steered_locally(const Steering &p_steering, int64_t p_local_peer);

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
    NetwEntity *view = nullptr;
    Steering steering;
    Declaration declaration;
    Facts facts;
    Mode mode = Mode::NONE;
    Mode announced = Mode::NONE;
    bool has_announced = false;
    bool owner_ticks = false;
    Bodies bodies;
    Installs installs;
    Contact contact;
    godot::Callable control_hook;
    godot::Callable sleep_hook;
    uint32_t index = 0;
    uint64_t order = 0;
    int64_t contact_tick = -1;
    int64_t last_frame_tick = -1;
    bool stepped_refused = false;
    Choice choice;
    Selection selection;
    IdSet selected_by;
    IdSet selecting;
};

Schedule resolved_schedule(Schedule p_declared, bool p_has_body);

bool steps(const Row &p_row);

bool chooses(const Row &p_row);

bool contact_moded(const Row &p_row);

using Seed = void (*)(void *p_context, Row &r_row);

class Rows {
    godot::HashMap<uint64_t, Row> rows;
    godot::LocalVector<Row *> by_index;
    godot::LocalVector<uint32_t> free_index;
    godot::LocalVector<uint32_t> stale_list;
    godot::LocalVector<uint32_t> gathered;
    uint64_t next_order = 0;
    store::Bits stale;
    store::Bits due;
    store::Bits bodied_bits;
    store::Bits stepping_bits;
    store::Bits chooser_bits;
    store::Bits awake_bits;
    store::Bits resting_bits;
    Seed seed = nullptr;
    void *seed_context = nullptr;

    void mark(Row &r_row);
    void derive(Row &r_row);
    void gather(
        const store::Bits &p_bits,
        godot::LocalVector<godot::RID> &r_out
    );
    void sort_gathered();

public:
    void seed_with(Seed p_seed, void *p_context);
    Row &ensure(const godot::RID &p_entity);
    Row *find(const godot::RID &p_entity);
    Row *peek(const godot::RID &p_entity);
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
    void choosers(godot::LocalVector<godot::RID> &r_out);
    void hold_poses(
        const godot::RID &p_space,
        const godot::LocalVector<godot::RID> &p_members,
        godot::LocalVector<HeldPose> &r_poses
    ) const;

    godot::LocalVector<godot::RID> follow_session_authority(bool p_here);
    void stepping(godot::LocalVector<godot::RID> &r_out);

    void settle();
    void contact_walk(godot::LocalVector<uint32_t> &r_out);
    Row *at(uint32_t p_index) const;
    bool is_due(uint32_t p_index) const;
    bool is_awake(uint32_t p_index) const;
    bool is_resting(uint32_t p_index) const;
    bool is_bodied(uint32_t p_index) const;
    bool is_stepping(uint32_t p_index) const;
    bool is_chooser(uint32_t p_index) const;
    void set_awake(const Row &p_row, bool p_awake);
    void set_resting(const Row &p_row, bool p_resting);
    void each(godot::LocalVector<Row *> &r_out) const;
};

} // namespace netw::sim
