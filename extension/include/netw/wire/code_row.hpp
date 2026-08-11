#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/wire/plan.hpp"

namespace netw::wire {

// One replicated row held as the codes that will cross the wire, packed to the
// widths its plan declares.
//
// Quantization happens once, where the value is gathered, and everything below
// that reads integers. What this buys is not compactness, it is that comparing
// two rows stops being a walk over typed values and becomes arithmetic on
// words: a whole-row XOR finds every column that moved in a handful of
// instructions, and the sender's model of what a receiver holds is exact
// instead of off by up to one grid step.
//
// A row addresses a column through its plan rather than storing offsets of its
// own, so a row is only meaningful beside the plan it was built for and two
// rows of one plan are always comparable.
class CodeRow {
    godot::LocalVector<uint64_t> words;
    int64_t used_bits = 0;

public:
    // A zeroed row wide enough for every column the plan declares. An invalid
    // plan yields an empty row, which reads and writes nothing.
    static CodeRow for_plan(const WirePlan &plan);
    static CodeRow from_bytes(
        const WirePlan &plan,
        const godot::PackedByteArray &bytes
    );

    uint32_t word_count() const {
        return words.size();
    }

    bool is_empty() const {
        return words.is_empty();
    }

    bool valid_for(const WirePlan &plan) const;
    godot::PackedByteArray to_bytes() const;

    void clear();

    // Takes `other`'s codes, which is what advancing a baseline is: the row a
    // peer is known to hold moves by copying words rather than by being
    // rebuilt from the values that produced it.
    void copy_from(const CodeRow &other);

    // Writes one element of a column. A code wider than the column's width, or
    // an element past its stride, is refused rather than allowed to run into
    // the neighbouring column's bits.
    bool write(const ColumnPlan &slot, int element, uint64_t code);
    uint64_t read(const ColumnPlan &slot, int element) const;

    bool write_bits(int64_t offset, int width, uint64_t code);
    uint64_t read_bits(int64_t offset, int width) const;

    // Columns whose codes differ, one bit per column in plan order.
    //
    // A column is a run of bits rather than a word, so this compares the runs
    // a column owns rather than XOR-ing whole words and attributing the
    // difference: two columns can share a word, and a mask built from word
    // differences would report both when one moved.
    static uint64_t changed_mask(
        const WirePlan &plan,
        const CodeRow &before,
        const CodeRow &after
    );

    bool equals(const CodeRow &other) const;
};

} // namespace netw::wire
