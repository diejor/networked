#include "support/netw_test.h"

#include "netw/api/despawn_config.hpp"

#include "godot/callable.hpp"

namespace TestNetwDespawnConfig {

using namespace godot;
using netw::NetwDespawnConfig;

Ref<NetwDespawnConfig> make_config() {
    Ref<NetwDespawnConfig> config;
    config.instantiate();
    return config;
}

TEST_CASE(
    "[Networked][Liveness][Hosted] DC1 a config that declared nothing frees "
    "immediately, so a removing peer reads the defaults as the policy rather "
    "than branching on an absent declaration"
) {
    const Ref<NetwDespawnConfig> config = make_config();

    CHECK(config->get_hook_method().is_empty());
    NETW_CHECK_CLOSE(config->get_linger_seconds(), 0.0, 1e-9);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] DC2 before_removal keeps the method name "
    "and drops the object, so the config outlives the instance it was "
    "authored from and never keeps that instance alive"
) {
    const Ref<NetwDespawnConfig> config = make_config();
    Ref<NetwDespawnConfig> authoring_instance = make_config();

    config->before_removal(Callable(authoring_instance.ptr(), "linger"));

    CHECK(bool(config->get_hook_method() == StringName("linger")));
    NETW_CHECK_EQ(authoring_instance->get_reference_count(), 1);
}

TEST_CASE(
    "[Networked][Liveness][Hosted] DC3 every verb answers the same config, "
    "which is what lets one _init declare the hook and the linger in one "
    "chain"
) {
    const Ref<NetwDespawnConfig> config = make_config();

    CHECK(bool(config->before_removal(Callable()) == config));
    CHECK(bool(config->linger(0.5) == config));
    NETW_CHECK_CLOSE(config->get_linger_seconds(), 0.5, 1e-9);
}

} // namespace TestNetwDespawnConfig
