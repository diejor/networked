#pragma once

#include <cstdint>

#include "godot/local_vector.hpp"
#include "netw/held_bytes.hpp"

namespace netw {

template <typename T>
class SlotRows {
    struct Row {
        uint64_t id = 0;
        T value;
    };

    godot::LocalVector<Row> rows;
    uint32_t held = 0;

    static uint32_t index_of(uint64_t p_id) {
        return uint32_t(p_id & 0xffffffffu);
    }

public:
    T *find(uint64_t p_id) {
        const uint32_t at = index_of(p_id);
        if (p_id == 0 || at >= rows.size() || rows[at].id != p_id) {
            return nullptr;
        }
        return &rows[at].value;
    }

    const T *find(uint64_t p_id) const {
        const uint32_t at = index_of(p_id);
        if (p_id == 0 || at >= rows.size() || rows[at].id != p_id) {
            return nullptr;
        }
        return &rows[at].value;
    }

    bool has(uint64_t p_id) const {
        return find(p_id) != nullptr;
    }

    T &ensure(uint64_t p_id) {
        const uint32_t at = index_of(p_id);
        if (at >= rows.size()) {
            rows.resize(at + 1);
        }
        Row &row = rows[at];
        if (row.id != p_id) {
            if (row.id == 0) {
                held += 1;
            }
            row.id = p_id;
            row.value = T();
        }
        return row.value;
    }

    bool erase(uint64_t p_id) {
        if (find(p_id) == nullptr) {
            return false;
        }
        Row &row = rows[index_of(p_id)];
        row.id = 0;
        row.value = T();
        held -= 1;
        return true;
    }

    uint32_t size() const {
        return held;
    }

    bool is_empty() const {
        return held == 0;
    }

    void clear() {
        rows.clear();
        held = 0;
    }

    template <typename F>
    void each(F &&p_visit) {
        for (uint32_t at = 0; at < rows.size(); ++at) {
            if (rows[at].id != 0) {
                p_visit(rows[at].id, rows[at].value);
            }
        }
    }

    template <typename F>
    void each(F &&p_visit) const {
        for (uint32_t at = 0; at < rows.size(); ++at) {
            if (rows[at].id != 0) {
                p_visit(rows[at].id, rows[at].value);
            }
        }
    }

    template <typename F>
    int64_t held_bytes(F p_each) const {
        int64_t bytes = netw::held::vector(rows);
        each([&](uint64_t, const T &p_value) { bytes += p_each(p_value); });
        return bytes;
    }
};

} // namespace netw
