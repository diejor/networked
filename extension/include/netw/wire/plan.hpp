#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "netw/table/schema_core.hpp"

namespace netw::wire {

// How a column's value is carried once a baseline exists.
//
// FULL writes the code itself, which is what a peer with no baseline can read
// and therefore what every gain edge sends. LADDER writes the signed step from
// the baseline's code, which costs a few bits for a value that moved a little
// and more than FULL for one that jumped.
//
// A decoder has to know both to read a spec, so both are named here. Which one
// a column gets is a measurement, not a preference, and nothing selects LADDER
// yet: `compile` gives every column FULL.
enum class DeltaMode {
    FULL,
    LADDER,
};

// One column's slot in a compiled row.
//
// `width` is the bits ONE element occupies. A column of stride N occupies
// `width * stride`, because a strided column is N elements of one shape rather
// than one wider value.
struct ColumnPlan {
    int width = 0;
    int stride = 1;
    DeltaMode delta = DeltaMode::FULL;
    // First bit of this column inside a row, so addressing a column is
    // arithmetic rather than a walk over the columns before it.
    int64_t offset = 0;

    int64_t bits() const {
        return int64_t(width) * int64_t(stride);
    }
};

// A sealed schema, compiled once into the fixed-width program a stream runs.
//
// The point of compiling is that a hot loop reads widths out of a flat array
// instead of asking each column's quantizer what it would do. A plan is derived
// wholly from the sealed record, so two peers that agree on the record agree on
// the plan without exchanging it.
//
// A plan is FIXED WIDTH by construction, so a schema carrying a
// `SchemaCore::VARIANT` column has none: a self-describing element has no one
// width, and `valid()` answers false rather than inventing a size for it.
//
// A plan carries at most `MAX_COLUMNS` columns, because a change mask is one
// bit per column in a single word. A schema wider than that has no plan and is
// refused, rather than being planned and then silently masked short.
class WirePlan {
    godot::LocalVector<ColumnPlan> columns;
    int64_t total_bits = 0;
    bool plannable = false;

public:
    static constexpr uint32_t MAX_COLUMNS = 64;

    // Derives the plan of a sealed record. An unsealed record, one carrying a
    // self-describing column, or one wider than MAX_COLUMNS compiles to an
    // invalid plan.
    static WirePlan compile(const godot::Ref<SchemaRecord> &record);

    // The bits one element of a column type occupies with no quantizer, or
    // zero for a type that has no fixed width.
    static int element_width(int column_type);

    bool valid() const {
        return plannable;
    }

    uint32_t column_count() const {
        return columns.size();
    }

    const ColumnPlan &column(uint32_t index) const {
        return columns[index];
    }

    // Every column's bits, which is what a full row costs and what a caller
    // prices a frame against without building one.
    int64_t row_bits() const {
        return total_bits;
    }

    // Bits a per-column change mask spends, one per column.
    uint32_t mask_width() const {
        return columns.size();
    }

    // The mask naming every column, which is what a whole row's change mask
    // is. A caller counting how often a send fell back to a full row compares
    // against this rather than shifting by the column count, which at
    // MAX_COLUMNS would shift a 64-bit word by 64.
    uint64_t full_mask() const {
        return columns.size() >= 64 ? ~uint64_t(0)
                                    : ((uint64_t(1) << columns.size()) - 1);
    }
};

} // namespace netw::wire
