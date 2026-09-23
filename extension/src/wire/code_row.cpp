#include "netw/wire/code_row.hpp"

#include "godot/utility.hpp"

using namespace godot;

namespace netw::wire {

namespace {

uint64_t low_mask(int count) {
    return count >= 64 ? ~uint64_t(0) : ((uint64_t(1) << count) - 1);
}

uint64_t read_run(
    const LocalVector<uint64_t> &words,
    int64_t offset,
    int width
) {
    if (width <= 0) {
        return 0;
    }
    const int64_t word = offset >> 6;
    const int within = int(offset & 63);
    uint64_t value = words[word] >> within;
    const int taken = 64 - within;
    if (taken < width) {
        value |= words[word + 1] << taken;
    }
    return value & low_mask(width);
}

void write_run(
    LocalVector<uint64_t> &words,
    int64_t offset,
    int width,
    uint64_t value
) {
    if (width <= 0) {
        return;
    }
    const int64_t word = offset >> 6;
    const int within = int(offset & 63);
    const uint64_t mask = low_mask(width);
    words[word] &= ~(mask << within);
    words[word] |= (value & mask) << within;
    const int taken = 64 - within;
    if (taken < width) {
        words[word + 1] &= ~(mask >> taken);
        words[word + 1] |= (value & mask) >> taken;
    }
}

bool range_fits(int64_t capacity, int64_t offset, int width) {
    return offset >= 0 && width >= 0 && width <= 64
        && offset <= capacity - width;
}

bool runs_differ(
    const LocalVector<uint64_t> &before,
    const LocalVector<uint64_t> &after,
    int64_t offset,
    int64_t width
) {
    int64_t compared = 0;
    while (compared < width) {
        const int taking = int(width - compared < 64 ? width - compared : 64);
        if (read_run(before, offset + compared, taking)
            != read_run(after, offset + compared, taking)) {
            return true;
        }
        compared += taking;
    }
    return false;
}

int64_t fixed_byte_count(int64_t p_bits) {
    return (p_bits + 7) / 8;
}

uint32_t value_slots(const WirePlan &plan) {
    return plan.has_variable() ? plan.column_count() : 0;
}

} // namespace

CodeRow CodeRow::for_plan(const WirePlan &plan) {
    CodeRow row;
    if (!plan.valid()) {
        return row;
    }
    row.used_bits = plan.row_bits();
    const int64_t needed = (plan.row_bits() + 63) / 64 + 1;
    row.words.resize(uint32_t(needed));
    for (uint32_t index = 0; index < row.words.size(); ++index) {
        row.words[index] = 0;
    }
    row.values.resize(value_slots(plan));
    return row;
}

CodeRow CodeRow::from_bytes(
    const WirePlan &plan,
    const PackedByteArray &bytes
) {
    CodeRow row = for_plan(plan);
    const int64_t expected = fixed_byte_count(plan.row_bits());
    if (row.is_empty() || bytes.size() < expected
        || (!plan.has_variable() && bytes.size() != expected)) {
        return CodeRow();
    }
    const int tail = int(plan.row_bits() & 7);
    if (tail != 0 && expected > 0
        && (bytes[expected - 1] & uint8_t(0xffU << tail)) != 0) {
        return CodeRow();
    }
    for (int64_t at = 0; at < expected; ++at) {
        row.words[uint32_t(at >> 3)] |= uint64_t(bytes[at])
            << int((at & 7) * 8);
    }
    if (!plan.has_variable()) {
        return row;
    }
    const Array carried = gd::bytes_to_var(bytes.slice(expected));
    if (carried.size() != int64_t(row.values.size())) {
        return CodeRow();
    }
    for (uint32_t at = 0; at < row.values.size(); ++at) {
        row.values[at] = carried[at];
    }
    return row;
}

bool CodeRow::valid_for(const WirePlan &plan) const {
    return plan.valid() && used_bits == plan.row_bits()
        && words.size() == uint32_t((used_bits + 63) / 64 + 1)
        && values.size() == value_slots(plan);
}

PackedByteArray CodeRow::to_bytes() const {
    PackedByteArray out;
    if (words.is_empty()) {
        return out;
    }
    const int64_t count = fixed_byte_count(used_bits);
    out.resize(count);
    for (int64_t at = 0; at < count; ++at) {
        out.set(
            at,
            uint8_t((words[uint32_t(at >> 3)] >> int((at & 7) * 8)) & 0xff)
        );
    }
    if (values.is_empty()) {
        return out;
    }
    Array carried;
    for (uint32_t at = 0; at < values.size(); ++at) {
        carried.push_back(values[at]);
    }
    out.append_array(gd::var_to_bytes(carried));
    return out;
}

void CodeRow::clear() {
    for (uint32_t index = 0; index < words.size(); ++index) {
        words[index] = 0;
    }
    for (uint32_t index = 0; index < values.size(); ++index) {
        values[index] = Variant();
    }
}

void CodeRow::copy_from(const CodeRow &other) {
    if (words.size() != other.words.size()) {
        words.resize(other.words.size());
    }
    for (uint32_t index = 0; index < words.size(); ++index) {
        words[index] = other.words[index];
    }
    if (values.size() != other.values.size()) {
        values.resize(other.values.size());
    }
    for (uint32_t index = 0; index < values.size(); ++index) {
        values[index] = other.values[index];
    }
    used_bits = other.used_bits;
}

bool CodeRow::write(const ColumnPlan &slot, int element, uint64_t code) {
    if (element < 0 || element >= slot.stride || slot.width > 64) {
        return false;
    }
    if ((code & ~low_mask(slot.width)) != 0) {
        return false;
    }
    return write_bits(
        slot.offset + int64_t(element) * slot.width,
        slot.width,
        code
    );
}

uint64_t CodeRow::read(const ColumnPlan &slot, int element) const {
    if (element < 0 || element >= slot.stride || slot.width > 64) {
        return 0;
    }
    return read_bits(slot.offset + int64_t(element) * slot.width, slot.width);
}

bool CodeRow::write_bits(int64_t offset, int width, uint64_t code) {
    if (!range_fits(used_bits, offset, width)
        || (code & ~low_mask(width)) != 0) {
        return false;
    }
    write_run(words, offset, width, code);
    return true;
}

uint64_t CodeRow::read_bits(int64_t offset, int width) const {
    if (!range_fits(used_bits, offset, width)) {
        return 0;
    }
    return read_run(words, offset, width);
}

bool CodeRow::write_value(uint32_t column, const Variant &value) {
    if (column >= values.size()) {
        return false;
    }
    values[column] = value.duplicate(true);
    return true;
}

Variant CodeRow::read_value(uint32_t column) const {
    return column < values.size() ? values[column] : Variant();
}

uint64_t CodeRow::changed_mask(
    const WirePlan &plan,
    const CodeRow &before,
    const CodeRow &after
) {
    if (!before.valid_for(plan) || !after.valid_for(plan)) {
        return plan.full_mask();
    }
    uint64_t mask = 0;
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        const ColumnPlan &slot = plan.column(index);
        if (slot.variable) {
            if (before.values[index] != after.values[index]) {
                mask |= uint64_t(1) << index;
            }
            continue;
        }
        for (int element = 0; element < slot.stride; ++element) {
            const int64_t at = slot.offset + int64_t(element) * slot.width;
            if (runs_differ(before.words, after.words, at, slot.width)) {
                mask |= uint64_t(1) << index;
                break;
            }
        }
    }
    return mask;
}

bool CodeRow::equals(const CodeRow &other) const {
    if (used_bits != other.used_bits || words.size() != other.words.size()
        || values.size() != other.values.size()) {
        return false;
    }
    for (uint32_t index = 0; index < words.size(); ++index) {
        if (words[index] != other.words[index]) {
            return false;
        }
    }
    for (uint32_t index = 0; index < values.size(); ++index) {
        if (values[index] != other.values[index]) {
            return false;
        }
    }
    return true;
}

} // namespace netw::wire
