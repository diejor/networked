#include "netw/persist/binding.hpp"

#include "godot/object.hpp"
#include "godot/utility.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/persistence_config.hpp"
#include "netw/api/property_config.hpp"
#include "netw/log.hpp"
#include "netw/object_port.hpp"
#include "netw/schema_core.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw::persist {

namespace {

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
        if (left.get_type() != right.get_type()
            || bool(left != right)) {
            return false;
        }
    }
    return true;
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
}

bool Bindings::withholds(Node *p_root) const {
    return p_root != nullptr
        && withheld.has(uint64_t(gd::instance_id(p_root)));
}

bool Bindings::awaits_load(const RID &p_binding) const {
    const Binding *held = book.get_or_null(p_binding);
    return held != nullptr
        && (held->loading.is_valid() || withheld.has(uint64_t(held->root)));
}

RID Bindings::compile(Node *p_root) {
    NETW_ERR_COND_V(
        session == nullptr || p_root == nullptr,
        RID(),
        sys::TABLE,
        "a persistence binding needs a session and an entity root"
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
        "entity '%s' binds no persistence, because it declared none. Declare "
        "it with Netw.configure_persistence(self) from _ready",
        name_of(p_root).utf8().get_data()
    );

    const Ref<NetwSchema> declared = config->get_schema();
    NETW_ERR_COND_V(
        declared.is_null(),
        RID(),
        sys::TABLE,
        "entity '%s' declares persistence with no schema. Hand one to "
        "Netw.configure_persistence(self).schema(...)",
        name_of(p_root).utf8().get_data()
    );
    const StringName schema_name = declared->get_schema_name();

    const StringName database_name = config->get_database_name();
    const RID database = session->database_find(database_name);
    NETW_ERR_COND_V(
        !database.is_valid(),
        RID(),
        sys::TABLE,
        "entity '%s' saves into database '%s', which this session does not "
        "declare. Declare it with Netw.configure_database(self, name) first",
        name_of(p_root).utf8().get_data(),
        String(database_name).utf8().get_data()
    );

    const RID schema = session->schema_of_declaration(declared);
    NETW_ERR_COND_V(
        !schema.is_valid(),
        RID(),
        sys::TABLE,
        "entity '%s' names schema '%s', which this session could not seal",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );

    const Callable provider = config->get_id_provider();
    NETW_ERR_COND_V(
        provider.is_null(),
        RID(),
        sys::TABLE,
        "entity '%s' declares no record id, so schema '%s' has no row to save "
        "into. Hand a Callable to "
        "Netw.configure_persistence(self).record_id(...)",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );
    const Variant answered = provider.call();
    const StringName record_id = answered;
    NETW_ERR_COND_V(
        String(record_id).is_empty(),
        RID(),
        sys::TABLE,
        "entity '%s' answered an empty record id for schema '%s', and a row "
        "with no id is a row nothing can read back",
        name_of(p_root).utf8().get_data(),
        String(schema_name).utf8().get_data()
    );

    const int column_count = session->schema_get_column_count(schema);
    LocalVector<Node *> nodes;
    collect_nodes(p_root, nodes);

    LocalVector<BoundColumn> bound;
    for (uint32_t at = 0; at < nodes.size(); ++at) {
        Node *node = nodes[at];
        const Dictionary configs
            = netw::script::model::get_node_property_configs(node);
        const Array names = configs.keys();
        for (int index = 0; index < names.size(); ++index) {
            const StringName property = names[index];
            const Ref<NetwPropertyConfig> declaration = configs[property];
            if (declaration.is_null()) {
                continue;
            }
            const Ref<NetwColumnRef> column
                = declaration->get_persist_column();
            if (column.is_null()) {
                continue;
            }
            NETW_ERR_COND_V(
                column->get_schema() != declared,
                RID(),
                sys::TABLE,
                "entity '%s' saves into schema '%s', and '%s.%s' names a "
                "column of schema '%s'. A column reference belongs to the "
                "schema it was taken from",
                name_of(p_root).utf8().get_data(),
                String(schema_name).utf8().get_data(),
                name_of(node).utf8().get_data(),
                String(property).utf8().get_data(),
                column->get_schema().is_valid()
                    ? String(column->get_schema()->get_schema_name())
                          .utf8()
                          .get_data()
                    : "a freed schema"
            );
            const int column_index = column->get_index();
            NETW_ERR_COND_V(
                column_index < 0 || column_index >= column_count,
                RID(),
                sys::TABLE,
                "entity '%s' binds '%s.%s' to column %d of schema '%s', which "
                "carries %d columns",
                name_of(p_root).utf8().get_data(),
                name_of(node).utf8().get_data(),
                String(property).utf8().get_data(),
                int(column_index),
                String(schema_name).utf8().get_data(),
                int(column_count)
            );
            const StringName key
                = session->schema_get_column_key(schema, column_index);
            for (uint32_t seen = 0; seen < bound.size(); ++seen) {
                NETW_ERR_COND_V(
                    bound[seen].index == column_index,
                    RID(),
                    sys::TABLE,
                    "entity '%s' binds column '%s' of schema '%s' twice, from "
                    "'%s.%s' and from '%s.%s'. One column holds one property",
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
                "entity '%s' binds column '%s' of schema '%s' to '%s.%s', "
                "which that node does not carry",
                name_of(p_root).utf8().get_data(),
                String(key).utf8().get_data(),
                String(schema_name).utf8().get_data(),
                name_of(node).utf8().get_data(),
                String(property).utf8().get_data()
            );
            const int type
                = int(session->schema_get_column_type(schema, column_index));
            const int stride
                = session->schema_get_column_stride(schema, column_index);
            NETW_ERR_COND_V(
                SchemaCore::validate_value(
                    type,
                    stride,
                    node->get(property)
                ) != OK,
                RID(),
                sys::TABLE,
                "entity '%s' binds column '%s' of schema '%s' to '%s.%s', "
                "which holds a value that column cannot store",
                name_of(p_root).utf8().get_data(),
                String(key).utf8().get_data(),
                String(schema_name).utf8().get_data(),
                name_of(node).utf8().get_data(),
                String(property).utf8().get_data()
            );
            BoundColumn entry;
            entry.index = column_index;
            entry.key = key;
            entry.node = gd::instance_id(node);
            entry.property = property;
            bound.push_back(entry);
        }
    }

    NETW_ERR_COND_V(
        int(bound.size()) != column_count,
        RID(),
        sys::TABLE,
        "entity '%s' binds %d of the %d columns of schema '%s'. Every column "
        "of a declared schema holds one property of this entity",
        name_of(p_root).utf8().get_data(),
        int(bound.size()),
        int(column_count),
        String(schema_name).utf8().get_data()
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
            || (held->departed && !held->final_save)
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
