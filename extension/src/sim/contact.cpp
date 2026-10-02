#include "netw/sim/contact.hpp"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/physics_server.hpp"
#include "godot/variant.hpp"

using namespace godot;

namespace netw::sim {

namespace {

const StringName &max_contacts_name() {
    static const StringName name("max_contacts_reported");
    return name;
}

const StringName &freeze_name() {
    static const StringName name("freeze");
    return name;
}

template <typename State>
void note_colliders(const State *p_state, LocalVector<ObjectID> &r_colliders) {
    if (p_state == nullptr) {
        return;
    }
    const int count = p_state->get_contact_count();
    for (int at = 0; at < count; ++at) {
        const ObjectID id(uint64_t(p_state->get_contact_collider_id(at)));
        if (r_colliders.find(id) < 0) {
            r_colliders.push_back(id);
        }
    }
}

} // namespace

void monitor_contacts(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        Object *body = gd::object_of(held.node);
        if (body == nullptr) {
            continue;
        }
        if (int(body->get(max_contacts_name())) < CLAIM_CONTACTS_REPORTED) {
            body->set(max_contacts_name(), CLAIM_CONTACTS_REPORTED);
        }
    }
}

void touching(const Bodies &p_bodies, LocalVector<ObjectID> &r_colliders) {
    r_colliders.clear();
    for (const AuthoredBody &held : p_bodies.held) {
        const store::NodeCell &cell = resident_of(p_bodies, held);
        if (cell.solid != nullptr) {
            if (cell.solid->get_max_contacts_reported() > 0) {
                note_colliders(cell.state_3d, r_colliders);
            }
        } else if (cell.flat != nullptr) {
            if (cell.flat->get_max_contacts_reported() > 0) {
                note_colliders(
                    PhysicsServer2D::get_singleton()->body_get_direct_state(
                        cell.body
                    ),
                    r_colliders
                );
            }
        }
    }
}

bool thawed(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        const store::NodeCell &cell = resident_of(p_bodies, held);
        const bool frozen = cell.solid != nullptr
            ? cell.solid->is_freeze_enabled()
            : cell.flat != nullptr
            ? cell.flat->is_freeze_enabled()
            : cell.object != nullptr && bool(cell.object->get(freeze_name()));
        if (frozen) {
            return false;
        }
    }
    return true;
}

bool moving(const Bodies &p_bodies) {
    static const StringName linear("linear_velocity");
    static const StringName angular("angular_velocity");
    static const StringName sleeping("sleeping");
    for (const AuthoredBody &held : p_bodies.held) {
        const store::NodeCell &cell = resident_of(p_bodies, held);
        Vector3 along;
        Vector3 around;
        if (RigidBody3D *solid = cell.solid) {
            if (solid->is_sleeping()) {
                continue;
            }
            along = solid->get_linear_velocity();
            around = solid->get_angular_velocity();
        } else {
            Object *body = cell.object;
            if (body == nullptr || bool(body->get(sleeping))) {
                continue;
            }
            along = body->get(linear);
            around = body->get(angular);
        }
        if (along.length() > CLAIM_MOVING_SPEED
            || around.length() > CLAIM_MOVING_SPEED) {
            return true;
        }
    }
    return false;
}

bool rested(
    Rest &r_rest,
    bool p_resting,
    int64_t p_tick,
    int64_t p_ticks_needed
) {
    if (!p_resting) {
        r_rest.asleep_since = -1;
        return false;
    }
    if (r_rest.asleep_since < 0) {
        r_rest.asleep_since = p_tick;
    }
    return p_tick - r_rest.asleep_since >= p_ticks_needed;
}

void note_touch(Contact &r_contact, uint64_t p_other) {
    if (r_contact.touched_now.find(p_other) < 0) {
        r_contact.touched_now.push_back(p_other);
    }
}

void keep_touching(Contact &r_contact) {
    for (const uint64_t other : r_contact.touching) {
        note_touch(r_contact, other);
    }
}

void settle_onsets(Contact &r_contact, int64_t p_tick) {
    for (const uint64_t other : r_contact.touched_now) {
        if (r_contact.touching.find(other) < 0 && r_contact.fence_met) {
            r_contact.fence_tick = MAX(r_contact.fence_tick, p_tick);
            r_contact.fence_met = false;
        }
    }
    r_contact.touching = r_contact.touched_now;
    r_contact.touched_now.clear();
}

bool fenced(Contact &r_contact, int64_t p_tick) {
    if (p_tick < 0) {
        return false;
    }
    if (p_tick < r_contact.fence_tick) {
        return true;
    }
    r_contact.fence_met = true;
    return false;
}

} // namespace netw::sim
