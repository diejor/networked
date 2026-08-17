#include "netw/settle_queue.hpp"

using namespace godot;

namespace netw {

void SettleQueue::schedule(const Callable &fn, const StringName &key) {
    if (!key.is_empty()) {
        cancel(key);
    }
    Row row;
    row.key = key;
    row.fn = fn;
    rows.push_back(row);
}

void SettleQueue::schedule_after(
    const Callable &fn,
    const StringName &key,
    int p_pumps
) {
    schedule(fn, key);
    rows[rows.size() - 1].pumps = p_pumps > 0 ? p_pumps : 0;
}

void SettleQueue::advance_windows() {
    for (Row &row : rows) {
        if (row.pumps > 0) {
            row.pumps -= 1;
        }
    }
}

bool SettleQueue::has_due() const {
    for (const Row &row : rows) {
        if (row.pumps <= 0) {
            return true;
        }
    }
    return false;
}

void SettleQueue::cancel(const StringName &key) {
    if (key.is_empty()) {
        return;
    }
    for (int index = int(rows.size()) - 1; index >= 0; index--) {
        if (rows[index].key == key) {
            rows.remove_at(index);
        }
    }
}

bool SettleQueue::is_empty() const {
    return rows.is_empty();
}

int SettleQueue::size() const {
    return int(rows.size());
}

bool SettleQueue::has(const StringName &key) const {
    for (const Row &row : rows) {
        if (row.key == key) {
            return true;
        }
    }
    return false;
}

PackedStringArray SettleQueue::drain() {
    int passes = 0;
    // The batch and the held rows are copied out rather than moved, because the
    // two tiers disagree about which assignment a LocalVector offers.
    LocalVector<Row> batch;
    LocalVector<Row> held;
    while (has_due()) {
        batch.clear();
        held.clear();
        for (const Row &row : rows) {
            if (row.pumps > 0) {
                held.push_back(row);
            } else {
                batch.push_back(row);
            }
        }
        if (passes >= MAX_PASSES) {
            PackedStringArray pending;
            for (const Row &row : batch) {
                pending.push_back(
                    row.key.is_empty() ? String("<unkeyed>") : String(row.key)
                );
            }
            rows.clear();
            for (const Row &row : held) {
                rows.push_back(row);
            }
            return pending;
        }
        passes++;
        rows.clear();
        for (const Row &row : held) {
            rows.push_back(row);
        }
        for (const Row &row : batch) {
            if (row.fn.is_valid()) {
                row.fn.call();
            }
        }
    }
    return PackedStringArray();
}

void SettleQueue::clear() {
    rows.clear();
}

} // namespace netw
