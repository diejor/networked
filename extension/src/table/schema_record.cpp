#include "netw/table/schema_record.hpp"

namespace netw::table {

const SchemaColumn *SchemaRecord::at(int column) const {
    if (column < 0 || column >= int(columns.size())) {
        return nullptr;
    }
    return &columns[column];
}

SchemaColumn *SchemaRecord::at(int column) {
    if (column < 0 || column >= int(columns.size())) {
        return nullptr;
    }
    return &columns[column];
}

int SchemaRecord::column_count() const {
    return int(columns.size());
}

} // namespace netw::table
