#include "netw/store/table.hpp"

#include <cstring>
#include <utility>

#include "godot/physics_body.hpp"
#include "godot/script.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/store/port.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::store {

namespace {

BodyKey body_key_3d(const StringName &p_key) {
    static const StringName position("position");
    static const StringName quaternion("quaternion");
    static const StringName linear("linear_velocity");
    static const StringName angular("angular_velocity");
    static const StringName sleeping("sleeping");
    if (p_key == position) {
        return BodyKey::POSITION;
    }
    if (p_key == quaternion) {
        return BodyKey::TURN;
    }
    if (p_key == linear) {
        return BodyKey::LINEAR;
    }
    if (p_key == angular) {
        return BodyKey::ANGULAR;
    }
    if (p_key == sleeping) {
        return BodyKey::SLEEPING;
    }
    return BodyKey::NONE;
}

BodyKey body_key_2d(const StringName &p_key) {
    static const StringName rotation("rotation");
    static const StringName quaternion("quaternion");
    if (p_key == rotation) {
        return BodyKey::TURN;
    }
    if (p_key == quaternion) {
        return BodyKey::NONE;
    }
    return body_key_3d(p_key);
}

bool script_declares(const Ref<Script> &p_script, const StringName &p_key) {
    const Array declared = gd::script_property_list(p_script);
    for (int at = 0; at < declared.size(); ++at) {
        const Dictionary info = declared[at];
        if (StringName(info.get("name", StringName())) == p_key) {
            return true;
        }
    }
    return false;
}

template <typename T> void put(uint8_t *p_at, const T &p_value) {
    memcpy(p_at, &p_value, sizeof(T));
}

template <typename T> T take(const uint8_t *p_at) {
    T value;
    memcpy(&value, p_at, sizeof(T));
    return value;
}

} // namespace

uint32_t value_bytes_of(int p_type) {
    switch (p_type) {
        case SchemaCore::BOOL:
            return 1;
        case SchemaCore::F32:
        case SchemaCore::F64:
        case SchemaCore::I8:
        case SchemaCore::I16:
        case SchemaCore::I32:
        case SchemaCore::I64:
        case SchemaCore::U8:
        case SchemaCore::U16:
        case SchemaCore::ENTITY:
            return 8;
        case SchemaCore::VECTOR2:
            return sizeof(Vector2);
        case SchemaCore::VECTOR3:
            return sizeof(Vector3);
        case SchemaCore::VECTOR4:
            return sizeof(Vector4);
        case SchemaCore::COLOR:
            return sizeof(Color);
        case SchemaCore::QUATERNION:
            return sizeof(Quaternion);
        default:
            return 0;
    }
}

const char *port_name(Port p_port) {
    switch (p_port) {
        case Port::BODY:
            return "body";
        case Port::ENGINE:
            return "engine";
        case Port::SCRIPT:
            return "script";
        case Port::CUSTOM:
            return "custom";
        case Port::ARRAY:
            return "array";
    }
    return "engine";
}

const char *family_name(Family p_family) {
    switch (p_family) {
        case Family::VOLATILE:
            return "volatile";
        case Family::RETAINED:
            return "retained";
        case Family::WINDOW:
            return "window";
    }
    return "volatile";
}

void Table::compile(const SchemaRecord &p_schema, Family p_family) {
    schema = p_schema;
    family = p_family;
    plan = wire::WirePlan::compile(schema);
    scalar = wire::is_scalar_schema(schema, plan);
    words = plan.valid() ? uint32_t((plan.row_bits() + 63) / 64 + 1) : 0;
    carried = plan.has_variable() ? plan.column_count() : 0;
    scratch = wire::CodeRow::for_plan(plan);
    landing = wire::CodeRow::for_plan(plan);
    base = wire::CodeRow::for_plan(plan);
    columns.resize(uint32_t(schema.column_count()));
    values.resize(columns.size());
    value_offsets.resize(columns.size());
    decode_codes.resize(columns.size());
    row_bytes = 0;
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const table::SchemaColumn *declared = schema.at(int(at));
        columns[at].value_bytes
            = declared->stride == 1 ? value_bytes_of(declared->type) : 0;
        value_offsets[at] = row_bytes;
        row_bytes += columns[at].value_bytes;
    }
    landed.resize(row_bytes);
    exact.resize(row_bytes);
    compile_coders();
}

void Table::compile_coders() {
    coders.resize(columns.size());
    code_offsets.resize(columns.size());
    typed = plan.valid() && carried == 0;
    uint32_t elements = 0;
    for (uint32_t at = 0; at < columns.size(); ++at) {
        coders[at] = typed ? coder_of(*schema.at(int(at)), plan.column(at))
                           : ColumnCoder();
        code_offsets[at] = elements;
        elements += uint32_t(coders[at].slot.stride);
        typed = typed && coders[at].kind != Coder::VARIANT
            && columns[at].value_bytes > 0;
    }
    pulled_codes.resize(elements);
}

void resolve_columns(
    Object *p_node,
    const LocalVector<StringName> &p_keys,
    LocalVector<Column> &r_columns
) {
    r_columns.resize(p_keys.size());
    const bool solid = Object::cast_to<RigidBody3D>(p_node) != nullptr;
    const bool flat = Object::cast_to<RigidBody2D>(p_node) != nullptr;
    const Ref<Script> script
        = p_node != nullptr ? Ref<Script>(p_node->get_script()) : Ref<Script>();
    for (uint32_t at = 0; at < p_keys.size(); ++at) {
        const StringName &key = p_keys[at];
        const BodyKey body = solid ? body_key_3d(key)
            : flat                 ? body_key_2d(key)
                                   : BodyKey::NONE;
        r_columns[at].body = body;
        if (body != BodyKey::NONE) {
            r_columns[at].port = Port::BODY;
        } else if (script.is_valid() && script_declares(script, key)) {
            r_columns[at].port = Port::SCRIPT;
        } else {
            r_columns[at].port = Port::ENGINE;
        }
    }
}

void Table::resolve_ports(Object *p_node) {
    if (ports_resolved || p_node == nullptr) {
        return;
    }
    ports_resolved = true;
    LocalVector<StringName> keys;
    for (uint32_t at = 0; at < columns.size(); ++at) {
        keys.push_back(schema.at(int(at))->key);
    }
    LocalVector<Column> resolved;
    resolve_columns(p_node, keys, resolved);
    for (uint32_t at = 0; at < columns.size(); ++at) {
        columns[at].port = resolved[at].port;
        columns[at].body = resolved[at].body;
        body_columns += resolved[at].port == Port::BODY ? 1 : 0;
    }
}

uint32_t Table::add_row() {
    const uint32_t row = slots.size();
    slots.push_back(NO_SLOT);
    routes.push_back(0);
    owners.push_back(0);
    nodes.push_back(0);
    cells.push_back(NodeCell());
    scripts.push_back(0);
    authorities.push_back(0);
    comps.push_back(0);
    lanes.push_back(0);
    ordinals.push_back(0);
    sources.push_back(uint8_t(Source::NONE));
    flags.push_back(0);
    changed_ticks.push_back(-1);
    frame_ticks.push_back(-1);
    received_ticks.push_back(-1);
    reconcile_acks.push_back(-1);
    tenures.push_back(wire::StreamTenure());
    admit_stamps.push_back(0);
    unsourced.resize(slots.size());
    unsourced.set(row, true);
    working.resize(slots.size());
    working.set(row, true);
    offers.push_back(OFFER_NONE);
    offer_counts[OFFER_NONE] += 1;
    due.resize(slots.size());
    due_ticks.resize(slots.size() * PENDING_DEPTH);
    due_held.push_back(0);
    due_senders.push_back(0);
    due_tenures.push_back(0);
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const uint32_t size = columns[at].value_bytes;
        LocalVector<uint8_t> &held = values[at];
        const uint32_t from = held.size();
        held.resize(from + size);
        for (uint32_t byte = from; byte < held.size(); ++byte) {
            held[byte] = 0;
        }
    }
    carried_values.resize(carried_values.size() + carried);
    const uint32_t first = codes.size();
    codes.resize(first + words);
    for (uint32_t at = first; at < codes.size(); ++at) {
        codes[at] = 0;
    }
    history_ticks.resize(slots.size() * HISTORY_DEPTH);
    history_values.resize(slots.size() * HISTORY_DEPTH * row_bytes);
    history_carried.resize(slots.size() * HISTORY_DEPTH * carried);
    history_next.push_back(0);
    history_held.push_back(0);
    history_appends.push_back(0);
    for (uint32_t at = 0; at < senders.size(); ++at) {
        senders[at].rows.resize(slots.size());
    }
    push_peer_rows();
    selected.reserve(slots.size());
    return row;
}

void Table::set_offer(uint32_t p_row, uint8_t p_offer) {
    offer_counts[offers[p_row]] -= 1;
    offers[p_row] = p_offer;
    offer_counts[p_offer] += 1;
}

uint32_t Table::swap_remove(uint32_t p_row) {
    const uint32_t last = slots.size() - 1;
    set_offer(p_row, OFFER_NONE);
    offer_counts[OFFER_NONE] -= 1;
    if (p_row != last) {
        offers[p_row] = offers[last];
        unsourced.set(p_row, unsourced.get(last));
        working.set(p_row, working.get(last));
        slots[p_row] = slots[last];
        routes[p_row] = routes[last];
        owners[p_row] = owners[last];
        nodes[p_row] = nodes[last];
        cells[p_row] = cells[last];
        scripts[p_row] = scripts[last];
        authorities[p_row] = authorities[last];
        comps[p_row] = comps[last];
        lanes[p_row] = lanes[last];
        ordinals[p_row] = ordinals[last];
        sources[p_row] = sources[last];
        flags[p_row] = flags[last];
        changed_ticks[p_row] = changed_ticks[last];
        frame_ticks[p_row] = frame_ticks[last];
        received_ticks[p_row] = received_ticks[last];
        reconcile_acks[p_row] = reconcile_acks[last];
        tenures[p_row] = tenures[last];
        admit_stamps[p_row] = admit_stamps[last];
        due.set(p_row, due.get(last));
        for (uint32_t at = 0; at < PENDING_DEPTH; ++at) {
            due_ticks[p_row * PENDING_DEPTH + at]
                = due_ticks[last * PENDING_DEPTH + at];
        }
        due_held[p_row] = due_held[last];
        due_senders[p_row] = due_senders[last];
        due_tenures[p_row] = due_tenures[last];
        for (uint32_t at = 0; at < columns.size(); ++at) {
            const uint32_t size = columns[at].value_bytes;
            uint8_t *held = values[at].ptr();
            memcpy(held + p_row * size, held + last * size, size);
        }
        for (uint32_t at = 0; at < carried; ++at) {
            carried_values[p_row * carried + at]
                = carried_values[last * carried + at];
        }
        for (uint32_t at = 0; at < words; ++at) {
            codes[p_row * words + at] = codes[last * words + at];
        }
        const uint32_t span = HISTORY_DEPTH * row_bytes;
        memcpy(
            history_values.ptr() + p_row * span,
            history_values.ptr() + last * span,
            span
        );
        for (uint32_t at = 0; at < HISTORY_DEPTH; ++at) {
            history_ticks[p_row * HISTORY_DEPTH + at]
                = history_ticks[last * HISTORY_DEPTH + at];
        }
        for (uint32_t at = 0; at < HISTORY_DEPTH * carried; ++at) {
            history_carried[p_row * HISTORY_DEPTH * carried + at]
                = history_carried[last * HISTORY_DEPTH * carried + at];
        }
        history_next[p_row] = history_next[last];
        history_held[p_row] = history_held[last];
        history_appends[p_row] = history_appends[last];
        for (uint32_t at = 0; at < senders.size(); ++at) {
            senders[at].rows[p_row] = std::move(senders[at].rows[last]);
        }
        move_peer_rows(p_row, last);
    }
    shrink_peer_rows(last);
    slots.resize(last);
    routes.resize(last);
    owners.resize(last);
    nodes.resize(last);
    cells.resize(last);
    scripts.resize(last);
    authorities.resize(last);
    comps.resize(last);
    lanes.resize(last);
    ordinals.resize(last);
    sources.resize(last);
    flags.resize(last);
    changed_ticks.resize(last);
    frame_ticks.resize(last);
    received_ticks.resize(last);
    reconcile_acks.resize(last);
    tenures.resize(last);
    admit_stamps.resize(last);
    offers.resize(last);
    unsourced.resize(last);
    working.resize(last);
    due.resize(last);
    due_ticks.resize(last * PENDING_DEPTH);
    due_held.resize(last);
    due_senders.resize(last);
    due_tenures.resize(last);
    for (uint32_t at = 0; at < columns.size(); ++at) {
        values[at].resize(last * columns[at].value_bytes);
    }
    carried_values.resize(last * carried);
    codes.resize(last * words);
    history_ticks.resize(last * HISTORY_DEPTH);
    history_values.resize(last * HISTORY_DEPTH * row_bytes);
    history_carried.resize(last * HISTORY_DEPTH * carried);
    history_next.resize(last);
    history_held.resize(last);
    history_appends.resize(last);
    for (uint32_t at = 0; at < senders.size(); ++at) {
        senders[at].rows.resize(last);
    }
    return p_row != last ? last : NO_SLOT;
}

void Table::write_value(
    uint32_t p_row,
    uint32_t p_column,
    const Variant &p_value
) {
    const uint32_t size = columns[p_column].value_bytes;
    if (size == 0) {
        return;
    }
    write_bytes(
        values[p_column].ptr() + uint64_t(p_row) * size,
        p_column,
        p_value
    );
}

void Table::write_bytes(
    uint8_t *p_at,
    uint32_t p_column,
    const Variant &p_value
) const {
    uint8_t *at = p_at;
    switch (schema.at(int(p_column))->type) {
        case SchemaCore::BOOL:
            *at = bool(p_value) ? 1 : 0;
            return;
        case SchemaCore::F32:
        case SchemaCore::F64:
            put(at, double(p_value));
            return;
        case SchemaCore::VECTOR2:
            put(at, Vector2(p_value));
            return;
        case SchemaCore::VECTOR3:
            put(at, Vector3(p_value));
            return;
        case SchemaCore::VECTOR4:
            put(at, Vector4(p_value));
            return;
        case SchemaCore::COLOR:
            put(at, Color(p_value));
            return;
        case SchemaCore::QUATERNION:
            put(at, Quaternion(p_value));
            return;
        default:
            put(at, int64_t(p_value));
            return;
    }
}

Variant Table::read_value(uint32_t p_row, uint32_t p_column) const {
    const uint32_t size = columns[p_column].value_bytes;
    if (size == 0) {
        return carried == 0 ? Variant()
                            : carried_values[p_row * carried + p_column];
    }
    return read_bytes(
        values[p_column].ptr() + uint64_t(p_row) * size,
        p_column
    );
}

Variant Table::read_bytes(const uint8_t *p_at, uint32_t p_column) const {
    const uint8_t *at = p_at;
    switch (schema.at(int(p_column))->type) {
        case SchemaCore::BOOL:
            return *at != 0;
        case SchemaCore::F32:
        case SchemaCore::F64:
            return take<double>(at);
        case SchemaCore::VECTOR2:
            return take<Vector2>(at);
        case SchemaCore::VECTOR3:
            return take<Vector3>(at);
        case SchemaCore::VECTOR4:
            return take<Vector4>(at);
        case SchemaCore::COLOR:
            return take<Color>(at);
        case SchemaCore::QUATERNION:
            return take<Quaternion>(at);
        default:
            return take<int64_t>(at);
    }
}

bool Table::encode(
    uint32_t p_column,
    const Variant &p_value,
    wire::CodeRow &r_row
) const {
    return wire::encode_scalar_column(
        *schema.at(int(p_column)),
        plan.column(p_column),
        p_column,
        p_value,
        r_row
    );
}

bool Table::settle(
    uint32_t p_row,
    const wire::CodeRow &p_codes,
    int64_t p_tick
) {
    uint64_t *held = codes.ptr() + uint64_t(p_row) * words;
    const uint64_t *fresh = p_codes.word_data();
    bool changed = false;
    for (uint32_t at = 0; at < words; ++at) {
        if (held[at] != fresh[at]) {
            held[at] = fresh[at];
            changed = true;
        }
    }
    if (carried != 0) {
        const Variant *given = p_codes.value_data();
        for (uint32_t at = 0; at < carried; ++at) {
            Variant &kept = carried_values[p_row * carried + at];
            if (kept != given[at]) {
                kept = given[at];
                changed = true;
            }
        }
    }
    flags[p_row]
        = (flags[p_row] & ~uint8_t(DIRTY | UNGATHERED | UNENCODED)) | PULLED;
    if (changed) {
        flags[p_row] |= DIRTY;
        changed_ticks[p_row] = p_tick;
    }
    return changed;
}

int Table::settle_exact(uint32_t p_row, bool p_body_columns, int64_t p_tick) {
    const uint8_t *given = exact.ptr();
    for (uint32_t at = 0; at < columns.size(); ++at) {
        if (!p_body_columns && columns[at].port == Port::BODY) {
            continue;
        }
        if (!code_value(
                coders[at],
                given + value_offsets[at],
                pulled_codes.ptr() + code_offsets[at]
            )) {
            return -1;
        }
    }
    uint64_t *held = codes.ptr() + uint64_t(p_row) * words;
    bool changed = false;
    for (uint32_t at = 0; at < columns.size(); ++at) {
        if (!p_body_columns && columns[at].port == Port::BODY) {
            continue;
        }
        const ColumnCoder &coder = coders[at];
        const uint64_t *fresh = pulled_codes.ptr() + code_offsets[at];
        for (int element = 0; element < coder.slot.stride; ++element) {
            if (read_code(held, coder.slot, element) != fresh[element]) {
                changed = true;
                write_code(held, coder.slot, element, fresh[element]);
            }
        }
        const uint32_t size = columns[at].value_bytes;
        memcpy(
            values[at].ptr() + uint64_t(p_row) * size,
            given + value_offsets[at],
            size
        );
    }
    flags[p_row]
        = (flags[p_row] & ~uint8_t(DIRTY | UNGATHERED | UNENCODED)) | PULLED;
    if (changed) {
        flags[p_row] |= DIRTY;
        changed_ticks[p_row] = p_tick;
    }
    return changed ? 1 : 0;
}

int Table::first_difference(
    uint32_t p_row,
    const wire::CodeRow &p_codes
) const {
    wire::CodeRow stored;
    stored.assign(plan, codes_of(p_row), carried_of(p_row));
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        const wire::ColumnPlan &slot = plan.column(index);
        if (slot.variable) {
            if (stored.read_value(index) != p_codes.read_value(index)) {
                return int(index);
            }
            continue;
        }
        for (int element = 0; element < slot.stride; ++element) {
            if (stored.read(slot, element) != p_codes.read(slot, element)) {
                return int(index);
            }
        }
    }
    return -1;
}

uint32_t Table::dirty_count() const {
    uint32_t count = 0;
    for (uint32_t row = 0; row < flags.size(); ++row) {
        count += (flags[row] & DIRTY) != 0 ? 1 : 0;
    }
    return count;
}

int64_t Table::byte_count() const {
    int64_t bytes = int64_t(slots.size())
        * int64_t(sizeof(uint32_t) + sizeof(int64_t) * 2 + sizeof(uint64_t) * 2
                  + 4);
    for (uint32_t at = 0; at < values.size(); ++at) {
        bytes += int64_t(values[at].size());
    }
    bytes += int64_t(carried_values.size()) * int64_t(sizeof(Variant));
    bytes += int64_t(codes.size()) * int64_t(sizeof(uint64_t));
    bytes += int64_t(history_ticks.size()) * int64_t(sizeof(int64_t));
    bytes += int64_t(history_values.size());
    bytes += int64_t(history_carried.size()) * int64_t(sizeof(Variant));
    for (uint32_t at = 0; at < senders.size(); ++at) {
        for (const Received &held : senders[at].rows) {
            bytes += int64_t(held.revisions.size() + held.images.size())
                * int64_t(sizeof(uint64_t));
            bytes += int64_t(held.carried.size()) * int64_t(sizeof(Variant));
        }
    }
    return bytes;
}

#if defined(NETW_PROFILING)
int64_t Table::held_bytes() const {
    int64_t bytes = int64_t(sizeof(Table)) + held::vector(columns)
        + held::vector(slots) + held::vector(routes) + held::vector(owners)
        + held::vector(nodes) + held::vector(cells) + held::vector(scripts)
        + held::vector(authorities) + held::vector(comps)
        + held::vector(lanes) + held::vector(ordinals)
        + held::vector(sources) + held::vector(flags)
        + held::vector(changed_ticks) + held::vector(frame_ticks)
        + held::vector(received_ticks) + held::vector(reconcile_acks)
        + held::vector(tenures) + held::vector(admit_stamps)
        + held::vectors(values) + held::vector(carried_values)
        + held::vector(codes) + held::vector(senders)
        + held::vector(value_offsets) + held::vector(history_ticks)
        + held::vector(history_values) + held::vector(history_carried)
        + held::vector(history_next) + held::vector(history_held)
        + held::vector(history_appends) + held::vector(unsourced.words)
        + held::vector(working.words) + held::vector(touched_routes)
        + held::map(lagging) + held::vector(offers) + held::vector(due.words)
        + held::vector(due_ticks) + held::vector(due_held)
        + held::vector(due_senders) + held::vector(due_tenures)
        + held::vector(arrived_window) + held::vector(coders)
        + held::vector(exact) + held::vector(pulled_codes)
        + held::vector(code_offsets) + scratch.held_bytes()
        + landing.held_bytes() + base.held_bytes() + held::vector(landed)
        + held::vector(decode_codes) + held::vector(selected);
    for (const Sender &sender : senders) {
        bytes += held::vector(sender.rows);
        for (const Received &received : sender.rows) {
            bytes += held::vector(received.revisions)
                + held::vector(received.images)
                + held::vector(received.carried);
        }
    }
    for (const repl::WindowSample &sample : arrived_window) {
        bytes += sample.row.held_bytes();
    }
    return bytes;
}

int64_t Table::peer_bytes() const {
    int64_t bytes = held::vector(peers);
    for (const PeerRows &peer : peers) {
        bytes += held::vector(peer.admitted.words)
            + held::vector(peer.sendable.words) + held::vector(peer.owed.words)
            + held::vector(peer.queued.words) + held::vector(peer.due.words)
            + held::vector(peer.stream) + held::vector(peer.request)
            + held::vector(peer.token) + held::vector(peer.origin)
            + held::vector(peer.opened)
            + held::vector(peer.next_rev) + held::vector(peer.acked_rev)
            + held::vector(peer.exposed_rev) + held::vector(peer.baseline)
            + held::vector(peer.baseline_carried) + held::vector(peer.acc)
            + held::vector(peer.last_sent) + held::vector(peer.refresh_at)
            + held::vector(peer.repair_at);
    }
    return bytes;
}
#endif

} // namespace netw::store
