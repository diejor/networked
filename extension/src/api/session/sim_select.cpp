#include "netw/api/netw_multiplayer.hpp"

#include <algorithm>

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/spatial_node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/simulation_handle.hpp"
#include "netw/log.hpp"
#include "netw/sim/row.hpp"
#include "netw/sim/select.hpp"
#include "netw/subsystems.hpp"
#include "netw/sync_authoring.hpp"

using namespace godot;

namespace netw {

namespace {

String order_name(const Ref<NetwEntity> &p_entity) {
    const String named = String(p_entity->get_entity_id());
    if (!named.is_empty()) {
        return named;
    }
    const int64_t route = p_entity->get_route();
    if (route > 0) {
        return String("route:") + String::num_int64(route);
    }
    Node *root = p_entity->get_owner();
    if (root != nullptr) {
        return String("path:") + String(root->get_path());
    }
    return String();
}

bool order_less(const Ref<NetwEntity> &p_a, const Ref<NetwEntity> &p_b) {
    const String a_id = order_name(p_a);
    const String b_id = order_name(p_b);
    if (a_id == b_id) {
        return p_a->get_instance_id() < p_b->get_instance_id();
    }
    return a_id < b_id;
}

Vector3 position_of(const Ref<NetwEntity> &p_entity) {
    Node *root = p_entity->get_owner();
    if (Node3D *spatial = Object::cast_to<Node3D>(root)) {
        return spatial->get_global_position();
    }
    if (Node2D *flat = Object::cast_to<Node2D>(root)) {
        const Vector2 point = flat->get_global_position();
        return Vector3(point.x, point.y, 0.0);
    }
    return Vector3();
}

bool same_scene(
    const NetwMultiplayer &p_core,
    const Ref<NetwEntity> &p_subject,
    const Ref<NetwEntity> &p_member
) {
    return p_core.scene_of(p_subject->get_rid_handle())
        == p_core.scene_of(p_member->get_rid_handle());
}

bool member_of(
    const NetwMultiplayer &p_core,
    const Ref<NetwEntity> &p_subject,
    const Ref<NetwEntity> &p_member
) {
    if (p_member.is_null() || p_member == p_subject
        || p_member->get_owner() == nullptr || p_member->get_declares_scene()) {
        return false;
    }
    return same_scene(p_core, p_subject, p_member)
        && p_member->get_multiplayer() == p_subject->get_multiplayer();
}

void take(
    const NetwMultiplayer &p_core,
    const Ref<NetwEntity> &p_subject,
    const Ref<NetwEntity> &p_member,
    LocalVector<Ref<NetwEntity>> &r_found
) {
    if (!member_of(p_core, p_subject, p_member)) {
        return;
    }
    for (const Ref<NetwEntity> &held : r_found) {
        if (held == p_member) {
            return;
        }
    }
    r_found.push_back(p_member);
}

} // namespace

void NetwMultiplayer::sim_release_row(const RID &p_entity) {
    sim_refresh_members(sim_rows.release(p_entity));
}

Error NetwMultiplayer::sim_simulate(
    const RID &p_subject,
    const Ref<NetwEntity> &p_entity
) {
    const Ref<NetwEntity> subject = entity_get_view(p_subject);
    NETW_ERR_COND_V(
        subject.is_null() || p_entity.is_null(),
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "simulate: the subject and the entity must both be live."
    );
    NETW_ERR_COND_V(
        !same_scene(*this, subject, p_entity),
        ERR_INVALID_PARAMETER,
        sys::PREDICTION,
        "simulate: %s is in another scene than %s, and a selection cannot "
        "cross scenes.",
        String(p_entity->get_entity_id()).utf8().get_data(),
        String(subject->get_entity_id()).utf8().get_data()
    );
    sim::choose(
        sim_rows.ensure(p_subject).choice,
        p_entity->get_rid_handle(),
        sim::Pick::CHOSEN
    );
    return OK;
}

void NetwMultiplayer::sim_forget(
    const RID &p_subject,
    const Ref<NetwEntity> &p_entity
) {
    sim::Row *row = sim_rows.find(p_subject);
    if (row != nullptr && p_entity.is_valid()) {
        sim::forget(row->choice, p_entity->get_rid_handle());
    }
}

void NetwMultiplayer::sim_simulate_nearest(
    const RID &p_subject,
    int p_count,
    const StringName &p_layer
) {
    sim::simulate_nearest(sim_rows.ensure(p_subject).choice, p_count, p_layer);
}

void NetwMultiplayer::sim_simulate_within(
    const RID &p_subject,
    double p_meters,
    const StringName &p_layer
) {
    sim::simulate_within(sim_rows.ensure(p_subject).choice, p_meters, p_layer);
}

void NetwMultiplayer::sim_simulate_all(
    const RID &p_subject,
    const StringName &p_layer
) {
    sim::simulate_all(sim_rows.ensure(p_subject).choice, p_layer);
}

void NetwMultiplayer::sim_simulate_none(const RID &p_subject) {
    sim::Row *row = sim_rows.find(p_subject);
    if (row != nullptr) {
        sim::simulate_none(row->choice);
    }
}

TypedArray<NetwEntity> NetwMultiplayer::sim_selected(const RID &p_subject) {
    TypedArray<NetwEntity> out;
    const sim::Row *row = sim_rows.row_of(p_subject);
    if (row == nullptr) {
        return out;
    }
    for (const sim::Selected &member : row->selection.members) {
        if (!member.promoted) {
            continue;
        }
        const Ref<NetwEntity> entity
            = entity_get_view(sim_rows.entity_of_key(member.key));
        if (entity.is_valid()) {
            out.push_back(entity);
        }
    }
    return out;
}

uint32_t NetwMultiplayer::sim_selection_count(const RID &p_member) const {
    return sim_rows.selection_count(p_member);
}

RID NetwMultiplayer::sim_entity_of_key(int64_t p_key) const {
    return sim_rows.entity_of_key(p_key);
}

bool NetwMultiplayer::sim_chooses(const RID &p_subject) const {
    const sim::Row *row = sim_rows.row_of(p_subject);
    return row != nullptr && row->choice.declared();
}

TypedArray<NetwEntity> NetwMultiplayer::sim_named(const RID &p_subject) {
    TypedArray<NetwEntity> out;
    const sim::Row *row = sim_rows.row_of(p_subject);
    if (row == nullptr) {
        return out;
    }
    for (const sim::Named &named : row->choice.named) {
        const Ref<NetwEntity> entity = entity_get_view(named.entity);
        if (entity.is_valid()) {
            out.push_back(entity);
        }
    }
    return out;
}

TypedArray<NetwEntity> NetwMultiplayer::sim_candidates(
    const Ref<NetwEntity> &p_subject
) {
    TypedArray<NetwEntity> out;
    if (p_subject.is_null()) {
        return out;
    }
    const sim::Row *row = sim_rows.row_of(p_subject->get_rid_handle());
    if (row == nullptr) {
        return out;
    }
    LocalVector<Ref<NetwEntity>> found;
    for (const sim::Named &named : row->choice.named) {
        take(*this, p_subject, entity_get_view(named.entity), found);
    }
    for (const StringName &layer : row->choice.layers) {
        const TypedArray<Object> shared
            = interest_shared_entities(p_subject, layer);
        for (int at = 0; at < shared.size(); ++at) {
            take(
                *this,
                p_subject,
                Ref<NetwEntity>(Object::cast_to<NetwEntity>(shared[at])),
                found
            );
        }
    }
    std::sort(found.ptr(), found.ptr() + found.size(), order_less);
    for (const Ref<NetwEntity> &member : found) {
        out.push_back(member);
    }
    return out;
}

bool NetwMultiplayer::sim_selectable(const Ref<NetwEntity> &p_member) {
    const RID member = p_member->get_rid_handle();
    if (!member.is_valid()) {
        return false;
    }
    const sim::Row *row = sim_rows.row_of(member);
    if (predict_engine_for(member) != nullptr) {
        return row != nullptr && row->facts.predicted && !sim::leads(row->mode);
    }
    Node *owner = p_member->get_owner();
    if (owner == nullptr || authoring::declares_prediction(owner)) {
        return false;
    }
    const sim::Mode mode = row != nullptr
        ? row->mode
        : sim_rows.resolve(member, sim_body_facts(p_member));
    return !sim::leads(mode);
}

TypedArray<NetwEntity> NetwMultiplayer::sim_select(
    const Ref<NetwEntity> &p_subject,
    const TypedArray<NetwEntity> &p_candidates,
    const TypedArray<NetwEntity> &p_contact,
    int64_t p_frontier
) {
    TypedArray<NetwEntity> promoted;
    if (p_subject.is_null()) {
        return promoted;
    }
    const RID subject = p_subject->get_rid_handle();
    const sim::Row *standing = sim_rows.row_of(subject);
    if (standing == nullptr || !sim::leads(standing->mode)) {
        sim_drop_selections(subject);
        return promoted;
    }

    LocalVector<Ref<NetwEntity>> ranked;
    ranked.reserve(uint32_t(p_candidates.size()) + 1);
    for (int at = 0; at < p_candidates.size(); ++at) {
        const Ref<NetwEntity> member = p_candidates[at];
        if (member.is_valid() && member != p_subject) {
            ranked.push_back(member);
        }
    }
    ranked.push_back(p_subject);
    std::sort(ranked.ptr(), ranked.ptr() + ranked.size(), order_less);

    const Vector3 here = position_of(p_subject);
    HashMap<int64_t, Ref<NetwEntity>> by_key;
    LocalVector<sim::Candidate> candidates;
    int64_t subject_order = 0;
    for (uint32_t at = 0; at < ranked.size(); ++at) {
        const Ref<NetwEntity> &member = ranked[at];
        if (member == p_subject) {
            subject_order = int64_t(at);
            continue;
        }
        const RID entity = member->get_rid_handle();
        const int64_t key = int64_t(entity.get_id());
        if (!entity.is_valid() || by_key.has(key)) {
            continue;
        }
        sim::Candidate candidate;
        candidate.key = key;
        candidate.order_key = int64_t(at);
        candidate.distance_squared
            = here.distance_squared_to(position_of(member));
        const sim::Named *named = standing->choice.named_of(entity);
        candidate.pick = named != nullptr ? named->pick : sim::Pick::AUTOMATIC;
        candidate.eligible = sim_selectable(member);
        candidate.contact = p_contact.has(member);
        candidates.push_back(candidate);
        by_key.insert(key, member);
    }

    sim::Row &row = sim_rows.ensure(subject);
    row.selection.owner_order_key = subject_order;
    row.selection.policy = row.choice.policy;
    row.selection.count = row.choice.count;
    row.selection.meters = row.choice.meters;
    row.selection.commit(candidates, p_frontier);

    LocalVector<int64_t> promoted_keys;
    LocalVector<int64_t> demoted_keys;
    for (const sim::Selected &member : row.selection.members) {
        if (member.promoted) {
            promoted_keys.push_back(member.key);
        } else {
            demoted_keys.push_back(member.key);
        }
    }
    LocalVector<RID> moved;
    for (const int64_t key : promoted_keys) {
        const HashMap<int64_t, Ref<NetwEntity>>::ConstIterator found
            = by_key.find(key);
        if (found == by_key.end()) {
            continue;
        }
        const RID member = found->value->get_rid_handle();
        if (sim_rows.note_selected(member, subject)) {
            moved.push_back(member);
        }
        promoted.push_back(found->value);
    }
    for (const int64_t key : demoted_keys) {
        const HashMap<int64_t, Ref<NetwEntity>>::ConstIterator found
            = by_key.find(key);
        const RID member = found != by_key.end()
            ? found->value->get_rid_handle()
            : sim_rows.entity_of_key(key);
        if (member.is_valid() && sim_rows.erase_selected(member, subject)) {
            moved.push_back(member);
        }
    }
    sim_refresh_members(moved);
    return promoted;
}

void NetwMultiplayer::sim_drop_selections(const RID &p_subject) {
    sim_refresh_members(sim_rows.drop_selections(p_subject));
}

void NetwMultiplayer::sim_note_selected_by(
    const RID &p_member,
    const RID &p_subject,
    bool p_selected
) {
    const bool moved = p_selected
        ? sim_rows.note_selected(p_member, p_subject)
        : sim_rows.erase_selected(p_member, p_subject);
    if (moved) {
        LocalVector<RID> members;
        members.push_back(p_member);
        sim_refresh_members(members);
    }
}

void NetwMultiplayer::sim_release_lingering(
    const RID &p_subject,
    int64_t p_floor
) {
    sim::Row *row = sim_rows.find(p_subject);
    if (row != nullptr) {
        row->selection.release_lingering(p_floor);
    }
}

void NetwMultiplayer::sim_select_unpredicted(int64_t p_tick, bool p_commit) {
    sim_rows.choosers(sim_stepping);
    for (const RID &subject : sim_stepping) {
        const Ref<NetwEntity> entity = entity_get_view(subject);
        if (entity.is_null()) {
            sim_release_row(subject);
            continue;
        }
        Node *owner = entity->get_owner();
        const sim::Row *row = sim_rows.row_of(subject);
        if (owner == nullptr || !owner->is_inside_tree() || row == nullptr
            || predict_engine_for(subject) != nullptr) {
            continue;
        }
        const sim::Mode mode = row->bodies.recorded
            ? row->mode
            : sim_rows.resolve(subject, sim_body_facts(entity));
        if (!sim::leads(mode)) {
            sim_drop_selections(subject);
        } else if (p_commit) {
            sim_select(
                entity,
                sim_candidates(entity),
                TypedArray<NetwEntity>(),
                p_tick
            );
        }
    }
}

void NetwMultiplayer::sim_seed(sim::Row &r_row) {
    const Ref<NetwEntity> entity = entity_get_view(r_row.entity);
    if (entity.is_null()) {
        return;
    }
    const Ref<NetwSimulationHandle> handle = entity->get_simulation();
    if (handle.is_null()) {
        return;
    }
    const sim::Declaration &held = handle->declaration();
    if (!r_row.bodies.recorded) {
        r_row.declaration.bodies = held.bodies;
    }
    r_row.declaration.schedule = held.schedule;
    r_row.declaration.replicas = held.replicas;
    r_row.declaration.restore = held.restore;
    r_row.declaration.max_restore_ticks = held.max_restore_ticks;
    r_row.declaration.step = held.step;
    r_row.facts.replicas = held.replicas;
    r_row.choice = handle->selection_choice();
}

void NetwMultiplayer::sim_declare(const Ref<NetwEntity> &p_entity) {
    if (p_entity.is_null()) {
        return;
    }
    const RID entity = p_entity->get_rid_handle();
    const Ref<NetwSimulationHandle> handle = p_entity->get_simulation();
    if (!entity.is_valid() || handle.is_null()
        || entity_get_view(entity).is_null()) {
        return;
    }
    sim::Row *standing = sim_rows.find(entity);
    if (standing == nullptr && !handle->is_declared()) {
        return;
    }
    sim_seed(standing != nullptr ? *standing : sim_rows.ensure(entity));
    LocalVector<RID> members;
    members.push_back(entity);
    sim_refresh_members(members);
}

void NetwMultiplayer::sim_announce(const RID &p_entity) {
    const sim::Row *row = sim_rows.row_of(p_entity);
    const Ref<NetwEntity> entity = entity_get_view(p_entity);
    if (row == nullptr || entity.is_null()) {
        return;
    }
    const Ref<NetwSimulationHandle> handle = entity->get_simulation();
    if (handle.is_valid()) {
        handle->announce(NetwSimulationHandle::Mode(int(row->mode)));
    }
}

void NetwMultiplayer::sim_refresh_members(const LocalVector<RID> &p_members) {
    for (const RID &member : p_members) {
        const sim::Row *row = sim_rows.row_of(member);
        if (row == nullptr) {
            continue;
        }
        if (NetwPredictSlotEngine *engine = predict_engine_for(member)) {
            if (row->selected_by.count() == 0) {
                prediction_engine.leave_joint(engine->native_slot());
            }
            if (sim::resolve(row->facts) != row->mode) {
                engine->rewire();
            }
            continue;
        }
        const Ref<NetwEntity> entity = entity_get_view(member);
        if (entity.is_null()) {
            continue;
        }
        sim_rows.resolve(member, sim_body_facts(entity));
        sim_settle_body(entity);
        display_mark_role_dirty(member);
        sim_announce(member);
    }
}

} // namespace netw
