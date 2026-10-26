#include "netw/persist/binding.hpp"

#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/game_call.hpp"
#include "netw/log.hpp"
#include "netw/object_port.hpp"
#include "netw/schema_core.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw::persist {

namespace {

const GameCall MIGRATION_STEP = {
    sys::TABLE,
    "The NetwSchema.migrate or NetwPersistenceConfig.migrate step",
    "func(values: Dictionary) -> Dictionary",
    1
};

const GameCall RECORD_ID = {
    sys::TABLE,
    "The NetwPersistenceConfig.record_id provider",
    "func() -> StringName",
    0
};

Node *node_of(ObjectID p_id) {
    return Object::cast_to<Node>(gd::object_of(p_id));
}

bool roots_other_entity(Node *p_node, Node *p_root) {
    if (p_node == p_root) {
        return false;
    }
    const Ref<NetwEntity> entity = NetwEntity::resolve(p_node);
    return entity.is_valid() && entity->get_owner() == p_node;
}

void collect_nodes(Node *p_root, LocalVector<Node *> &r_nodes) {
    LocalVector<Node *> stack;
    stack.push_back(p_root);
    while (!stack.is_empty()) {
        Node *node = stack[stack.size() - 1];
        stack.remove_at(stack.size() - 1);
        r_nodes.push_back(node);
        for (int at = node->get_child_count() - 1; at >= 0; --at) {
            Node *child = node->get_child(at);
            if (!roots_other_entity(child, p_root)) {
                stack.push_back(child);
            }
        }
    }
}

String name_of(Node *p_node) {
    return p_node != nullptr ? String(p_node->get_name())
                             : String("a freed node");
}

bool rows_agree(const Dictionary &p_left, const Dictionary &p_right) {
    if (p_left.size() != p_right.size()) {
        return false;
    }
    const Array keys = p_left.keys();
    for (int at = 0; at < keys.size(); ++at) {
        if (!p_right.has(keys[at])) {
            return false;
        }
        const Variant left = p_left[keys[at]];
        const Variant right = p_right[keys[at]];
        if (left.get_type() != right.get_type() || bool(left != right)) {
            return false;
        }
    }
    return true;
}

int saved_type_of(Node *p_node, const StringName &p_property) {
    const int64_t type
        = script::model::get_node_property_type(p_node, p_property);
    switch (type) {
        case Variant::BOOL:
            return SchemaCore::BOOL;
        case Variant::INT:
            return SchemaCore::I64;
        case Variant::FLOAT:
            return SchemaCore::F64;
        case Variant::VECTOR2:
            return SchemaCore::VECTOR2;
        case Variant::VECTOR3:
            return SchemaCore::VECTOR3;
        case Variant::VECTOR4:
            return SchemaCore::VECTOR4;
        case Variant::COLOR:
            return SchemaCore::COLOR;
        case Variant::QUATERNION:
            return SchemaCore::QUATERNION;
        case Variant::STRING:
        case Variant::STRING_NAME:
            return SchemaCore::STRING;
        default:
            return SchemaCore::VARIANT;
    }
}

void order_by_key(
    LocalVector<BoundColumn> &r_bound,
    LocalVector<int> &r_types
) {
    for (uint32_t at = 1; at < r_bound.size(); ++at) {
        for (uint32_t back = at; back > 0; --back) {
            if (String(r_bound[back - 1].key) <= String(r_bound[back].key)) {
                break;
            }
            const BoundColumn column = r_bound[back];
            r_bound[back] = r_bound[back - 1];
            r_bound[back - 1] = column;
            const int type = r_types[back];
            r_types[back] = r_types[back - 1];
            r_types[back - 1] = type;
        }
    }
}

bool schema_matches(
    NetwMultiplayer *p_session,
    const RID &p_schema,
    const LocalVector<BoundColumn> &p_bound,
    const LocalVector<int> &p_types,
    int p_version
) {
    SchemaCore *core = p_session->get_schema_core();
    if (core->column_count(p_schema) != int(p_bound.size())
        || core->storage_version_of(p_schema) != p_version) {
        return false;
    }
    for (uint32_t at = 0; at < p_bound.size(); ++at) {
        const int column = core->find_column(p_schema, p_bound[at].key);
        if (column < 0 || core->column_type(p_schema, column) != p_types[at]
            || core->column_stride(p_schema, column) != 1) {
            return false;
        }
    }
    return true;
}

RID durable_schema(
    NetwMultiplayer *p_session,
    const StringName &p_name,
    const LocalVector<BoundColumn> &p_bound,
    const LocalVector<int> &p_types,
    const Ref<NetwPersistenceConfig> &p_config,
    String &r_detail
) {
    const int version = p_config->get_storage_version();
    const RID existing = p_session->schema_find(p_name);
    if (existing.is_valid()) {
        if (!schema_matches(p_session, existing, p_bound, p_types, version)) {
            r_detail = "Another entity already declared it with other "
                       "columns, types or storage version. Declare the same "
                       "saved properties on both.";
            return RID();
        }
        return existing;
    }
    const RID schema = p_session->schema_create(p_name);
    for (uint32_t at = 0; at < p_bound.size(); ++at) {
        p_session->schema_add_column(
            schema,
            p_bound[at].key,
            NetwMultiplayer::ColumnType(p_types[at]),
            1
        );
    }
    SchemaCore *core = p_session->get_schema_core();
    core->set_storage_version(schema, version);
    const Dictionary steps = p_config->get_migrations();
    const Array froms = steps.keys();
    for (int at = 0; at < froms.size(); ++at) {
        const Callable step = steps[froms[at]];
        game_call_declared(step, MIGRATION_STEP);
        core->add_migration(schema, int(froms[at]), step);
    }
    if (p_session->schema_seal(schema) != OK) {
        r_detail = "Its columns could not be built into a table.";
        return RID();
    }
    return schema;
}

} // namespace

Bindings::~Bindings() {
    clear();
}

void Bindings::bind(NetwMultiplayer *p_session) {
    session = p_session;
}

Binding *Bindings::at(const RID &p_binding) {
    return book.get_or_null(p_binding);
}

const Binding *Bindings::at(const RID &p_binding) const {
    return book.get_or_null(p_binding);
}

bool Bindings::is_valid(const RID &p_binding) const {
    return book.get_or_null(p_binding) != nullptr;
}

RID Bindings::find(Node *p_root) const {
    if (p_root == nullptr) {
        return RID();
    }
    return find_id(gd::instance_id(p_root));
}

RID Bindings::find_id(ObjectID p_root) const {
    const HashMap<uint64_t, RID>::ConstIterator found
        = by_root.find(uint64_t(p_root));
    return found != by_root.end() ? found->value : RID();
}

void Bindings::forget(const RID &p_binding) {
    for (uint32_t at = 0; at < order.size(); ++at) {
        if (order[at] == p_binding) {
            order.remove_at(at);
            break;
        }
    }
}

void Bindings::release(const RID &p_binding) {
    Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return;
    }
    by_root.erase(uint64_t(held->root));
    withheld.erase(uint64_t(held->root));
    forget(p_binding);
    book.free(p_binding);
    memdelete(held);
}

void Bindings::clear() {
    LocalVector<RID> standing;
    standing.reserve(order.size());
    for (uint32_t at = 0; at < order.size(); ++at) {
        standing.push_back(order[at]);
    }
    for (uint32_t at = 0; at < standing.size(); ++at) {
        release(standing[at]);
    }
    by_root.clear();
    withheld.clear();
    refused.clear();
    order.clear();
}

void Bindings::withhold(Node *p_root) {
    if (p_root != nullptr) {
        withheld.insert(uint64_t(gd::instance_id(p_root)));
    }
}

void Bindings::publish(Node *p_root) {
    if (p_root != nullptr) {
        withheld.erase(uint64_t(gd::instance_id(p_root)));
    }
}

void Bindings::unwithhold(ObjectID p_root) {
    withheld.erase(uint64_t(p_root));
    refused.erase(uint64_t(p_root));
}

bool Bindings::withholds(Node *p_root) const {
    return p_root != nullptr && withheld.has(uint64_t(gd::instance_id(p_root)));
}

bool Bindings::awaits_load(const RID &p_binding) const {
    const Binding *held = book.get_or_null(p_binding);
    return held != nullptr
        && (held->loading.is_valid() || withheld.has(uint64_t(held->root))
            || (held->unmounted && held->load_on_spawn && !held->loaded));
}

void Bindings::refuse(Node *p_root) {
    if (p_root != nullptr) {
        refused.insert(uint64_t(gd::instance_id(p_root)));
    }
}

bool Bindings::refuses(Node *p_root) const {
    return p_root != nullptr && refused.has(uint64_t(gd::instance_id(p_root)));
}

void Bindings::sweep_unmounted() {
    LocalVector<RID> gone;
    for (uint32_t at = 0; at < order.size(); ++at) {
        const Binding *held = book.get_or_null(order[at]);
        if (held != nullptr && held->unmounted
            && node_of(held->root) == nullptr) {
            gone.push_back(order[at]);
        }
    }
    for (uint32_t at = 0; at < gone.size(); ++at) {
        release(gone[at]);
    }
    LocalVector<uint64_t> forgotten;
    for (const uint64_t root : refused) {
        if (node_of(ObjectID(root)) == nullptr) {
            forgotten.push_back(root);
        }
    }
    for (uint32_t at = 0; at < forgotten.size(); ++at) {
        refused.erase(forgotten[at]);
    }
}

RID Bindings::compile(Node *p_root) {
    NETW_ERR_COND_V(
        session == nullptr || p_root == nullptr,
        RID(),
        sys::TABLE,
        "Persistence was bound with no session or no entity root."
    );
    const RID standing = find(p_root);
    if (standing.is_valid()) {
        return standing;
    }
    const Ref<NetwPersistenceConfig> config
        = netw::script::model::get_persistence_config(p_root);
    NETW_ERR_COND_V(
        config.is_null(),
        RID(),
        sys::TABLE,
        "'%s' was asked to save, but it declares no persistence. Call "
        "Netw.configure_persistence on it before it enters the tree.",
        name_of(p_root).utf8().get_data()
    );

    const StringName schema_name = config->get_table_name();
    NETW_ERR_COND_V(
        String(schema_name).is_empty(),
        RID(),
        sys::TABLE,
        "'%s' declares persistence with no table. Name one with "
        "NetwPersistenceConfig.table.",
        name_of(p_root).utf8().get_data()
    );

    const StringName database_name = config->get_database_name();
    const RID database = session->database_lookup(database_name);
    NETW_ERR_COND_V(
        !database.is_valid(),
        RID(),
        sys::TABLE,
        "'%s' saves into database '%s', which this session does not "
        "declare. Declare it with Netw.configure_database in _init of a node "
        "above this entity.",
        name_of(p_root).utf8().get_data(),
        String(database_name).utf8().get_data()
    );

    const Callable provider = config->get_id_provider();
    NETW_ERR_COND_V(
        provider.is_null(),
        RID(),
        sys::TABLE,
        "'%s' declares no record id, so table '%s' has no row to save "
        "into. Pass a Callable to NetwPersistenceConfig.record_id.",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );
    bool called = false;
    const Variant answered = game_call(provider, Array(), RECORD_ID, &called);
    if (!called) {
        return RID();
    }
    const StringName record_id = answered;
    NETW_ERR_COND_V(
        String(record_id).is_empty(),
        RID(),
        sys::TABLE,
        "The record id Callable of '%s' returned an empty id for table '%s'. "
        "Return a non-empty id from it.",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );

    LocalVector<Node *> nodes;
    collect_nodes(p_root, nodes);

    LocalVector<BoundColumn> bound;
    LocalVector<int> types;
    for (uint32_t at = 0; at < nodes.size(); ++at) {
        Node *node = nodes[at];
        const Dictionary configs
            = netw::script::model::get_node_property_configs(node);
        const Array names = configs.keys();
        for (int index = 0; index < names.size(); ++index) {
            const StringName property = names[index];
            const Ref<NetwPropertyConfig> declaration = configs[property];
            if (declaration.is_null() || !declaration->is_persisted()) {
                continue;
            }
            const StringName key = declaration->get_saved_column().is_empty()
                ? property
                : declaration->get_saved_column();
            for (uint32_t seen = 0; seen < bound.size(); ++seen) {
                NETW_ERR_COND_V(
                    bound[seen].key == key,
                    RID(),
                    sys::TABLE,
                    "'%s' saves column '%s' of table '%s' twice, from "
                    "'%s.%s' and from '%s.%s'. Give one of them another "
                    "name in NetwPropertyConfig.persisted.",
                    name_of(p_root).utf8().get_data(),
                    String(key).utf8().get_data(),
                    String(schema_name).utf8().get_data(),
                    name_of(node_of(bound[seen].node)).utf8().get_data(),
                    String(bound[seen].property).utf8().get_data(),
                    name_of(node).utf8().get_data(),
                    String(property).utf8().get_data()
                );
            }
            NETW_ERR_COND_V(
                !gd::has_property(node, property),
                RID(),
                sys::TABLE,
                "'%s' saves column '%s' of table '%s' from '%s.%s', but "
                "that node has no such property. Fix the property name.",
                name_of(p_root).utf8().get_data(),
                String(key).utf8().get_data(),
                String(schema_name).utf8().get_data(),
                name_of(node).utf8().get_data(),
                String(property).utf8().get_data()
            );
            BoundColumn entry;
            entry.key = key;
            entry.node = gd::instance_id(node);
            entry.property = property;
            bound.push_back(entry);
            types.push_back(saved_type_of(node, property));
        }
    }

    NETW_ERR_COND_V(
        bound.is_empty(),
        RID(),
        sys::TABLE,
        "'%s' saves into table '%s', but no property is marked to save. "
        "Call NetwPropertyConfig.persisted on each property to save.",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );
    order_by_key(bound, types);
    for (uint32_t at = 0; at < bound.size(); ++at) {
        bound[at].index = int(at);
    }
    String detail;
    const RID schema
        = durable_schema(session, schema_name, bound, types, config, detail);
    NETW_ERR_COND_V(
        !schema.is_valid(),
        RID(),
        sys::TABLE,
        "'%s' cannot save into table '%s'. %s",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data(),
        detail.utf8().get_data()
    );

    Binding *made = memnew(Binding);
    made->root = gd::instance_id(p_root);
    made->database = database;
    made->schema = schema;
    made->record_id = record_id;
    made->columns = bound;
    made->interval = config->get_save_interval();
    made->load_on_spawn = config->get_load_at_spawn();
    const RID handle = book.make_rid(made);
    made->reference = live_values(handle);
    by_root[uint64_t(made->root)] = handle;
    order.push_back(handle);
    return handle;
}

Dictionary Bindings::live_values(const RID &p_binding) const {
    const Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return Dictionary();
    }
    if (held->departed) {
        return held->departure_values;
    }
    Dictionary values;
    for (uint32_t at = 0; at < held->columns.size(); ++at) {
        const BoundColumn &column = held->columns[at];
        Node *node = node_of(column.node);
        if (node == nullptr) {
            return Dictionary();
        }
        values[column.key] = port_get(node, column.property);
    }
    return values;
}

bool Bindings::is_dirty(const RID &p_binding) const {
    const Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return false;
    }
    const Dictionary values = live_values(p_binding);
    if (values.is_empty()) {
        return false;
    }
    return !rows_agree(values, held->baseline);
}

bool Bindings::has_unsaved_changes(const RID &p_binding) const {
    const Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return false;
    }
    const Dictionary values = live_values(p_binding);
    return !values.is_empty() && !rows_agree(values, held->reference);
}

void Bindings::adopt(const RID &p_binding, const Dictionary &p_values) {
    Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return;
    }
    held->baseline = p_values.duplicate(true);
    held->reference = held->baseline.duplicate(true);
}

bool Bindings::apply(const RID &p_binding, const Dictionary &p_values) {
    const Binding *held = book.get_or_null(p_binding);
    if (held == nullptr) {
        return false;
    }
    LocalVector<BoundColumn> columns;
    columns.reserve(held->columns.size());
    for (uint32_t at = 0; at < held->columns.size(); ++at) {
        columns.push_back(held->columns[at]);
    }
    for (uint32_t at = 0; at < columns.size(); ++at) {
        const BoundColumn &column = columns[at];
        Node *node = node_of(column.node);
        if (node == nullptr || !p_values.has(column.key)) {
            return false;
        }
        port_set(node, column.property, p_values[column.key]);
        if (book.get_or_null(p_binding) == nullptr) {
            return false;
        }
    }
    const Binding *after = book.get_or_null(p_binding);
    Node *root = after != nullptr ? node_of(after->root) : nullptr;
    return root != nullptr;
}

void Bindings::capture(const RID &p_binding) {
    Binding *held = book.get_or_null(p_binding);
    if (held == nullptr || held->departed) {
        return;
    }
    const Dictionary values = live_values(p_binding);
    if (!values.is_empty()) {
        held->departure_values = values;
    }
}

void Bindings::keep(const RID &p_binding) {
    Binding *held = book.get_or_null(p_binding);
    if (held != nullptr && !held->departed) {
        held->departure_values.clear();
    }
}

void Bindings::depart(const RID &p_binding, bool p_save) {
    Binding *held = book.get_or_null(p_binding);
    if (held == nullptr || held->departed) {
        return;
    }
    capture(p_binding);
    held->departed = true;
    held->final_save = p_save;
}

LocalVector<RID> Bindings::due(double p_delta) {
    LocalVector<RID> ready;
    for (uint32_t at = 0; at < order.size(); ++at) {
        Binding *held = book.get_or_null(order[at]);
        if (held == nullptr || held->interval <= 0.0 || held->in_flight
            || held->unmounted || (held->departed && !held->final_save)
            || awaits_load(order[at])) {
            continue;
        }
        held->elapsed += p_delta;
        if (held->elapsed < held->interval) {
            continue;
        }
        held->elapsed = 0.0;
        ready.push_back(order[at]);
    }
    return ready;
}

} // namespace netw::persist
