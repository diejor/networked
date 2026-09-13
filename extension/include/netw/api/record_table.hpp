#pragma once

#include "godot/callable.hpp"
#include "godot/ref_counted.hpp"
#include "godot/script.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/record.hpp"

namespace netw {

class NetwDatabase;
class NetwTransaction;

class NetwRecordTable : public godot::RefCounted {
    GDCLASS(NetwRecordTable, godot::RefCounted)

private:
    godot::Ref<NetwDatabase> db;
    godot::StringName table;
    godot::Ref<godot::Script> record_script;

    godot::Ref<NetwRecord> mint() const;

    void settle_fetched(
        const godot::Dictionary &record,
        const godot::Ref<NetwPromise> &answer
    );
    void settle_missing(
        int code,
        const godot::String &detail,
        const godot::Ref<NetwPromise> &answer
    );
    void settle_fetched_all(
        const godot::Array &rows,
        const godot::Ref<NetwPromise> &answer
    );
    void settle_none(
        int code,
        const godot::String &detail,
        const godot::Ref<NetwPromise> &answer
    );
    void queue_put(
        const godot::Ref<NetwTransaction> &transaction,
        const godot::StringName &id,
        const godot::Ref<NetwRecord> &record
    );

protected:
    static void _bind_methods();

public:
    static godot::Ref<NetwRecordTable> open(
        const godot::Ref<NetwDatabase> &database,
        const godot::StringName &table_name,
        const godot::Ref<godot::Script> &script
    );

    godot::StringName get_table_name() const {
        return table;
    }
    godot::TypedArray<godot::StringName> get_columns() const;

    godot::Ref<NetwPromise> fetch(const godot::StringName &id);
    godot::Ref<NetwPromise> fetch_all(
        const godot::Dictionary &filter = godot::Dictionary()
    );
    godot::Ref<NetwPromise> put(
        const godot::StringName &id,
        const godot::Ref<NetwRecord> &record
    );
    godot::Ref<NetwPromise> erase(const godot::StringName &id);
};

} // namespace netw
