#include "netw/sim/row.hpp"

namespace netw::sim {

using godot::KeyValue;
using godot::LocalVector;
using godot::RID;

void Rows::seed_with(Seed p_seed, void *p_context) {
    seed = p_seed;
    seed_context = p_context;
}

Row &Rows::ensure(const RID &p_entity) {
    Row *held = rows.getptr(p_entity.get_id());
    if (held != nullptr) {
        return *held;
    }
    Row fresh;
    fresh.entity = p_entity;
    Row &made = rows.insert(p_entity.get_id(), fresh)->value;
    if (seed != nullptr) {
        seed(seed_context, made);
    }
    return made;
}

Row *Rows::find(const RID &p_entity) {
    return rows.getptr(p_entity.get_id());
}

const Row *Rows::row_of(const RID &p_entity) const {
    return rows.getptr(p_entity.get_id());
}

Mode Rows::resolve(const RID &p_entity, const Facts &p_facts) {
    Row &row = ensure(p_entity);
    row.facts = p_facts;
    row.facts.replicas = row.declaration.replicas;
    row.facts.selection_count = row.selected_by.count();
    row.mode = sim::resolve(row.facts);
    return row.mode;
}

LocalVector<RID> Rows::release(const RID &p_entity) {
    LocalVector<RID> moved = drop_selections(p_entity);
    rows.erase(p_entity.get_id());
    return moved;
}

bool Rows::note_selected(const RID &p_member, const RID &p_subject) {
    ensure(p_subject).selecting.note(p_member.get_id());
    Row &row = ensure(p_member);
    if (!row.selected_by.note(p_subject.get_id())) {
        return false;
    }
    row.facts.selection_count = row.selected_by.count();
    return true;
}

bool Rows::erase_selected(const RID &p_member, const RID &p_subject) {
    if (Row *subject = find(p_subject)) {
        subject->selecting.erase(p_member.get_id());
    }
    Row *row = find(p_member);
    if (row == nullptr || !row->selected_by.erase(p_subject.get_id())) {
        return false;
    }
    row->facts.selection_count = row->selected_by.count();
    return true;
}

uint32_t Rows::selection_count(const RID &p_member) const {
    const Row *row = row_of(p_member);
    return row == nullptr ? 0 : row->selected_by.count();
}

LocalVector<RID> Rows::drop_selections(const RID &p_subject) {
    LocalVector<RID> moved;
    Row *subject = find(p_subject);
    if (subject == nullptr) {
        return moved;
    }
    const uint64_t id = p_subject.get_id();
    for (const uint64_t member : subject->selecting.ids) {
        Row *row = rows.getptr(member);
        if (row != nullptr && row->selected_by.erase(id)) {
            row->facts.selection_count = row->selected_by.count();
            moved.push_back(row->entity);
        }
    }
    subject->selecting.ids.clear();
    subject->selection.members.clear();
    return moved;
}

RID Rows::entity_of_key(int64_t p_key) const {
    const Row *row = rows.getptr(uint64_t(p_key));
    return row == nullptr ? RID() : row->entity;
}

void Rows::hold_poses(
    const RID &p_space,
    const LocalVector<RID> &p_members,
    LocalVector<HeldPose> &r_poses
) const {
    for (const KeyValue<uint64_t, Row> &held : rows) {
        const Row &row = held.value;
        if (row.bodies.held.is_empty() || p_members.has(row.entity)) {
            continue;
        }
        sim::hold_poses(row.bodies, p_space, r_poses);
    }
}

void Rows::choosers(LocalVector<RID> &r_out) const {
    r_out.clear();
    for (const KeyValue<uint64_t, Row> &held : rows) {
        const Row &row = held.value;
        if (!row.facts.predicted
            && (row.choice.declared() || row.selecting.count() > 0)) {
            r_out.push_back(held.value.entity);
        }
    }
}

LocalVector<RID> Rows::follow_session_authority(bool p_here) {
    LocalVector<RID> moved;
    for (KeyValue<uint64_t, Row> &held : rows) {
        Row &row = held.value;
        if (row.facts.session_authority_here == p_here) {
            continue;
        }
        row.facts.session_authority_here = p_here;
        if (sim::resolve(row.facts) != row.mode) {
            moved.push_back(row.entity);
        }
    }
    return moved;
}

LocalVector<RID> Rows::holding() const {
    LocalVector<RID> out;
    for (const KeyValue<uint64_t, Row> &held : rows) {
        if (!held.value.installs.held.is_empty()) {
            out.push_back(held.value.entity);
        }
    }
    return out;
}

void Rows::unpredicted(LocalVector<RID> &r_out) const {
    r_out.clear();
    for (const KeyValue<uint64_t, Row> &held : rows) {
        if (!held.value.facts.predicted) {
            r_out.push_back(held.value.entity);
        }
    }
}

Schedule resolved_schedule(Schedule p_declared, bool p_has_body) {
    switch (p_declared) {
        case Schedule::AUTO:
            return p_has_body ? Schedule::FRAME : Schedule::TICK;
        case Schedule::STEPPED:
            return Schedule::FRAME;
        default:
            return p_declared;
    }
}

} // namespace netw::sim
