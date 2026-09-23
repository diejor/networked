#include "netw/api/netw_multiplayer.hpp"

#include <cmath>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity.hpp"
#include "netw/log.hpp"
#include "netw/sim/contact.hpp"
#include "netw/sim/row.hpp"
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

} // namespace

void NetwMultiplayer::sim_contact_arm(
    sim::Row &r_row,
    const Ref<NetwEntity> &p_entity
) {
    const bool declares = r_row.declaration.claim_on_contact
        || r_row.declaration.release_on_rest > 0.0;
    r_row.contact.eligible = declares
        && p_entity->get_transfer() == NetwEntity::TRANSFER_IMMEDIATE
        && sim::controller_authored(r_row.facts);
    if (declares && !r_row.contact.eligible && !r_row.contact.warned) {
        r_row.contact.warned = true;
        NETW_WARN(
            sys::PREDICTION,
            "'%s' declares claim_on_contact or release_on_rest, which need a "
            "controller-authored entity under TRANSFER_IMMEDIATE, so neither "
            "acts on it",
            String(p_entity->get_entity_id())
        );
    }
    if (r_row.contact.eligible && r_row.declaration.claim_on_contact
        && !r_row.contact.monitored) {
        sim::monitor_contacts(r_row.bodies);
        r_row.contact.monitored = true;
    }
}

void NetwMultiplayer::sim_note_contacts(sim::Row &r_row) {
    const bool fences = r_row.mode == sim::Mode::ACTIVE;
    const bool leads = sim::leads(r_row.mode);
    if (!fences && !leads) {
        return;
    }
    LocalVector<ObjectID> colliders;
    sim::touching(r_row.bodies, colliders);
    for (const ObjectID &collider : colliders) {
        sim::Row *other = sim_rows.find(contact_owner(collider));
        if (other == nullptr || other == &r_row) {
            continue;
        }
        if (fences && sim::leads(other->mode)) {
            sim::note_touch(r_row.contact, other->entity.get_id());
        }
        if (leads && other->mode == sim::Mode::ACTIVE) {
            sim::note_touch(other->contact, r_row.entity.get_id());
        }
    }
}

void NetwMultiplayer::sim_rest_pass(
    sim::Row &r_row,
    const Ref<NetwEntity> &p_entity,
    int64_t p_tick
) {
    sim::Rest &rest = r_row.contact.rest;
    if (r_row.declaration.release_on_rest <= 0.0 || !r_row.contact.eligible) {
        rest = sim::Rest();
        return;
    }
    const bool asleep = sim::asleep(r_row.bodies);
    if (rest.releasing) {
        if (!p_entity->get_is_control_pending()
            || !p_entity->is_controller_here()) {
            rest = sim::Rest();
        } else if (!asleep && !p_entity->is_claim_pending()) {
            rest = sim::Rest();
            p_entity->request_control(entity::Control::HOLD_YIELDABLE);
        }
        return;
    }
    const bool resting = asleep && p_entity->is_controller_here()
        && !p_entity->get_is_control_pending()
        && p_entity->get_hold() == entity::Control::HOLD_YIELDABLE
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
    p_entity->release_control(0);
}

void NetwMultiplayer::sim_contact_pass(int64_t p_tick) {
    LocalVector<RID> bodied;
    sim_rows.bodied(bodied);
    LocalVector<RID> sources;
    for (const RID &entity : bodied) {
        sim::Row *row = sim_rows.find(entity);
        const Ref<NetwEntity> view = entity_get_view(entity);
        if (row == nullptr || view.is_null()) {
            continue;
        }
        sim_contact_arm(*row, view);
        sim_note_contacts(*row);
    }
    for (const RID &entity : bodied) {
        sim::Row *row = sim_rows.find(entity);
        const Ref<NetwEntity> view = entity_get_view(entity);
        if (row == nullptr || view.is_null()) {
            continue;
        }
        sim::settle_onsets(row->contact, p_tick);
        if (sim::leads(row->mode) && row->contact.eligible
            && row->declaration.claim_on_contact
            && view->get_is_controlled_locally()) {
            sources.push_back(entity);
        } else {
            row->contact.claim_touching.clear();
        }
        sim_rest_pass(*row, view, p_tick);
    }
    LocalVector<ObjectID> colliders;
    LocalVector<bool> onsets;
    for (uint32_t at = 0; at < sources.size(); ++at) {
        const RID source = sources[at];
        sim::Row *row = sim_rows.find(source);
        const Ref<NetwEntity> view = entity_get_view(source);
        if (row == nullptr || view.is_null()) {
            continue;
        }
        sim::touching(row->bodies, colliders);
        onsets.clear();
        for (const ObjectID &collider : colliders) {
            onsets.push_back(row->contact.claim_touching.find(collider) < 0);
        }
        row->contact.claim_touching = colliders;
        for (uint32_t which = 0; which < colliders.size(); ++which) {
            const RID target = contact_owner(colliders[which]);
            const sim::Row *other = sim_rows.row_of(target);
            const Ref<NetwEntity> touched = entity_get_view(target);
            if (other == nullptr || touched.is_null() || target == source
                || !other->contact.eligible
                || !other->declaration.claim_on_contact
                || (!onsets[which] && !sim::moving(other->bodies))
                || touched->get_controller() != 0
                || touched->get_is_control_pending()) {
                continue;
            }
            touched->claim_by_contact(view);
            if (touched->get_is_controlled_locally()
                && sources.find(target) < 0) {
                sources.push_back(target);
            }
        }
    }
}

bool NetwMultiplayer::sim_fenced(const RID &p_entity, int64_t p_tick) {
    sim::Row *row = sim_rows.find(p_entity);
    if (row == nullptr || row->mode != sim::Mode::ACTIVE
        || !sim::fenced(row->contact, p_tick)) {
        return false;
    }
    row->installs.stats.fenced += 1;
    return true;
}

} // namespace netw
