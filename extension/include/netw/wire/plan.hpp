#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "godot/ref_counted.hpp"
#include "netw/api/schema_core.hpp"

namespace netw::wire {

enum class DeltaMode {
    FULL,
    LADDER,
};

struct ColumnPlan {
    int width = 0;
    int stride = 1;
    DeltaMode delta = DeltaMode::FULL;
    int64_t offset = 0;

    int64_t bits() const {
        return int64_t(width) * int64_t(stride);
    }
};

class WirePlan {
    godot::LocalVector<ColumnPlan> columns;
    int64_t total_bits = 0;
    bool plannable = false;

public:
    static constexpr uint32_t MAX_COLUMNS = 64;

    static WirePlan compile(const godot::Ref<SchemaRecord> &record);

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

    int64_t row_bits() const {
        return total_bits;
    }

    uint32_t mask_width() const {
        return columns.size();
    }

    uint64_t full_mask() const {
        return columns.size() >= 64 ? ~uint64_t(0)
                                    : ((uint64_t(1) << columns.size()) - 1);
    }
};

} // namespace netw::wire
