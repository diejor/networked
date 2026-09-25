#include "netw/sim/contact.hpp"

#include "godot/node.hpp"
#include "godot/variant.hpp"

using namespace godot;

namespace netw::sim {

namespace {

const StringName &contact_monitor_name() {
    static const StringName name("contact_monitor");
    return name;
}

const StringName &max_contacts_name() {
    static const StringName name("max_contacts_reported");
    return name;
}

const StringName &colliding_name() {
    static const StringName name("get_colliding_bodies");
    return name;
}

const StringName &freeze_name() {
    static const StringName name("freeze");
    return name;
}

} // namespace

void monitor_contacts(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        Object *body = gd::object_of(held.node);
        if (body == nullptr) {
            continue;
        }
        if (!bool(body->get(contact_monitor_name()))) {
            body->set(contact_monitor_name(), true);
        }
        if (int(body->get(max_contacts_name())) < CLAIM_CONTACTS_REPORTED) {
            body->set(max_contacts_name(), CLAIM_CONTACTS_REPORTED);
        }
    }
}

void touching(const Bodies &p_bodies, LocalVector<ObjectID> &r_colliders) {
    r_colliders.clear();
    for (const AuthoredBody &held : p_bodies.held) {
        Object *body = gd::object_of(held.node);
        if (body == nullptr || !bool(body->get(contact_monitor_name()))) {
            continue;
        }
        const Array colliders = body->call(colliding_name());
        for (int at = 0; at < colliders.size(); ++at) {
            Object *other = colliders[at];
            if (other == nullptr) {
                continue;
            }
            const ObjectID id = gd::instance_id(other);
            if (r_colliders.find(id) < 0) {
                r_colliders.push_back(id);
            }
        }
    }
}

bool thawed(const Bodies &p_bodies) {
    for (const AuthoredBody &held : p_bodies.held) {
        Object *body = gd::object_of(held.node);
        if (body != nullptr && bool(body->get(freeze_name()))) {
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
        Object *body = gd::object_of(held.node);
        if (body == nullptr || bool(body->get(sleeping))) {
            continue;
        }
        const Vector3 along = body->get(linear);
        const Vector3 around = body->get(angular);
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
