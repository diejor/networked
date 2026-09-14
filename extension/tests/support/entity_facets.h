#pragma once

/* Saves and restores entity factories for one test case.
 *
 * The wrapper and facet factories outlive their records. The addon registers
 * defaults before tests run, so each case must restore the previous factories.
 *
 * `reset_for_case` also runs at SUBCASE start, so it cannot provide this scope.
 *
 * [codeblock]
 * EntityFactories factories;
 * NetwEntityRecord::set_part_factory(NetwEntityRecord::PART_SCENE, mint);
 * [/codeblock]
 */

// A sibling under `support/`, so the prelude is reached by its bare name here.
#include "netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace netw_test {

class EntityFactories {
    godot::Callable held[netw::NetwEntityRecord::PART_MAX];
    godot::Callable held_wrapper;

public:
    EntityFactories() {
        for (int at = 0; at < netw::NetwEntityRecord::PART_MAX; at++) {
            held[at] = netw::NetwEntityRecord::part_factory(at);
        }
        held_wrapper = netw::NetwMultiplayer::wrapper_factory();
        netw::NetwEntityRecord::clear_part_factories();
        netw::NetwMultiplayer::clear_wrapper_factory();
    }

    ~EntityFactories() {
        for (int at = 0; at < netw::NetwEntityRecord::PART_MAX; at++) {
            netw::NetwEntityRecord::set_part_factory(at, held[at]);
        }
        netw::NetwMultiplayer::set_wrapper_factory(held_wrapper);
    }
};

} // namespace netw_test
