#include "netw/lagcomp_book.hpp"

#include <algorithm>

#include "netw/api/schema_core.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw {

namespace {

constexpr uint32_t MASK_FIELDS = 64;

uint64_t bit_of(uint32_t p_field) {
    return uint64_t(1) << p_field;
}

bool declares(
    const LocalVector<StringName> &p_declared,
    const StringName &p_key
) {
    if (p_declared.is_empty()) {
        return true;
    }
    for (const StringName &key : p_declared) {
        if (key == p_key) {
            return true;
        }
    }
    return false;
}

} // namespace

void LagBook::set_window(int64_t p_samples) {
    uint32_t rounded = 1;
    while (int64_t(rounded) < p_samples) {
        rounded <<= 1;
    }
    window = rounded;
    if (compiled) {
        size_rings();
    }
}

void LagBook::compile(
    const predict::FieldCodec &p_source,
    const LocalVector<StringName> &p_declared
) {
    codec.clear();
    for (int at = 0; at < p_source.count(); ++at) {
        const StringName &key = p_source.keys[uint32_t(at)];
        if (!declares(p_declared, key)) {
            continue;
        }
        const table::SchemaColumn *column = p_source.schema.at(at);
        codec.append(
            key,
            p_source.quantizers[uint32_t(at)],
            p_source.types[uint32_t(at)],
            column != nullptr ? column->type : -1
        );
    }
    codec.seal();
    compiled = true;
    size_rings();
}

void LagBook::compile_keys(const Array &p_keys) {
    codec.clear();
    for (int64_t at = 0; at < p_keys.size(); ++at) {
        codec.append(
            StringName(p_keys[at]),
            Ref<NetwQuantize>(),
            int(Variant::NIL),
            SchemaCore::VARIANT
        );
    }
    codec.seal();
    compiled = true;
    size_rings();
}

void LagBook::size_rings() {
    fields = uint32_t(std::min(codec.count(), int(MASK_FIELDS)));
    scratch = wire::CodeRow::for_plan(codec.plan);
    reader = wire::CodeRow::for_plan(codec.plan);
    words = scratch.word_count();
    ticks.resize(window);
    present.resize(window);
    coded.resize(window);
    images.resize(window * words);
    carried.clear();
    staged.resize(fields);
    element_codes.resize(fields);
    recorded.resize(window);
    for (uint32_t at = 0; at < window; ++at) {
        ticks[at] = -1;
        present[at] = 0;
        coded[at] = 0;
        recorded[at] = -1;
    }
    head = 0;
    held = 0;
    recorded_head = 0;
    recorded_held = 0;
}

void LagBook::stage(const Dictionary &p_payload) {
    scratch.clear();
    staged_present = 0;
    staged_coded = 0;
    for (uint32_t field = 0; field < fields; ++field) {
        staged[field] = Variant();
        const StringName &key = codec.keys[field];
        if (!p_payload.has(key)) {
            continue;
        }
        const Variant value = p_payload[key];
        staged_present |= bit_of(field);
        if (codec.holds(int(field), value)
            && wire::encode_scalar_column(
                *codec.schema.at(int(field)),
                codec.plan.column(field),
                field,
                value,
                scratch
            )) {
            staged_coded |= bit_of(field);
        } else {
            staged[field] = value;
        }
    }
}

bool LagBook::staged_equals(uint32_t p_at) const {
    if (present[p_at] != staged_present || coded[p_at] != staged_coded) {
        return false;
    }
    const uint64_t *source = scratch.word_data();
    for (uint32_t word = 0; word < words; ++word) {
        if (images[p_at * words + word] != source[word]) {
            return false;
        }
    }
    const uint64_t carrying = staged_present & ~staged_coded;
    for (uint32_t field = 0; field < fields; ++field) {
        if ((carrying & bit_of(field)) == 0) {
            continue;
        }
        if (carried.is_empty()
            || carried[p_at * fields + field] != staged[field]) {
            return false;
        }
    }
    return true;
}

void LagBook::write_staged(uint32_t p_at, int64_t p_tick) {
    ticks[p_at] = p_tick;
    present[p_at] = staged_present;
    coded[p_at] = staged_coded;
    const uint64_t *source = scratch.word_data();
    for (uint32_t word = 0; word < words; ++word) {
        images[p_at * words + word] = source[word];
    }
    const uint64_t carrying = staged_present & ~staged_coded;
    if (carrying == 0 && carried.is_empty()) {
        return;
    }
    if (carried.is_empty()) {
        carried.resize(window * fields);
    }
    for (uint32_t field = 0; field < fields; ++field) {
        carried[p_at * fields + field] = staged[field];
    }
}

void LagBook::note_recorded(int64_t p_tick) {
    if (recorded_held > 0) {
        const uint32_t newest
            = (recorded_head + recorded_held - 1) & (window - 1);
        if (recorded[newest] == p_tick) {
            return;
        }
    }
    if (recorded_held < window) {
        recorded[(recorded_head + recorded_held) & (window - 1)] = p_tick;
        recorded_held += 1;
        return;
    }
    recorded[recorded_head] = p_tick;
    recorded_head = (recorded_head + 1) & (window - 1);
}

int LagBook::find_sample(int64_t p_tick) const {
    for (uint32_t age = 0; age < held; ++age) {
        const uint32_t at = slot_of_age(age);
        if (ticks[at] == p_tick) {
            return int(at);
        }
        if (ticks[at] < p_tick) {
            return -1;
        }
    }
    return -1;
}

bool LagBook::record(int64_t p_tick, const Dictionary &p_payload) {
    NETW_ZONE_LEAF(
        "Lagcomp book record",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    if (!compiled) {
        compile_keys(p_payload.keys());
    }
    note_recorded(p_tick);
    stage(p_payload);
    uint32_t place = held;
    while (place > 0 && ticks[slot_at(place - 1)] > p_tick) {
        place -= 1;
    }
    if (place > 0) {
        const uint32_t before = slot_at(place - 1);
        if (ticks[before] == p_tick) {
            write_staged(before, p_tick);
            return true;
        }
        if (staged_equals(before)) {
            return false;
        }
    }
    if (held == window) {
        if (place == 0) {
            return false;
        }
        head = (head + 1) & (window - 1);
        held -= 1;
        place -= 1;
    }
    for (uint32_t at = held; at > place; --at) {
        copy_entry(slot_at(at), slot_at(at - 1));
    }
    held += 1;
    write_staged(slot_at(place), p_tick);
    return true;
}

void LagBook::copy_entry(uint32_t p_to, uint32_t p_from) {
    ticks[p_to] = ticks[p_from];
    present[p_to] = present[p_from];
    coded[p_to] = coded[p_from];
    for (uint32_t word = 0; word < words; ++word) {
        images[p_to * words + word] = images[p_from * words + word];
    }
    if (carried.is_empty()) {
        return;
    }
    for (uint32_t field = 0; field < fields; ++field) {
        carried[p_to * fields + field] = carried[p_from * fields + field];
    }
}

int64_t LagBook::oldest_recorded() const {
    int64_t out = -1;
    for (uint32_t at = 0; at < recorded_held; ++at) {
        const int64_t tick = recorded[(recorded_head + at) & (window - 1)];
        out = out < 0 || tick < out ? tick : out;
    }
    return out;
}

int64_t LagBook::sample_tick(int64_t p_tick) const {
    if (p_tick < floor_tick || recorded_held == 0
        || p_tick < oldest_recorded()) {
        return -1;
    }
    for (uint32_t age = 0; age < held; ++age) {
        const int64_t tick = ticks[slot_of_age(age)];
        if (tick <= p_tick) {
            return tick < floor_tick ? -1 : tick;
        }
    }
    return -1;
}

Dictionary LagBook::sample(int64_t p_tick) const {
    Dictionary out;
    const int64_t tick = sample_tick(p_tick);
    if (tick < 0) {
        return out;
    }
    const int found = find_sample(tick);
    if (found < 0) {
        return out;
    }
    const uint32_t at = uint32_t(found);
    if (words > 0) {
        reader.assign(codec.plan, images.ptr() + at * words, nullptr);
    }
    for (uint32_t field = 0; field < fields; ++field) {
        if ((present[at] & bit_of(field)) == 0) {
            continue;
        }
        Variant value;
        if ((coded[at] & bit_of(field)) == 0) {
            value = carried.is_empty() ? Variant()
                                       : carried[at * fields + field];
        } else if (!wire::decode_scalar_column(
                       *codec.schema.at(int(field)),
                       codec.plan.column(field),
                       field,
                       reader,
                       element_codes[field],
                       value
                   )) {
            continue;
        }
        out[codec.keys[field]] = value;
    }
    return out;
}

bool LagBook::recorded_at(int64_t p_tick) const {
    if (p_tick < floor_tick) {
        return false;
    }
    for (uint32_t at = 0; at < recorded_held; ++at) {
        if (recorded[(recorded_head + at) & (window - 1)] == p_tick) {
            return true;
        }
    }
    return false;
}

void LagBook::trim_before(int64_t p_tick) {
    floor_tick = std::max(floor_tick, p_tick);
}

int64_t LagBook::newest_tick() const {
    return held == 0 ? -1 : ticks[slot_of_age(0)];
}

int64_t LagBook::byte_count() const {
    return int64_t(ticks.size()) * 8 + int64_t(present.size()) * 8
        + int64_t(coded.size()) * 8 + int64_t(images.size()) * 8
        + int64_t(carried.size()) * int64_t(sizeof(Variant))
        + int64_t(recorded.size()) * 8;
}

} // namespace netw
