#include "netw/predict/transition_book.hpp"

#include <algorithm>

#include "godot/utility.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/colors.hpp"
#include "netw/profile.hpp"
#include "netw/wire/value_row.hpp"

using namespace godot;

namespace netw::predict {

namespace {

constexpr uint32_t MASK_FIELDS = 64;
constexpr uint32_t FNV_OFFSET = 2166136261u;
constexpr uint32_t FNV_PRIME = 16777619u;

uint64_t bit_of(int p_field) {
    return uint64_t(1) << uint32_t(p_field);
}

uint64_t mask_of(uint32_t p_fields) {
    return p_fields >= MASK_FIELDS ? ~uint64_t(0)
                                   : (uint64_t(1) << p_fields) - 1;
}

Variant detached(const Variant &p_value) {
    switch (p_value.get_type()) {
        case Variant::ARRAY:
            return Array(p_value).duplicate(true);
        case Variant::DICTIONARY:
            return Dictionary(p_value).duplicate(true);
        default:
            return p_value;
    }
}

template <typename T>
void copy_values(LocalVector<T> &r_target, const LocalVector<T> &p_source) {
    r_target.resize(p_source.size());
    for (uint32_t at = 0; at < p_source.size(); ++at) {
        r_target[at] = p_source[at];
    }
}

void mix(uint32_t &r_hash, uint8_t p_byte) {
    r_hash = (r_hash ^ uint32_t(p_byte)) * FNV_PRIME;
}

void mix_word(uint32_t &r_hash, uint64_t p_word) {
    for (int shift = 0; shift < 64; shift += 8) {
        mix(r_hash, uint8_t(p_word >> shift));
    }
}

void mix_value(uint32_t &r_hash, const Variant &p_value) {
    const PackedByteArray bytes = gd::var_to_bytes(p_value);
    const uint8_t *data = bytes.ptr();
    for (int64_t at = 0; at < bytes.size(); ++at) {
        mix(r_hash, data[at]);
    }
}

} // namespace

uint64_t causal_scope(const Wiring &p_wiring) {
    uint64_t out = 0;
    const int count = std::min(p_wiring.count(), int(MASK_FIELDS));
    for (int at = 0; at < count; ++at) {
        if (p_wiring.causal[uint32_t(at)] != 0) {
            out |= bit_of(at);
        }
    }
    return out;
}

uint64_t family_scope(const Wiring &p_wiring, StateFamily p_family) {
    uint64_t out = 0;
    const int count = std::min(p_wiring.count(), int(MASK_FIELDS));
    for (int at = 0; at < count; ++at) {
        if (p_wiring.causal[uint32_t(at)] != 0
            && p_wiring.state_family[uint32_t(at)] == int(p_family)) {
            out |= bit_of(at);
        }
    }
    return out;
}

int32_t fingerprint_codes(
    const FieldCodec &p_codec,
    const wire::CodeRow &p_row,
    uint64_t p_present,
    uint64_t p_coded,
    const Variant *p_carried,
    uint64_t p_scope,
    bool &r_any
) {
    NETW_ZONE_LEAF(
        "Predict fingerprint codes",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    uint32_t hash = FNV_OFFSET;
    r_any = false;
    const uint32_t fields
        = uint32_t(std::min(p_codec.count(), int(MASK_FIELDS)));
    for (uint32_t field = 0; field < fields; ++field) {
        const uint64_t bit = bit_of(int(field));
        if ((p_present & p_scope & bit) == 0) {
            continue;
        }
        r_any = true;
        mix(hash, uint8_t(field));
        if ((p_coded & bit) == 0) {
            mix(hash, 0);
            if (p_carried != nullptr) {
                mix_value(hash, p_carried[field]);
            }
            continue;
        }
        mix(hash, 1);
        const wire::ColumnPlan &slot = p_codec.plan.column(field);
        for (int element = 0; element < slot.stride; ++element) {
            mix_word(hash, p_row.read(slot, element));
        }
    }
    return int32_t(hash);
}

int32_t fingerprint_row(
    const FieldCodec &p_codec,
    const StateRow &p_row,
    uint64_t p_scope,
    wire::CodeRow &r_scratch,
    bool &r_any
) {
    if (!r_scratch.valid_for(p_codec.plan)) {
        r_scratch = wire::CodeRow::for_plan(p_codec.plan);
    }
    r_scratch.clear();
    uint64_t present = 0;
    uint64_t coded = 0;
    const uint32_t span = std::min(
        uint32_t(std::min(p_codec.count(), int(MASK_FIELDS))),
        p_row.values.size()
    );
    for (uint32_t field = 0; field < span; ++field) {
        const uint64_t bit = bit_of(int(field));
        if ((p_scope & bit) == 0 || !p_row.has(int(field))) {
            continue;
        }
        present |= bit;
        const Variant &value = p_row.values[field];
        if (p_codec.holds(int(field), value)
            && wire::encode_scalar_column(
                *p_codec.schema.at(int(field)),
                p_codec.plan.column(field),
                field,
                value,
                r_scratch
            )) {
            coded |= bit;
        }
    }
    return fingerprint_codes(
        p_codec,
        r_scratch,
        present,
        coded,
        p_row.values.ptr(),
        p_scope,
        r_any
    );
}

TransitionBook::TransitionBook(const TransitionBook &p_other) {
    *this = p_other;
}

TransitionBook &TransitionBook::operator=(const TransitionBook &p_other) {
    if (this == &p_other) {
        return *this;
    }
    depth = p_other.depth;
    words = p_other.words;
    fields = p_other.fields;
    floor_key = p_other.floor_key;
    newest = p_other.newest;
    held = p_other.held;
    copy_values(keys, p_other.keys);
    copy_values(labels, p_other.labels);
    copy_values(fresh, p_other.fresh);
    copy_values(origins, p_other.origins);
    copy_values(relays, p_other.relays);
    copy_values(present, p_other.present);
    copy_values(coded, p_other.coded);
    copy_values(images, p_other.images);
    copy_values(carried, p_other.carried);
    copy_values(element_codes, p_other.element_codes);
    scratch.copy_from(p_other.scratch);
    return *this;
}

void TransitionBook::reset(const FieldCodec &p_codec) {
    fields = uint32_t(std::min(p_codec.count(), int(MASK_FIELDS)));
    scratch = wire::CodeRow::for_plan(p_codec.plan);
    words = scratch.word_count();
    keys.resize(depth);
    labels.resize(depth);
    fresh.resize(depth);
    origins.resize(depth);
    relays.resize(depth);
    present.resize(depth);
    coded.resize(depth);
    images.resize(depth * words);
    carried.clear();
    element_codes.clear();
    element_codes.resize(fields);
    clear();
}

void TransitionBook::set_depth(uint32_t p_depth) {
    if (p_depth == depth) {
        return;
    }
    if (keys.is_empty()) {
        depth = p_depth;
        return;
    }
    const TransitionBook old(*this);
    depth = p_depth;
    keys.reset();
    labels.reset();
    fresh.reset();
    origins.reset();
    relays.reset();
    present.reset();
    coded.reset();
    images.reset();
    carried.reset();
    keys.resize(depth);
    labels.resize(depth);
    fresh.resize(depth);
    origins.resize(depth);
    relays.resize(depth);
    present.resize(depth);
    coded.resize(depth);
    images.resize(depth * words);
    if (!old.carried.is_empty()) {
        carried.resize(depth * fields);
    }
    clear();
    floor_key = old.floor_key;
    if (old.held == 0) {
        return;
    }
    const int64_t span = int64_t(std::min(depth, old.depth));
    const int64_t lowest = std::max(int64_t(0), old.newest - span + 1);
    for (int64_t key = lowest; key <= old.newest; ++key) {
        const uint32_t from = old.at_of(key);
        if (old.keys[from] != key) {
            continue;
        }
        const uint32_t to = at_of(key);
        keys[to] = key;
        labels[to] = old.labels[from];
        fresh[to] = old.fresh[from];
        origins[to] = old.origins[from];
        relays[to] = old.relays[from];
        present[to] = old.present[from];
        coded[to] = old.coded[from];
        for (uint32_t word = 0; word < words; ++word) {
            images[to * words + word] = old.images[from * words + word];
        }
        for (uint32_t field = 0; field < fields && !old.carried.is_empty();
             ++field) {
            carried[to * fields + field] = old.carried[from * fields + field];
        }
        held += 1;
        newest = key;
    }
}

void TransitionBook::clear() {
    for (uint32_t at = 0; at < keys.size(); ++at) {
        keys[at] = -1;
        labels[at] = -1;
        fresh[at] = 0;
        origins[at] = 0;
        relays[at] = 0;
        present[at] = 0;
        coded[at] = 0;
    }
    for (uint32_t at = 0; at < carried.size(); ++at) {
        carried[at] = Variant();
    }
    floor_key = -1;
    newest = -1;
    held = 0;
}

bool TransitionBook::open_entry(
    const FieldCodec &p_codec,
    int64_t p_key,
    int64_t p_label,
    bool p_fresh,
    uint8_t p_origin
) {
    if (p_key < 0) {
        return false;
    }
    if (keys.is_empty()
        || fields != uint32_t(std::min(p_codec.count(), int(MASK_FIELDS)))) {
        reset(p_codec);
    }
    if (held > 0 && p_key <= newest - int64_t(depth)) {
        return false;
    }
    const uint32_t at = at_of(p_key);
    if (keys[at] < 0) {
        held += 1;
    }
    keys[at] = p_key;
    labels[at] = p_label;
    fresh[at] = p_fresh ? 1 : 0;
    origins[at] = p_origin;
    relays[at] = 0;
    present[at] = 0;
    coded[at] = 0;
    if (held == 1 || p_key > newest) {
        newest = p_key;
    }
    return true;
}

void TransitionBook::carry(uint32_t p_at, int p_field, const Variant &p_value) {
    if (carried.is_empty()) {
        carried.resize(depth * fields);
    }
    carried[p_at * fields + uint32_t(p_field)] = detached(p_value);
}

void TransitionBook::seal_entry(uint32_t p_at) {
    const uint64_t *source = scratch.word_data();
    for (uint32_t word = 0; word < words; ++word) {
        images[p_at * words + word] = source[word];
    }
}

bool TransitionBook::code_field(
    const FieldCodec &p_codec,
    int p_field,
    const Variant &p_value
) {
    return p_codec.holds(p_field, p_value)
        && wire::encode_scalar_column(
               *p_codec.schema.at(p_field),
               p_codec.plan.column(uint32_t(p_field)),
               uint32_t(p_field),
               p_value,
               scratch
        );
}

bool TransitionBook::record_row(
    const FieldCodec &p_codec,
    int64_t p_key,
    int64_t p_label,
    bool p_fresh,
    const StateRow &p_row,
    uint8_t p_origin
) {
    NETW_ZONE_LEAF(
        "Predict book record row",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    if (!open_entry(p_codec, p_key, p_label, p_fresh, p_origin)) {
        return false;
    }
    const uint32_t at = at_of(p_key);
    scratch.clear();
    const uint32_t span = std::min(fields, uint32_t(p_row.values.size()));
    for (uint32_t field = 0; field < span; ++field) {
        if (!p_row.has(int(field))) {
            continue;
        }
        const Variant &value = p_row.values[field];
        present[at] |= bit_of(int(field));
        if (code_field(p_codec, int(field), value)) {
            coded[at] |= bit_of(int(field));
        } else {
            carry(at, int(field), value);
        }
    }
    seal_entry(at);
    return true;
}

bool TransitionBook::holds(int64_t p_key) const {
    if (p_key < 0 || p_key < floor_key || keys.is_empty()) {
        return false;
    }
    return keys[at_of(p_key)] == p_key;
}

int64_t TransitionBook::newest_at_or_before(int64_t p_key) const {
    if (held == 0) {
        return -1;
    }
    const int64_t lowest = std::max(floor_key, newest - int64_t(depth) + 1);
    for (int64_t key = std::min(p_key, newest); key >= lowest && key >= 0;
         --key) {
        if (keys[at_of(key)] == key) {
            return key;
        }
    }
    return -1;
}

int64_t TransitionBook::newest_labeled_at_or_before(int64_t p_label) const {
    if (held == 0) {
        return -1;
    }
    const int64_t lowest = std::max(floor_key, newest - int64_t(depth) + 1);
    for (int64_t key = newest; key >= lowest && key >= 0; --key) {
        const uint32_t at = at_of(key);
        if (keys[at] == key && labels[at] <= p_label) {
            return key;
        }
    }
    return -1;
}

int64_t TransitionBook::oldest_key() const {
    if (held == 0) {
        return -1;
    }
    const int64_t lowest = std::max(floor_key, newest - int64_t(depth) + 1);
    int64_t out = -1;
    for (int64_t key = newest; key >= lowest && key >= 0; --key) {
        if (keys[at_of(key)] == key) {
            out = key;
        }
    }
    return out;
}

int64_t TransitionBook::label_of(int64_t p_key) const {
    return holds(p_key) ? labels[at_of(p_key)] : -1;
}

bool TransitionBook::fresh_at(int64_t p_key) const {
    return holds(p_key) && fresh[at_of(p_key)] != 0;
}

int TransitionBook::origin_of(int64_t p_key) const {
    return holds(p_key) ? int(origins[at_of(p_key)]) : -1;
}

void TransitionBook::note_relayed(int64_t p_key) {
    if (holds(p_key)) {
        relays[at_of(p_key)] = 1;
    }
}

void TransitionBook::forget_relays() {
    for (uint32_t at = 0; at < relays.size(); ++at) {
        relays[at] = 0;
    }
}

bool TransitionBook::relayed_at(int64_t p_key) const {
    return holds(p_key) && relays[at_of(p_key)] != 0;
}

bool TransitionBook::carries_fields(int64_t p_key) const {
    return holds(p_key) && present[at_of(p_key)] != 0;
}

const uint64_t *TransitionBook::image_of(int64_t p_key) const {
    if (!holds(p_key) || words == 0) {
        return nullptr;
    }
    return images.ptr() + at_of(p_key) * words;
}

bool TransitionBook::coded_whole(int64_t p_key) const {
    if (!holds(p_key)) {
        return false;
    }
    const uint32_t at = at_of(p_key);
    const uint64_t whole = mask_of(fields);
    return present[at] == whole && coded[at] == whole;
}

bool TransitionBook::field_value(
    const FieldCodec &p_codec,
    uint32_t p_at,
    int p_field,
    Variant &r_value
) const {
    if ((coded[p_at] & bit_of(p_field)) == 0) {
        r_value = carried.is_empty()
            ? Variant()
            : detached(carried[p_at * fields + uint32_t(p_field)]);
        return true;
    }
    return wire::decode_scalar_column(
        *p_codec.schema.at(p_field),
        p_codec.plan.column(uint32_t(p_field)),
        uint32_t(p_field),
        scratch,
        element_codes[uint32_t(p_field)],
        r_value
    );
}

bool TransitionBook::read_row(
    const FieldCodec &p_codec,
    int64_t p_key,
    StateRow &r_row
) const {
    NETW_ZONE_LEAF(
        "Predict book read",
        colors::PREDICTION,
        profile::SUBSYSTEM_PREDICTION
    );
    if (!holds(p_key)) {
        return false;
    }
    const uint32_t at = at_of(p_key);
    if (r_row.values.size() != fields) {
        r_row.resize(int(fields));
    }
    if (words > 0) {
        scratch.assign(p_codec.plan, images.ptr() + at * words, nullptr);
    }
    for (uint32_t field = 0; field < fields; ++field) {
        if ((present[at] & bit_of(int(field))) == 0) {
            r_row.values[field] = Variant();
            r_row.present[field] = 0;
            continue;
        }
        Variant value;
        if (!field_value(p_codec, at, int(field), value)) {
            r_row.values[field] = Variant();
            r_row.present[field] = 0;
            continue;
        }
        r_row.set(int(field), value);
    }
    return true;
}

int32_t TransitionBook::fingerprint(
    const FieldCodec &p_codec,
    int64_t p_key,
    uint64_t p_scope,
    bool &r_any
) const {
    r_any = false;
    if (!holds(p_key)) {
        return 0;
    }
    const uint32_t at = at_of(p_key);
    if (words > 0) {
        scratch.assign(p_codec.plan, images.ptr() + at * words, nullptr);
    }
    return fingerprint_codes(
        p_codec,
        scratch,
        present[at],
        coded[at],
        carried.is_empty() ? nullptr : carried.ptr() + at * fields,
        p_scope,
        r_any
    );
}

void TransitionBook::release(uint32_t p_at) {
    keys[p_at] = -1;
    labels[p_at] = -1;
    fresh[p_at] = 0;
    origins[p_at] = 0;
    relays[p_at] = 0;
    present[p_at] = 0;
    coded[p_at] = 0;
    held -= 1;
}

void TransitionBook::drop_origin(uint8_t p_origin, bool p_matching) {
    if (held == 0) {
        return;
    }
    int64_t kept = -1;
    for (uint32_t at = 0; at < keys.size(); ++at) {
        if (keys[at] < 0) {
            continue;
        }
        if ((origins[at] == p_origin) == p_matching) {
            release(at);
            continue;
        }
        kept = std::max(kept, keys[at]);
    }
    newest = held == 0 ? -1 : kept;
}

void TransitionBook::trim_before(int64_t p_key) {
    floor_key = std::max(floor_key, p_key);
}

int64_t TransitionBook::byte_count() const {
    return int64_t(keys.size()) * 8 + int64_t(labels.size()) * 8
        + int64_t(fresh.size()) + int64_t(origins.size())
        + int64_t(relays.size()) + int64_t(present.size()) * 8
        + int64_t(coded.size()) * 8 + int64_t(images.size()) * 8
        + int64_t(carried.size()) * int64_t(sizeof(Variant));
}

} // namespace netw::predict
