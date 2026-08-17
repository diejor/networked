#include "netw/wire/code_row.hpp"

using namespace godot;

namespace netw::wire {

namespace {

uint64_t low_mask(int count) {
    return count >= 64 ? ~uint64_t(0) : ((uint64_t(1) << count) - 1);
}

// A run of bits can straddle a word boundary, so both halves are handled and
// the second is a no-op whenever the run fits where it started.
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

} // namespace

CodeRow CodeRow::for_plan(const WirePlan &plan) {
    CodeRow row;
    if (!plan.valid()) {
        return row;
    }
    row.used_bits = plan.row_bits();
    // One spare word so a run ending in the final word never addresses past
    // the buffer when it checks whether it straddles.
    const int64_t needed = (plan.row_bits() + 63) / 64 + 1;
    row.words.resize(uint32_t(needed));
    for (uint32_t index = 0; index < row.words.size(); ++index) {
        row.words[index] = 0;
    }
    return row;
}

CodeRow CodeRow::from_bytes(
    const WirePlan &plan,
    const PackedByteArray &bytes
) {
    CodeRow row = for_plan(plan);
    const int64_t expected = (plan.row_bits() + 7) / 8;
    if (row.is_empty() || bytes.size() != expected) {
        return CodeRow();
    }
    const int tail = int(plan.row_bits() & 7);
    if (tail != 0 && bytes.size() > 0
        && (bytes[bytes.size() - 1] & uint8_t(0xffU << tail)) != 0) {
        return CodeRow();
    }
    for (int64_t at = 0; at < bytes.size(); ++at) {
        row.words[uint32_t(at >> 3)] |= uint64_t(bytes[at])
            << int((at & 7) * 8);
    }
    return row;
}

bool CodeRow::valid_for(const WirePlan &plan) const {
    return plan.valid() && used_bits == plan.row_bits()
        && words.size() == uint32_t((used_bits + 63) / 64 + 1);
}

PackedByteArray CodeRow::to_bytes() const {
    PackedByteArray out;
    if (words.is_empty()) {
        return out;
    }
    const int64_t count = (used_bits + 7) / 8;
    out.resize(count);
    for (int64_t at = 0; at < count; ++at) {
        out.set(
            at,
            uint8_t((words[uint32_t(at >> 3)] >> int((at & 7) * 8)) & 0xff)
        );
    }
    return out;
}

void CodeRow::clear() {
    for (uint32_t index = 0; index < words.size(); ++index) {
        words[index] = 0;
    }
}

void CodeRow::copy_from(const CodeRow &other) {
    if (words.size() != other.words.size()) {
        words.resize(other.words.size());
    }
    for (uint32_t index = 0; index < words.size(); ++index) {
        words[index] = other.words[index];
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

uint64_t CodeRow::changed_mask(
    const WirePlan &plan,
    const CodeRow &before,
    const CodeRow &after
) {
    // Every column rather than none. Zero reads as a caught-up peer and costs
    // the pass nothing, so a row this plan cannot interpret would strand the
    // receiver silently; the whole mask is what the baseline book already
    // answers for a peer whose baseline it does not hold.
    if (!before.valid_for(plan) || !after.valid_for(plan)) {
        return plan.full_mask();
    }
    uint64_t mask = 0;
    for (uint32_t index = 0; index < plan.column_count(); ++index) {
        const ColumnPlan &slot = plan.column(index);
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
    if (used_bits != other.used_bits || words.size() != other.words.size()) {
        return false;
    }
    for (uint32_t index = 0; index < words.size(); ++index) {
        if (words[index] != other.words[index]) {
            return false;
        }
    }
    return true;
}

} // namespace netw::wire
