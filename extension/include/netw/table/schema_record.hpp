#pragma once

#include "godot/local_vector.hpp"
#include "godot/variant.hpp"
#include "netw/api/quantize.hpp"

namespace netw {
class SchemaCore;
} // namespace netw

namespace netw::table {

enum class DeltaMode {
    FULL,
    LADDER,
};

struct SchemaColumn {
    godot::StringName key;
    int type = 15;
    int stride = 1;
    DeltaMode delta = DeltaMode::FULL;
    godot::Ref<NetwQuantize> quantizer;
};

class SchemaRecord {
    friend class netw::SchemaCore;

    int redeclare_at = 0;
    bool redeclare_open = false;
    bool redeclare_failed = false;

public:
    godot::StringName name;
    godot::LocalVector<SchemaColumn> columns;
    bool sealed = false;
    int shape_hash = 0;

    const SchemaColumn *at(int column) const;
    SchemaColumn *at(int column);
    int column_count() const;
};

} // namespace netw::table
