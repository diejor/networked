#include "support/netw_test.h"

#include "netw/api/persistence_config.hpp"

#include "godot/callable.hpp"

namespace TestNetwPersistenceConfig {

using namespace godot;
using netw::NetwPersistenceConfig;
using netw::NetwSchema;

Ref<NetwPersistenceConfig> make_config() {
    Ref<NetwPersistenceConfig> config;
    config.instantiate();
    return config;
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC1 a config that declared nothing "
    "saves on no cadence and loads on spawn, so an entity writes only when "
    "its game asks and reads its row before it plays"
) {
    const Ref<NetwPersistenceConfig> config = make_config();

    NETW_CHECK_CLOSE(config->get_save_interval(), 0.0, 1e-9);
    CHECK(config->get_load_at_spawn());
    CHECK(String(config->get_database_name()).is_empty());
    CHECK(config->get_schema().is_null());
    CHECK(config->get_id_provider().is_null());
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC2 record_id keeps the whole Callable, "
    "object and method, so two entities running one script answer two ids"
) {
    const Ref<NetwPersistenceConfig> first = make_config();
    const Ref<NetwPersistenceConfig> second = make_config();
    const Ref<NetwSchema> left = NetwSchema::create("left");
    const Ref<NetwSchema> right = NetwSchema::create("right");

    first->record_id(Callable(left.ptr(), "get_schema_name"));
    second->record_id(Callable(right.ptr(), "get_schema_name"));

    CHECK(bool(
        StringName(first->get_id_provider().call()) == StringName("left")
    ));
    CHECK(bool(
        StringName(second->get_id_provider().call()) == StringName("right")
    ));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC3 every verb answers the same config, "
    "which is what lets one _ready declare the database, the schema, the "
    "cadence and the record id in one chain"
) {
    const Ref<NetwPersistenceConfig> config = make_config();
    const Ref<NetwSchema> schema = NetwSchema::create("players");

    CHECK(bool(config->database(StringName("saves")) == config));
    CHECK(bool(config->schema(schema) == config));
    CHECK(bool(config->record_id(Callable()) == config));
    CHECK(bool(config->interval(0.25) == config));
    CHECK(bool(config->load_on_spawn(false) == config));

    CHECK(bool(config->get_database_name() == StringName("saves")));
    CHECK(bool(config->get_schema() == schema));
    NETW_CHECK_CLOSE(config->get_save_interval(), 0.25, 1e-9);
    CHECK(!config->get_load_at_spawn());
}

} // namespace TestNetwPersistenceConfig
