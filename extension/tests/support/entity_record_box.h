#pragma once

#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"

namespace netw_test {

struct RecordBox {
    netw::NetwEntityRecord *row = memnew(netw::NetwEntityRecord);

    RecordBox() = default;
    RecordBox(const RecordBox &) = delete;
    RecordBox &operator=(const RecordBox &) = delete;

    ~RecordBox() {
        unref();
    }

    void unref() {
        if (row != nullptr) {
            godot::memdelete(row);
            row = nullptr;
        }
    }

    netw::NetwEntityRecord *operator->() const {
        return row;
    }

    operator netw::NetwEntityRecord *() const {
        return row;
    }
};

} // namespace netw_test
