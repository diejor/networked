#include "netw/sim/row.hpp"

#include <algorithm>

namespace netw::sim {

using godot::KeyValue;
using godot::LocalVector;
using godot::RID;

bool steered_here(const Steering &p_steering, int64_t p_local_peer) {
    return p_local_peer != 0 && p_steering.controller == p_local_peer;
}

bool steered_locally(const Steering &p_steering, int64_t p_local_peer) {
    return steered_here(p_steering, p_local_peer)
        || (p_steering.immediate && p_steering.claiming);
}

bool steps(const Row &p_row) {
    const bool declared
        = p_row.declaration.step.is_valid() || p_row.owner_ticks;
    return !p_row.facts.predicted && (declared || !p_row.bodies.recorded);
}

bool chooses(const Row &p_row) {
    return !p_row.facts.predicted
        && (p_row.choice.declared() || p_row.selecting.count() > 0);
}

bool contact_moded(const Row &p_row) {
    return p_row.mode == Mode::ACTIVE || leads(p_row.mode);
}

void Rows::seed_with(Seed p_seed, void *p_context) {
    seed = p_seed;
    seed_context = p_context;
}

void Rows::mark(Row &r_row) {
    if (stale.get(r_row.index)) {
        return;
    }
    stale.set(r_row.index, true);
    stale_list.push_back(r_row.index);
}

void Rows::derive(Row &r_row) {
    const uint32_t at = r_row.index;
    bodied_bits.set(at, !r_row.bodies.held.is_empty());
    stepping_bits.set(at, steps(r_row));
    chooser_bits.set(at, chooses(r_row));
}

Row &Rows::ensure(const RID &p_entity) {
    Row *held = rows.getptr(p_entity.get_id());
    if (held != nullptr) {
        mark(*held);
        return *held;
    }
    uint32_t index = 0;
    if (!free_index.is_empty()) {
        index = free_index[free_index.size() - 1];
        free_index.resize(free_index.size() - 1);
    } else {
        index = by_index.size();
        by_index.push_back(nullptr);
        const uint32_t count = by_index.size();
        stale.resize(count);
        due.resize(count);
        bodied_bits.resize(count);
        stepping_bits.resize(count);
        chooser_bits.resize(count);
        awake_bits.resize(count);
        resting_bits.resize(count);
    }
    Row fresh;
    fresh.entity = p_entity;
    fresh.index = index;
    fresh.order = next_order++;
    Row &made = rows.insert(p_entity.get_id(), fresh)->value;
    by_index[index] = &made;
    stale.set(index, false);
    due.set(index, false);
    awake_bits.set(index, false);
    resting_bits.set(index, false);
    if (seed != nullptr) {
        seed(seed_context, made);
    }
    mark(made);
    return made;
}

Row *Rows::find(const RID &p_entity) {
    Row *held = rows.getptr(p_entity.get_id());
    if (held != nullptr) {
        mark(*held);
    }
    return held;
}

Row *Rows::peek(const RID &p_entity) {
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
    Row *held = rows.getptr(p_entity.get_id());
    if (held != nullptr) {
        const uint32_t at = held->index;
        by_index[at] = nullptr;
        stale.set(at, false);
        due.set(at, false);
        bodied_bits.set(at, false);
        stepping_bits.set(at, false);
        chooser_bits.set(at, false);
        awake_bits.set(at, false);
        resting_bits.set(at, false);
        free_index.push_back(at);
    }
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
            mark(*row);
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

void Rows::sort_gathered() {
    const LocalVector<Row *> &rows_at = by_index;
    std::sort(
        gathered.ptr(),
        gathered.ptr() + gathered.size(),
        [&rows_at](uint32_t p_left, uint32_t p_right) {
            return rows_at[p_left]->order < rows_at[p_right]->order;
        }
    );
}

void Rows::gather(const store::Bits &p_bits, LocalVector<RID> &r_out) {
    r_out.clear();
    gathered.clear();
    for (uint32_t word = 0; word < p_bits.words.size(); ++word) {
        uint64_t bits = p_bits.words[word];
        while (bits != 0) {
            gathered.push_back(word * 64 + store::Bits::lowest(bits));
            bits &= bits - 1;
        }
    }
    sort_gathered();
    for (uint32_t at = 0; at < gathered.size(); ++at) {
        r_out.push_back(by_index[gathered[at]]->entity);
    }
}

void Rows::choosers(LocalVector<RID> &r_out) {
    settle();
    gather(chooser_bits, r_out);
}

LocalVector<RID> Rows::follow_session_authority(bool p_here) {
    LocalVector<RID> moved;
    for (KeyValue<uint64_t, Row> &held : rows) {
        Row &row = held.value;
        if (row.facts.session_authority_here == p_here) {
            continue;
        }
        mark(row);
        row.facts.session_authority_here = p_here;
        if (sim::resolve(row.facts) != row.mode) {
            moved.push_back(row.entity);
        }
    }
    return moved;
}

void Rows::stepping(LocalVector<RID> &r_out) {
    settle();
    gather(stepping_bits, r_out);
}

void Rows::settle() {
    for (uint32_t at = 0; at < stale_list.size(); ++at) {
        const uint32_t index = stale_list[at];
        if (index >= by_index.size() || !stale.get(index)) {
            continue;
        }
        stale.set(index, false);
        Row *row = by_index[index];
        if (row == nullptr) {
            continue;
        }
        derive(*row);
        due.set(index, true);
    }
    stale_list.clear();
}

void Rows::contact_walk(LocalVector<uint32_t> &r_out) {
    settle();
    gathered.clear();
    for (uint32_t word = 0; word < due.words.size(); ++word) {
        const uint64_t forced = due.words[word] | resting_bits.words[word];
        uint64_t bits = forced | awake_bits.words[word];
        bits &= bodied_bits.words[word];
        while (bits != 0) {
            const uint32_t index = word * 64 + store::Bits::lowest(bits);
            const uint64_t bit = bits & (~bits + 1);
            bits &= bits - 1;
            const Row *row = by_index[index];
            if (row == nullptr) {
                continue;
            }
            if ((forced & bit) != 0 || contact_moded(*row)) {
                gathered.push_back(index);
            }
        }
    }
    due.clear();
    sort_gathered();
    r_out.clear();
    for (uint32_t at = 0; at < gathered.size(); ++at) {
        r_out.push_back(gathered[at]);
    }
}

Row *Rows::at(uint32_t p_index) const {
    return p_index < by_index.size() ? by_index[p_index] : nullptr;
}

bool Rows::is_due(uint32_t p_index) const {
    return p_index < by_index.size()
        && (due.get(p_index) || stale.get(p_index));
}

bool Rows::is_awake(uint32_t p_index) const {
    return p_index < by_index.size() && awake_bits.get(p_index);
}

bool Rows::is_resting(uint32_t p_index) const {
    return p_index < by_index.size() && resting_bits.get(p_index);
}

bool Rows::is_bodied(uint32_t p_index) const {
    return p_index < by_index.size() && bodied_bits.get(p_index);
}

bool Rows::is_stepping(uint32_t p_index) const {
    return p_index < by_index.size() && stepping_bits.get(p_index);
}

bool Rows::is_chooser(uint32_t p_index) const {
    return p_index < by_index.size() && chooser_bits.get(p_index);
}

void Rows::set_awake(const Row &p_row, bool p_awake) {
    awake_bits.set(p_row.index, p_awake);
}

void Rows::set_resting(const Row &p_row, bool p_resting) {
    resting_bits.set(p_row.index, p_resting);
}

void Rows::each(LocalVector<Row *> &r_out) const {
    r_out.clear();
    for (uint32_t at = 0; at < by_index.size(); ++at) {
        if (by_index[at] != nullptr) {
            r_out.push_back(by_index[at]);
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
