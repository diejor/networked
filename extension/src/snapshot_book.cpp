#include "netw/snapshot_book.hpp"

#include "godot/class_db.hpp"

using namespace godot;

namespace netw {

int NetwSnapshotBook::index_of(const StringName &property) const {
    for (uint32_t index = 0; index < columns.size(); ++index) {
        if (columns[index].property == property) {
            return int(index);
        }
    }
    return -1;
}

void NetwSnapshotBook::set_default_interval(double value) {
    default_interval = value;
}

double NetwSnapshotBook::get_default_interval() const {
    return default_interval;
}

void NetwSnapshotBook::declare(
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

bool NetwSnapshotBook::is_empty() const {
    return columns.is_empty();
}

Array NetwSnapshotBook::properties() const {
    Array out;
    for (const Column &column : columns) {
        out.push_back(column.property);
    }
    return out;
}

Array NetwSnapshotBook::advance(double delta) {
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

Dictionary NetwSnapshotBook::changed(const Dictionary &current) const {
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

bool NetwSnapshotBook::differs(const Dictionary &current) const {
    return current != last_flushed;
}

void NetwSnapshotBook::commit(const Dictionary &values) {
    const Array keys = values.keys();
    for (int index = 0; index < keys.size(); ++index) {
        const Variant key = keys[index];
        last_flushed[key] = values[key];
    }
}

void NetwSnapshotBook::adopt(const Dictionary &values) {
    last_flushed = values.duplicate();
}

void NetwSnapshotBook::_bind_methods() {
    ClassDB::bind_method(
        D_METHOD("set_default_interval", "value"),
        &NetwSnapshotBook::set_default_interval
    );
    ClassDB::bind_method(
        D_METHOD("get_default_interval"),
        &NetwSnapshotBook::get_default_interval
    );
    ADD_PROPERTY(
        PropertyInfo(Variant::FLOAT, "default_interval"),
        "set_default_interval",
        "get_default_interval"
    );

    ClassDB::bind_method(
        D_METHOD("declare", "property", "interval"),
        &NetwSnapshotBook::declare
    );
    ClassDB::bind_method(D_METHOD("is_empty"), &NetwSnapshotBook::is_empty);
    ClassDB::bind_method(
        D_METHOD("properties"),
        &NetwSnapshotBook::properties
    );
    ClassDB::bind_method(
        D_METHOD("advance", "delta"),
        &NetwSnapshotBook::advance
    );
    ClassDB::bind_method(
        D_METHOD("changed", "current"),
        &NetwSnapshotBook::changed
    );
    ClassDB::bind_method(
        D_METHOD("differs", "current"),
        &NetwSnapshotBook::differs
    );
    ClassDB::bind_method(
        D_METHOD("commit", "values"),
        &NetwSnapshotBook::commit
    );
    ClassDB::bind_method(D_METHOD("adopt", "values"), &NetwSnapshotBook::adopt);
}

} // namespace netw
