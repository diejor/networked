#include "netw/snapshot_book.hpp"

using namespace godot;

namespace netw {

int SnapshotBook::index_of(const StringName &property) const {
    for (uint32_t index = 0; index < columns.size(); ++index) {
        if (columns[index].property == property) {
            return int(index);
        }
    }
    return -1;
}

void SnapshotBook::set_default_interval(double value) {
    default_interval = value;
}

double SnapshotBook::get_default_interval() const {
    return default_interval;
}

void SnapshotBook::declare(
    const StringName &property,
    double interval
) {
    const int found = index_of(property);
    if (found >= 0) {
        columns[uint32_t(found)].interval = interval;
        return;
    }
    Column column;
    column.property = property;
    column.interval = interval;
    columns.push_back(column);
}

bool SnapshotBook::is_empty() const {
    return columns.is_empty();
}

Array SnapshotBook::properties() const {
    Array out;
    for (const Column &column : columns) {
        out.push_back(column.property);
    }
    return out;
}

Array SnapshotBook::advance(double delta) {
    Array due;
    for (Column &column : columns) {
        column.accum += delta;
        const double interval
            = column.interval > 0.0 ? column.interval : default_interval;
        if (column.accum >= interval) {
            column.accum = 0.0;
            due.push_back(column.property);
        }
    }
    return due;
}

Dictionary SnapshotBook::changed(const Dictionary &current) const {
    Dictionary out;
    const Array keys = current.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        const Variant value = current[key];
        const Variant written
            = last_flushed.has(key) ? last_flushed[key] : Variant();
        if (written == value) {
            continue;
        }
        out[key] = value;
    }
    return out;
}

bool SnapshotBook::differs(const Dictionary &current) const {
    return current != last_flushed;
}

void SnapshotBook::commit(const Dictionary &values) {
    const Array keys = values.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        last_flushed[key] = values[key];
    }
}

void SnapshotBook::adopt(const Dictionary &values) {
    last_flushed = values.duplicate();
}

} // namespace netw
