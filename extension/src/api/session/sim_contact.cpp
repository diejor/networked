#include "netw/api/netw_multiplayer.hpp"

#include <cmath>

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/physics_body.hpp"
#include "netw/api/entity.hpp"
#include "netw/log.hpp"
#include "netw/sim/contact.hpp"
#include "netw/sim/install.hpp"
#include "netw/sim/row.hpp"
#include "netw/store/validate.hpp"
#include "netw/subsystems.hpp"

using namespace godot;

namespace netw {

namespace {

RID contact_owner(const ObjectID &p_collider) {
    Node *node = Object::cast_to<Node>(gd::object_of(p_collider));
    const Ref<NetwEntity> entity
        = node != nullptr ? NetwEntity::of(node) : Ref<NetwEntity>();
    return entity.is_valid() ? entity->get_rid_handle() : RID();
}

const StringName &sleeping_changed_name() {
    static const StringName name("sleeping_state_changed");
    return name;
}

bool sleeps(Object *p_body) {
    if (const RigidBody3D *solid = Object::cast_to<RigidBody3D>(p_body)) {
        return solid->is_sleeping();
    }
    if (const RigidBody2D *flat = Object::cast_to<RigidBody2D>(p_body)) {
        return flat->is_sleeping();
    }
    return false;
}

} // namespace

int64_t NetwMultiplayer::sim_local_peer() {
#if defined(NETW_TESTS)
    store::peer_reads_seen() += 1;
#endif
    if (NETW_API_VIRTUAL(get_multiplayer_peer)().is_null()) {
        return 0;
    }
    return int64_t(NETW_API_VIRTUAL(get_unique_id)());
}

sim::Steering NetwMultiplayer::sim_steering_of(const NetwEntity &p_entity) {
    sim::Steering steering;
    steering.controller = p_entity.get_controller();
    steering.hold = int64_t(p_entity.get_hold());
    steering.pending = p_entity.get_is_control_pending();
    steering.claiming = p_entity.is_claim_pending();
    steering.immediate
        = p_entity.get_transfer() == NetwEntity::TRANSFER_IMMEDIATE;
    return steering;
}

void NetwMultiplayer::sim_follow_steering(const NetwEntity *p_entity) {
    if (p_entity == nullptr) {
        return;
    }
    sim::Row *row = sim_rows.peek(p_entity->get_rid_handle());
    if (row != nullptr) {
        row->steering = sim_steering_of(*p_entity);
    }
}

void NetwMultiplayer::sim_follow_view(const RID &p_entity, NetwEntity *p_view) {
    sim::Row *row = sim_rows.peek(p_entity);
    if (row == nullptr) {
        return;
    }
    row->view = p_view;
    if (p_view != nullptr) {
        row->steering = sim_steering_of(*p_view);
    }
}

RID NetwMultiplayer::sim_contact_owner(const ObjectID &p_collider) const {
    const RID *held = sim_bodies.getptr(uint64_t(p_collider));
    return held != nullptr ? *held : contact_owner(p_collider);
}

bool NetwMultiplayer::sim_contact_eligible(
    const sim::Row &p_row,
    const sim::Steering &p_steering
) const {
    const bool declares = p_row.declaration.claim_on_contact
        || p_row.declaration.release_on_rest > 0.0;
    return declares && p_steering.immediate
        && sim::controller_authored(p_row.facts);
}

void NetwMultiplayer::sim_contact_arm(sim::Row &r_row) {
    const bool declares = r_row.declaration.claim_on_contact
        || r_row.declaration.release_on_rest > 0.0;
    r_row.contact.eligible = sim_contact_eligible(r_row, r_row.steering);
    if (declares && !r_row.contact.eligible && !r_row.contact.warned) {
        r_row.contact.warned = true;
        NETW_WARN(
            sys::PREDICTION,
            "'%s' sets claim_on_contact or release_on_rest, which do nothing "
            "here. They need NetwEntity.TRANSFER_IMMEDIATE on an entity with "
            "no prediction and no state properties.",
            String(r_row.view->get_entity_id())
        );
    }
    if (r_row.contact.eligible && r_row.declaration.claim_on_contact
        && !r_row.contact.monitored) {
        sim::monitor_contacts(r_row.bodies);
        r_row.contact.monitored = true;
    }
}

void NetwMultiplayer::sim_note_touch(
    sim::Row &r_row,
    uint64_t p_other,
    int64_t p_tick
) {
    if (r_row.contact_tick != p_tick && r_row.contact.touched_now.is_empty()) {
        sim_noted.push_back(r_row.index);
    }
    sim::note_touch(r_row.contact, p_other);
}

void NetwMultiplayer::sim_note_contacts(sim::Row &r_row, int64_t p_tick) {
    const bool fences = r_row.mode == sim::Mode::ACTIVE;
    const bool leads = sim::leads(r_row.mode);
    if (!fences && !leads) {
        return;
    }
    if (sim::asleep(r_row.bodies)) {
        if (fences) {
            sim::keep_touching(r_row.contact);
        }
        return;
    }
    sim::touching(r_row.bodies, sim_colliders);
#if defined(NETW_TESTS)
    idle_work.touched += int64_t(sim_colliders.size());
#endif
    for (const ObjectID &collider : sim_colliders) {
        sim::Row *other = sim_rows.peek(sim_contact_owner(collider));
        if (other == nullptr || other == &r_row) {
            continue;
        }
        if (fences && sim::leads(other->mode)) {
            sim_note_touch(r_row, other->entity.get_id(), p_tick);
        }
        if (leads && other->mode == sim::Mode::ACTIVE) {
            sim_note_touch(*other, r_row.entity.get_id(), p_tick);
        }
    }
}

void NetwMultiplayer::sim_rest_pass(
    sim::Row &r_row,
    int64_t p_local_peer,
    int64_t p_tick
) {
    sim::Rest &rest = r_row.contact.rest;
    if (r_row.declaration.release_on_rest <= 0.0 || !r_row.contact.eligible) {
        rest = sim::Rest();
        return;
    }
    const sim::Steering &steering = r_row.steering;
    const bool here = sim::steered_here(steering, p_local_peer);
    const bool asleep = sim::asleep(r_row.bodies);
    if (rest.releasing) {
        if (!steering.pending || !here) {
            rest = sim::Rest();
        } else if (!asleep && !steering.claiming) {
            rest = sim::Rest();
            r_row.view->claim_authority(entity::Control::HOLD_YIELDABLE);
        }
        return;
    }
    const bool resting = asleep && here && !steering.pending
        && steering.hold == entity::Control::HOLD_YIELDABLE
        && sim::thawed(r_row.bodies);
    const double ticktime = clock_engine().ticktime();
    const int64_t needed = ticktime > 0.0
        ? int64_t(std::ceil(r_row.declaration.release_on_rest / ticktime))
        : 0;
    if (!sim::rested(rest, resting, p_tick, needed)) {
        return;
    }
    rest.asleep_since = -1;
    rest.releasing = true;
    r_row.view->release_authority(0);
}

void NetwMultiplayer::sim_settle_walk(int64_t p_local_peer, int64_t p_tick) {
    for (uint32_t at = 0; at < sim_walk.size(); ++at) {
        sim::Row *row = sim_rows.at(sim_walk[at]);
        if (row == nullptr || row->view == nullptr) {
            continue;
        }
        sim::settle_onsets(row->contact, p_tick);
        if (sim::leads(row->mode) && row->contact.eligible
            && row->declaration.claim_on_contact
            && sim::steered_locally(row->steering, p_local_peer)) {
            sim_sources.push_back(row->entity);
        } else {
            row->contact.claim_touching.clear();
        }
        sim_rest_pass(*row, p_local_peer, p_tick);
        const sim::Rest &rest = row->contact.rest;
        const bool watched = row->contact.eligible
            && row->declaration.release_on_rest > 0.0
            && (rest.releasing || rest.asleep_since >= 0
                || sim::steered_here(row->steering, p_local_peer));
        sim_rows.set_resting(*row, watched);
    }
    for (uint32_t at = 0; at < sim_noted.size(); ++at) {
        sim::Row *row = sim_rows.at(sim_noted[at]);
        if (row == nullptr || row->contact_tick == p_tick
            || row->view == nullptr) {
            continue;
        }
        if (row->mode == sim::Mode::ACTIVE) {
            sim::keep_touching(row->contact);
        }
        sim::settle_onsets(row->contact, p_tick);
    }
}

void NetwMultiplayer::sim_contact_pass(int64_t p_tick) {
    NETW_ZONE_NC("Contact pass", colors::PREDICTION);
    {
        NETW_ZONE_NC("Contact wake", colors::PREDICTION);
        sim_rouse_drowsy();
        sim_rows.settle();
        const int64_t every = store_world.validate_every;
        if (every > 0 && p_tick >= 0 && p_tick % every == 0) {
            sim_contact_validate(p_tick);
        }
    }
#if defined(NETW_TESTS)
    const int64_t allocated = store::allocations_seen();
    const int64_t peer_read = store::peer_reads_seen();
#endif
    const int64_t local = sim_local_peer();
    {
        NETW_ZONE_NC("Contact walk", colors::PREDICTION);
        sim_rows.contact_walk(sim_walk);
        NETW_ZONE_VALUE(sim_walk.size());
        sim_noted.clear();
        sim_sources.clear();
        for (uint32_t at = 0; at < sim_walk.size(); ++at) {
            sim::Row *row = sim_rows.at(sim_walk[at]);
            if (row->view == nullptr) {
                sim_rows.find(row->entity);
                continue;
            }
            row->contact_tick = p_tick;
        }
    }
#if defined(NETW_TESTS)
    idle_work.contacts += 1;
#endif
    {
        NETW_ZONE_NC("Contact notes", colors::PREDICTION);
        for (uint32_t at = 0; at < sim_walk.size(); ++at) {
            sim::Row *row = sim_rows.at(sim_walk[at]);
            if (row == nullptr || row->view == nullptr) {
                continue;
            }
#if defined(NETW_TESTS)
            idle_work.contacted += 1;
#endif
            NETW_ZONE_LEAF_F(
                "Contact entity",
                colors::PREDICTION,
                profile::SUBSYSTEM_ENTITY,
                "entity %lld",
                liveness_core.is_valid()
                    ? static_cast<long long>(
                          liveness_core->route_of(row->entity)
                      )
                    : 0LL
            );
            sim_contact_arm(*row);
            sim_note_contacts(*row, p_tick);
        }
    }
    {
        NETW_ZONE_NC("Contact settle", colors::PREDICTION);
        sim_settle_walk(local, p_tick);
    }
    {
        NETW_ZONE_NC("Contact claims", colors::PREDICTION);
        NETW_ZONE_VALUE(sim_sources.size());
        for (uint32_t at = 0; at < sim_sources.size(); ++at) {
            const RID source = sim_sources[at];
            sim::Row *row = sim_rows.peek(source);
            if (row == nullptr || row->view == nullptr) {
                continue;
            }
            if (sim::asleep(row->bodies)) {
                continue;
            }
            sim::touching(row->bodies, sim_colliders);
            sim_onsets.clear();
            for (const ObjectID &collider : sim_colliders) {
                sim_onsets.push_back(
                    row->contact.claim_touching.find(collider) < 0
                );
            }
            row->contact.claim_touching = sim_colliders;
            for (uint32_t which = 0; which < sim_colliders.size();
                 ++which) {
                const RID target = sim_contact_owner(sim_colliders[which]);
                const sim::Row *other = sim_rows.row_of(target);
                if (other == nullptr || other->view == nullptr
                    || target == source || !other->contact.eligible
                    || !other->declaration.claim_on_contact
                    || (!sim_onsets[which] && !sim::moving(other->bodies))
                    || other->steering.controller != 0
                    || other->steering.pending) {
                    continue;
                }
                const Ref<NetwEntity> touched(other->view);
                touched->claim_by_contact(Ref<NetwEntity>(row->view));
                if (sim::steered_locally(other->steering, local)
                    && sim_sources.find(target) < 0) {
                    sim_sources.push_back(target);
                }
            }
        }
    }
#if defined(NETW_TESTS)
    idle_work.contact_allocations += store::allocations_seen() - allocated;
    idle_work.contact_peer_reads += store::peer_reads_seen() - peer_read;
#endif
}

void NetwMultiplayer::sim_contact_fault(
    const sim::Row &p_row,
    const char *p_bit,
    int64_t p_tick
) {
    store::Miss missed;
    missed.route = liveness_core.is_valid()
        ? int64_t(liveness_core->route_of(p_row.entity))
        : 0;
    missed.slot = p_row.index;
    missed.tick = p_tick;
    missed.column = StringName(p_bit);
    missed.gate = "contact";
    store::report_miss(missed);
}

void NetwMultiplayer::sim_contact_validate(int64_t p_tick) {
    sim_rows.each(sim_scan);
    sim_still_waking.clear();
    sim_still_since.clear();
    for (sim::Row *row : sim_scan) {
        const uint32_t at = row->index;
#if defined(NETW_TESTS)
        store::validation_audit::ledger().checks += 1;
#endif
        const bool bodied = !row->bodies.held.is_empty();
        if (sim_rows.is_bodied(at) != bodied) {
            sim_contact_fault(*row, "bodied", p_tick);
        }
        if (sim_rows.is_stepping(at) != sim::steps(*row)) {
            sim_contact_fault(*row, "stepping", p_tick);
        }
        if (sim_rows.is_chooser(at) != sim::chooses(*row)) {
            sim_contact_fault(*row, "choosers", p_tick);
        }
        const Ref<NetwEntity> view = entity_get_view(row->entity);
        if (row->view != view.ptr()) {
            sim_contact_fault(*row, "view", p_tick);
        }
        if (view.is_valid()
            && !(row->steering == sim_steering_of(*view.ptr()))) {
            sim_contact_fault(*row, "steering", p_tick);
        }
        for (const sim::AuthoredBody &held : row->bodies.held) {
            const RID *owner = sim_bodies.getptr(uint64_t(held.node));
            if (owner != nullptr && *owner != contact_owner(held.node)) {
                sim_contact_fault(*row, "owner", p_tick);
            }
        }
        if (!bodied || sim_rows.is_due(at) || view.is_null()) {
            continue;
        }
        const sim::Steering steering = sim_steering_of(*view.ptr());
        if (sim_contact_eligible(*row, steering) != row->contact.eligible) {
            sim_contact_fault(*row, "eligible", p_tick);
        }
        if (!sim_rows.is_awake(at) && sim::contact_moded(*row)
            && !sim::asleep(row->bodies)) {
            const uint64_t id = row->entity.get_id();
            const int64_t found = sim_waking.find(id);
            const int64_t since
                = found >= 0 ? sim_waking_since[uint32_t(found)] : p_tick;
            bool swept = false;
            for (const sim::AuthoredBody &held : row->bodies.held) {
                swept = swept || store_world.dozing.has(uint64_t(held.node));
            }
            const int64_t lag
                = swept ? int64_t(store::World::SWEEP_TICKS) + 1 : 0;
            if (found >= 0 && p_tick - since > lag) {
                sim_contact_fault(*row, "awake", p_tick);
            }
            sim_still_waking.push_back(id);
            sim_still_since.push_back(since);
        }
        if (sim_rows.is_resting(at)) {
            continue;
        }
        const sim::Rest &rest = row->contact.rest;
        const bool rests = row->contact.eligible
            && row->declaration.release_on_rest > 0.0
            && view->is_controller_here();
        if (rests || rest.releasing || rest.asleep_since >= 0) {
            sim_contact_fault(*row, "resting", p_tick);
        }
    }
    sim_waking.clear();
    sim_waking_since.clear();
    for (uint32_t at = 0; at < sim_still_waking.size(); ++at) {
        sim_waking.push_back(sim_still_waking[at]);
        sim_waking_since.push_back(sim_still_since[at]);
    }
}

void NetwMultiplayer::sim_mark(const RID &p_entity) {
    sim_rows.find(p_entity);
}

void NetwMultiplayer::sim_on_body_sleep(const RID &p_entity) {
    sim::Row *row = sim_rows.peek(p_entity);
    if (row == nullptr) {
        return;
    }
    sim_follow_sleep(*row);
    if (!sim::asleep(row->bodies) && liveness_core.is_valid()) {
        store_work_route(int64_t(liveness_core->route_of(p_entity)));
    }
}

void NetwMultiplayer::sim_follow_sleep(const sim::Row &p_row) {
    const bool watched = p_row.sleep_hook.is_valid();
    const bool awake = watched && !sim::asleep(p_row.bodies);
    sim_rows.set_awake(p_row, awake);
    if (awake) {
        display_wake_entity(p_row.entity);
    }
    for (const sim::AuthoredBody &held : p_row.bodies.held) {
        const uint64_t body = uint64_t(held.node);
        if (!watched || awake) {
            store_world.dozing.erase(body);
            sim_drowsy.erase(body);
        } else if (store_world.probes(body)) {
            store_world.dozing.insert(body);
            sim_drowsy.erase(body);
        } else {
            store_world.dozing.erase(body);
            sim_drowsy.insert(body, p_row.entity);
        }
    }
}

void NetwMultiplayer::sim_rouse_drowsy() {
    if (sim_drowsy.is_empty()) {
        return;
    }
    sim_rousing.clear();
#if defined(NETW_TESTS)
    idle_work.drowsy += int64_t(sim_drowsy.size());
#endif
    for (const KeyValue<uint64_t, RID> &held : sim_drowsy) {
        if (!sleeps(gd::object_of(ObjectID(held.key)))) {
            sim_rousing.push_back(held.key);
        }
    }
    for (uint32_t at = 0; at < sim_rousing.size(); ++at) {
        const uint64_t body = sim_rousing[at];
        const RID *entity = sim_drowsy.getptr(body);
        if (entity == nullptr) {
            continue;
        }
        const sim::Row *row = sim_rows.peek(*entity);
        sim_drowsy.erase(body);
        if (row != nullptr) {
            sim_follow_sleep(*row);
        }
    }
}

void NetwMultiplayer::sim_watch_sleep(sim::Row &r_row, bool p_watch) {
    if (p_watch && !r_row.sleep_hook.is_valid()) {
        r_row.sleep_hook
            = callable_mp(this, &NetwMultiplayer::sim_on_body_sleep)
                  .bind(r_row.entity);
    }
    for (const sim::AuthoredBody &held : r_row.bodies.held) {
        const uint64_t id = uint64_t(held.node);
        const RID *indexed = sim_bodies.getptr(id);
        if (!p_watch && indexed != nullptr && *indexed == r_row.entity) {
            sim_bodies.erase(id);
        } else if (p_watch && contact_owner(held.node) == r_row.entity) {
            sim_bodies.insert(id, r_row.entity);
        }
        Object *body = gd::object_of(held.node);
        if (body == nullptr || !r_row.sleep_hook.is_valid()) {
            continue;
        }
        const bool connected
            = body->is_connected(sleeping_changed_name(), r_row.sleep_hook);
        if (p_watch && !connected) {
            body->connect(sleeping_changed_name(), r_row.sleep_hook);
        } else if (!p_watch && connected) {
            body->disconnect(sleeping_changed_name(), r_row.sleep_hook);
        }
        if (p_watch) {
            store_world.watched.insert(uint64_t(held.node));
        } else {
            store_world.watched.erase(uint64_t(held.node));
        }
    }
    if (!p_watch) {
        r_row.sleep_hook = Callable();
    }
    sim_follow_sleep(r_row);
}

void NetwMultiplayer::sim_note_push(Object *p_node) {
    Node *node = Object::cast_to<Node>(p_node);
    const Ref<NetwEntity> entity
        = node != nullptr ? NetwEntity::of(node) : Ref<NetwEntity>();
    if (entity.is_null()) {
        return;
    }
    sim::Row *row = sim_rows.peek(entity->get_rid_handle());
    if (row != nullptr && !row->bodies.held.is_empty()) {
        sim_follow_sleep(*row);
    }
}

bool NetwMultiplayer::sim_fenced(const RID &p_entity, int64_t p_tick) {
    sim::Row *row = sim_rows.peek(p_entity);
    if (row == nullptr || row->mode != sim::Mode::ACTIVE
        || !sim::fenced(row->contact, p_tick)) {
        return false;
    }
    row->installs.stats.fenced += 1;
    return true;
}

} // namespace netw
