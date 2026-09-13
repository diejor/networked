#include "support/netw_test.h"

#include "netw/api/persistence_config.hpp"

#include "godot/callable.hpp"

namespace TestNetwPersistenceConfig {

using namespace godot;
using netw::NetwPersistenceConfig;

Ref<NetwPersistenceConfig> make_config() {
    Ref<NetwPersistenceConfig> config;
    config.instantiate();
    return config;
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC1 a config that declared nothing "
    "already snapshots every five seconds and hydrates on spawn, so the "
    "server reads the defaults as the policy rather than branching on an "
    "absent declaration"
) {
    const Ref<NetwPersistenceConfig> config = make_config();

    NETW_CHECK_CLOSE(config->get_default_interval(), 5.0, 1e-9);
    CHECK(config->get_hydrate_on_spawn_enabled());
    CHECK(config->get_db().is_null());
    CHECK(config->get_table_name().is_empty());
    CHECK(config->get_record_id_provider().is_empty());
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC2 record_id keeps the method name and "
    "drops the object, so the config outlives the instance it was authored "
    "from and never keeps that instance alive"
) {
    const Ref<NetwPersistenceConfig> config = make_config();
    Ref<NetwPersistenceConfig> authoring_instance = make_config();

    config->record_id(Callable(authoring_instance.ptr(), "table"));

    CHECK(bool(config->get_record_id_provider() == StringName("table")));
    NETW_CHECK_EQ(authoring_instance->get_reference_count(), 1);
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC3 database answers back the very "
    "database it was handed, and the parameter's type is what refuses "
    "anything else before the engine can flush into it"
) {
    const Ref<NetwPersistenceConfig> config = make_config();
    Ref<netw::NetwDatabase> declared;
    declared.instantiate();

    config->database(declared);

    CHECK(bool(config->get_db() == declared));
}

TEST_CASE(
    "[Networked][Persistence][Hosted] PC4 every verb answers the same config, "
    "which is what lets one _init declare the database, the table, the "
    "cadence and the record id in one chain"
) {
    const Ref<NetwPersistenceConfig> config = make_config();

    CHECK(bool(config->database(Ref<netw::NetwDatabase>()) == config));
    CHECK(bool(config->table(StringName("players")) == config));
    CHECK(bool(config->interval(0.25) == config));
    CHECK(bool(config->hydrate_on_spawn(false) == config));
    CHECK(bool(config->record_id(Callable()) == config));

    CHECK(bool(config->get_table_name() == StringName("players")));
    NETW_CHECK_CLOSE(config->get_default_interval(), 0.25, 1e-9);
    CHECK(!config->get_hydrate_on_spawn_enabled());
}

} // namespace TestNetwPersistenceConfig
