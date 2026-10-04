#include <cstring>
#include <utility>

#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/repl/snapshot_frame.hpp"
#include "netw/store/world.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::store {

uint32_t Table::find_sender(int p_id) const {
    for (uint32_t at = 0; at < senders.size(); ++at) {
        if (senders[at].id == p_id) {
            return at;
        }
    }
    return NO_SLOT;
}

uint32_t Table::sender_index(int p_id) {
    const uint32_t found = find_sender(p_id);
    if (found != NO_SLOT) {
        return found;
    }
    const uint32_t made = senders.size();
    senders.resize(made + 1);
    senders[made].id = p_id;
    senders[made].rows.resize(slots.size());
    return made;
}

void Table::forget_sender(int p_id) {
    const uint32_t found = find_sender(p_id);
    if (found == NO_SLOT) {
        return;
    }
    const uint32_t last = senders.size() - 1;
    if (found != last) {
        senders[found] = std::move(senders[last]);
    }
    senders.resize(last);
}

const uint64_t *Table::received_image(
    const Received &p_received,
    uint64_t p_revision,
    const Variant *&r_carried
) const {
    r_carried = nullptr;
    for (uint32_t age = 0; age < p_received.held; ++age) {
        const uint32_t at
            = (p_received.next + RECEIVED_DEPTH - 1 - age) % RECEIVED_DEPTH;
        if (p_received.revisions[at] != p_revision) {
            continue;
        }
        if (carried != 0) {
            r_carried = p_received.carried.ptr() + uint64_t(at) * carried;
        }
        return p_received.images.ptr() + uint64_t(at) * words;
    }
    return nullptr;
}

void Table::keep_received(
    Received &r_received,
    uint64_t p_revision,
    const wire::CodeRow &p_codes
) {
    if (r_received.revisions.size() != RECEIVED_DEPTH) {
        r_received.revisions.resize(RECEIVED_DEPTH);
        r_received.images.resize(RECEIVED_DEPTH * words);
        r_received.carried.resize(RECEIVED_DEPTH * carried);
    }
    const uint32_t at = r_received.next;
    r_received.revisions[at] = p_revision;
    const uint64_t *given = p_codes.word_data();
    for (uint32_t word = 0; word < words; ++word) {
        r_received.images[at * words + word] = given[word];
    }
    const Variant *kept = p_codes.value_data();
    for (uint32_t column = 0; column < carried; ++column) {
        r_received.carried[at * carried + column] = kept[column];
    }
    r_received.next = (at + 1) % RECEIVED_DEPTH;
    if (r_received.held < RECEIVED_DEPTH) {
        r_received.held += 1;
    }
    r_received.accepted = p_revision;
}

bool Table::decode_row(const wire::CodeRow &p_codes) {
    uint8_t *staged = landed.ptr();
    for (uint32_t at = 0; at < columns.size(); ++at) {
        if (columns[at].value_bytes == 0) {
            continue;
        }
        Variant value;
        if (!wire::decode_scalar_column(
                *schema.at(int(at)),
                plan.column(at),
                at,
                p_codes,
                decode_codes[at],
                value
            )) {
            return false;
        }
        write_bytes(staged + value_offsets[at], at, value);
    }
    return true;
}

bool Table::land(uint32_t p_row, const wire::CodeRow &p_codes, int64_t p_tick) {
    if (!decode_row(p_codes)) {
        return false;
    }
    uint64_t *held = codes.ptr() + uint64_t(p_row) * words;
    const uint64_t *fresh = p_codes.word_data();
    bool changed = false;
    for (uint32_t at = 0; at < words; ++at) {
        changed = changed || held[at] != fresh[at];
        held[at] = fresh[at];
    }
    const Variant *given = p_codes.value_data();
    for (uint32_t at = 0; at < carried; ++at) {
        Variant &kept = carried_values[p_row * carried + at];
        changed = changed || kept != given[at];
        kept = given[at];
    }
    const uint8_t *staged = landed.ptr();
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const uint32_t size = columns[at].value_bytes;
        memcpy(
            values[at].ptr() + uint64_t(p_row) * size,
            staged + value_offsets[at],
            size
        );
    }
    received_ticks[p_row] = p_tick;
    if (changed) {
        changed_ticks[p_row] = p_tick;
    }
    if (changed || history_count(p_row) == 0) {
        record(p_row, p_tick, staged);
    }
    return true;
}

bool Table::record_codes(
    uint32_t p_row,
    const wire::CodeRow &p_codes,
    int64_t p_tick
) {
    if (!decode_row(p_codes)) {
        return false;
    }
    record(p_row, p_tick, landed.ptr());
    return true;
}

void Table::record_held(uint32_t p_row, int64_t p_tick) {
    const uint64_t *held = codes_of(p_row);
    uint8_t *staged = landed.ptr();
    uint64_t element_codes[4] = {0, 0, 0, 0};
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const ColumnCoder &coder = coders[at];
        for (int element = 0; element < coder.slot.stride; ++element) {
            element_codes[element] = read_code(held, coder.slot, element);
        }
        canonical_value(coder, element_codes, staged + value_offsets[at]);
    }
    record(p_row, p_tick, staged);
}

void Table::record(uint32_t p_row, int64_t p_tick, const uint8_t *p_image) {
    const uint32_t at = history_next[p_row];
    const uint64_t sample = uint64_t(p_row) * HISTORY_DEPTH + at;
    history_ticks[sample] = p_tick;
    memcpy(history_values.ptr() + sample * row_bytes, p_image, row_bytes);
    for (uint32_t column = 0; column < carried; ++column) {
        history_carried[sample * carried + column]
            = carried_values[p_row * carried + column];
    }
    history_next[p_row] = uint8_t((at + 1) % HISTORY_DEPTH);
    if (history_held[p_row] < HISTORY_DEPTH) {
        history_held[p_row] += 1;
    }
    history_appends[p_row] += 1;
    touch(p_row);
}

void Table::touch(uint32_t p_row) {
    if (touched_routes.size() >= row_count() * 2 + 64) {
        touched_overflow = true;
        return;
    }
    touched_routes.push_back(routes[p_row]);
}

uint32_t Table::history_slot(uint32_t p_row, uint32_t p_age) const {
    return p_row * HISTORY_DEPTH
        + (history_next[p_row] + HISTORY_DEPTH - 1 - p_age) % HISTORY_DEPTH;
}

int64_t Table::history_tick(uint32_t p_row, uint32_t p_age) const {
    if (p_age >= history_count(p_row)) {
        return -1;
    }
    return history_ticks[history_slot(p_row, p_age)];
}

int Table::history_at(uint32_t p_row, int64_t p_tick) const {
    int best = -1;
    int64_t best_tick = -1;
    for (uint32_t age = 0; age < history_count(p_row); ++age) {
        const int64_t tick = history_ticks[history_slot(p_row, age)];
        if (tick <= p_tick && (best < 0 || tick > best_tick)) {
            best = int(age);
            best_tick = tick;
        }
    }
    return best;
}

Variant Table::history_value(
    uint32_t p_row,
    uint32_t p_age,
    uint32_t p_column
) const {
    if (p_age >= history_count(p_row) || p_column >= columns.size()) {
        return Variant();
    }
    const uint32_t sample = history_slot(p_row, p_age);
    if (columns[p_column].value_bytes == 0) {
        return carried == 0 ? Variant()
                            : history_carried[sample * carried + p_column];
    }
    return read_bytes(
        history_values.ptr() + uint64_t(sample) * row_bytes
            + value_offsets[p_column],
        p_column
    );
}

uint32_t Table::owe_push(
    uint32_t p_row,
    int64_t p_tick,
    int64_t p_sender,
    uint64_t p_tenure
) {
    uint32_t dropped = 0;
    if (due_held[p_row] > 0
        && (due_senders[p_row] != p_sender || due_tenures[p_row] != p_tenure)) {
        dropped = due_held[p_row];
        due_held[p_row] = 0;
    }
    int64_t *ticks = due_ticks.ptr() + uint64_t(p_row) * PENDING_DEPTH;
    if (due_held[p_row] == PENDING_DEPTH) {
        for (uint32_t at = 1; at < PENDING_DEPTH; ++at) {
            ticks[at - 1] = ticks[at];
        }
        due_held[p_row] -= 1;
    }
    ticks[due_held[p_row]] = p_tick;
    due_held[p_row] += 1;
    due_senders[p_row] = p_sender;
    due_tenures[p_row] = p_tenure;
    due.set(p_row, true);
    return dropped;
}

bool Table::take_due(uint32_t p_row, int64_t p_display, int64_t &r_stamp) {
    int64_t *ticks = due_ticks.ptr() + uint64_t(p_row) * PENDING_DEPTH;
    bool taken = false;
    uint32_t kept = 0;
    for (uint32_t at = 0; at < due_held[p_row]; ++at) {
        if (ticks[at] <= p_display) {
            r_stamp = taken && r_stamp > ticks[at] ? r_stamp : ticks[at];
            taken = true;
            continue;
        }
        ticks[kept] = ticks[at];
        kept += 1;
    }
    due_held[p_row] = uint8_t(kept);
    due.set(p_row, kept > 0);
    return taken;
}

void Table::drop_due(uint32_t p_row) {
    due_held[p_row] = 0;
    due.set(p_row, false);
}

Landing World::receive(
    const RowAddress &p_at,
    int p_sender,
    const PackedByteArray &p_frame,
    int64_t p_base_tick,
    bool p_windowed
) {
    NETW_ZONE_LEAF(
        "Store receive row",
        colors::TABLE,
        profile::SUBSYSTEM_ENTITY
    );
#if defined(NETW_TESTS)
    receive_work.frames += 1;
    const int64_t allocated = allocations_seen();
#endif
    const Landing verdict
        = land_frame(p_at, p_sender, p_frame, p_base_tick, p_windowed);
#if defined(NETW_TESTS)
    receive_work.landed += verdict == Landing::ACCEPTED ? 1 : 0;
    receive_work.allocations += allocations_seen() - allocated;
#endif
    return verdict;
}

Landing World::admits(
    const RowAddress &p_at,
    int p_sender,
    const PackedByteArray &p_frame,
    bool p_windowed,
    uint64_t &r_accepted,
    const wire::CodeRow *&r_baseline
) {
    r_accepted = 0;
    r_baseline = nullptr;
    Table &table = *tables[p_at.table];
    if (!table.plan.valid()) {
        return Landing::MALFORMED;
    }
    repl::SnapshotHeader named;
    const bool readable = p_windowed
        ? repl::name_snapshot_window(p_frame, named)
        : repl::name_snapshot_row(p_frame, named);
    if (!readable) {
        return Landing::MALFORMED;
    }
    const uint32_t found = table.find_sender(p_sender);
    const Received *held = found != Table::NO_SLOT
        ? &table.senders[found].rows[p_at.row]
        : nullptr;
    const bool fresh = held == nullptr || held->token != named.token;
    r_accepted = fresh ? 0 : held->accepted;
    if (named.revision < wire::SNAPSHOT_FIRST_REVISION) {
        return Landing::STALE;
    }
    if (r_accepted != 0 && named.revision <= r_accepted) {
        return named.revision == r_accepted ? Landing::DUPLICATE
                                            : Landing::STALE;
    }
    if (p_windowed || named.absolute()) {
        return Landing::ACCEPTED;
    }
    const Variant *carried = nullptr;
    const uint64_t *image = fresh
        ? nullptr
        : table.received_image(*held, named.baseline_revision(), carried);
    if (image == nullptr) {
        return Landing::BASELINE_UNKNOWN;
    }
    table.base.assign(table.plan, image, carried);
    r_baseline = &table.base;
    return Landing::ACCEPTED;
}

Landing World::land_frame(
    const RowAddress &p_at,
    int p_sender,
    const PackedByteArray &p_frame,
    int64_t p_base_tick,
    bool p_windowed
) {
    Table &table = *tables[p_at.table];
    const uint32_t row = p_at.row;
    uint64_t accepted = 0;
    const wire::CodeRow *baseline = nullptr;
    const Landing admitted
        = admits(p_at, p_sender, p_frame, p_windowed, accepted, baseline);
    if (admitted != Landing::ACCEPTED) {
        return admitted;
    }
    repl::SnapshotHeader header;
    Received &held = table.senders[table.sender_index(p_sender)].rows[row];
    if (p_windowed) {
        LocalVector<repl::WindowSample> &samples = table.arrived_window;
        if (!repl::read_snapshot_window(
                p_frame,
                p_base_tick,
                table.plan,
                header,
                samples
            )) {
            return Landing::MALFORMED;
        }
        for (uint32_t at = 0; at < samples.size(); ++at) {
            if (!table.land(row, samples[at].row, samples[at].tick)) {
                return Landing::MALFORMED;
            }
        }
        table.arrived_tick = header.tick;
        table.arrived_ack = header.reconcile_ack;
        table.arrived_whole = true;
        table.reconcile_acks[row] = header.reconcile_ack;
        held.token = header.token;
        held.accepted = header.revision;
        return Landing::ACCEPTED;
    }
    repl::SnapshotRefusal refusal = repl::SnapshotRefusal::NONE;
    if (!repl::read_snapshot_row_into(
            p_frame,
            p_base_tick,
            table.plan,
            header,
            table.landing,
            baseline,
            &refusal
        )) {
        return refusal == repl::SnapshotRefusal::BASELINE_UNKNOWN
            ? Landing::BASELINE_UNKNOWN
            : Landing::MALFORMED;
    }
    const int64_t tick = header.tick >= 0 ? header.tick : p_base_tick;
    if (!table.land(row, table.landing, tick)) {
        return Landing::MALFORMED;
    }
    table.arrived_tick = header.tick;
    table.arrived_ack = header.reconcile_ack;
    table.arrived_whole = header.absolute();
    table.arrived_window.clear();
    table.reconcile_acks[row] = header.reconcile_ack;
    if (held.token != header.token) {
        held.token = header.token;
        held.accepted = 0;
        held.held = 0;
        held.next = 0;
    }
    table.keep_received(held, header.revision, table.landing);
    return Landing::ACCEPTED;
}

void World::forget_sender(int p_id) {
    for (uint32_t at = 0; at < tables.size(); ++at) {
        tables[at]->forget_sender(p_id);
    }
}

} // namespace netw::store
