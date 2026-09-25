#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/object.hpp"
#include "netw/sim/body.hpp"

namespace netw::sim {

constexpr int CLAIM_CONTACTS_REPORTED = 8;
constexpr double CLAIM_MOVING_SPEED = 0.1;

struct Rest {
    int64_t asleep_since = -1;
    bool releasing = false;
};

struct Contact {
    Rest rest;
    godot::LocalVector<uint64_t> touching;
    godot::LocalVector<uint64_t> touched_now;
    godot::LocalVector<godot::ObjectID> claim_touching;
    int64_t fence_tick = -1;
    bool fence_met = true;
    bool eligible = false;
    bool monitored = false;
    bool warned = false;
};

void monitor_contacts(const Bodies &p_bodies);

void touching(
    const Bodies &p_bodies,
    godot::LocalVector<godot::ObjectID> &r_colliders
);

bool thawed(const Bodies &p_bodies);

bool moving(const Bodies &p_bodies);

bool rested(
    Rest &r_rest,
    bool p_resting,
    int64_t p_tick,
    int64_t p_ticks_needed
);

void note_touch(Contact &r_contact, uint64_t p_other);

void keep_touching(Contact &r_contact);

void settle_onsets(Contact &r_contact, int64_t p_tick);

bool fenced(Contact &r_contact, int64_t p_tick);

} // namespace netw::sim
