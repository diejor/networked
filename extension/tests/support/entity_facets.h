#pragma once

/* A case's own entity factories, given back at the end of it.
 *
 * The wrapper mint and `NetwEntityRecord`'s facet factories outlive every
 * record, and in the library tier the addon has ALREADY registered its own by
 * the time a case runs. So a guard that only cleared would strip the rig's
 * entities from every case after it, and one that only saved would let a case
 * decide what the next one mints.
 *
 * A reset hook cannot carry this either: `reset_for_case` runs at SUBCASE start
 * as well as case start, so a factory registered at the top of a case body
 * would be gone inside that case's own subcases.
 *
 * [codeblock]
 * EntityFactories factories;
 * NetwEntityRecord::set_part_factory(NetwEntityRecord::PART_SCENE, mint);
 * [/codeblock]
 */

// A sibling under `support/`, so the prelude is reached by its bare name here.
#include "netw_test.h"

#include "godot/callable.hpp"
#include "netw/entity_record.hpp"
#include "netw/netw_multiplayer.hpp"

namespace netw_test {

class EntityFactories {
    godot::Callable held[netw::NetwEntityRecord::PART_MAX];
    godot::Callable held_wrapper;
    godot::Callable held_declaration;

public:
    EntityFactories() {
        for (int at = 0; at < netw::NetwEntityRecord::PART_MAX; at++) {
            held[at] = netw::NetwEntityRecord::part_factory(at);
        }
        held_wrapper = netw::NetwMultiplayerCore::wrapper_factory();
        held_declaration = netw::NetwEntityRecord::scene_declaration_reader();
        netw::NetwEntityRecord::clear_part_factories();
        netw::NetwMultiplayerCore::clear_wrapper_factory();
    }

    ~EntityFactories() {
        for (int at = 0; at < netw::NetwEntityRecord::PART_MAX; at++) {
            netw::NetwEntityRecord::set_part_factory(at, held[at]);
        }
        netw::NetwMultiplayerCore::set_wrapper_factory(held_wrapper);
        netw::NetwEntityRecord::set_scene_declaration_reader(held_declaration);
    }
};

} // namespace netw_test
