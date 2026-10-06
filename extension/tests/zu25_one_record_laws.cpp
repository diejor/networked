#include "support/netw_test.h"

#include "godot/local_vector.hpp"
#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestZu25OneRecordLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwMultiplayer;

class Wrappers {
    LocalVector<Node *> owners;

public:
    ~Wrappers() {
        for (Node *owner : owners) {
            memdelete(owner);
        }
    }

    Ref<NetwEntity> make() {
        Ref<NetwEntity> out;
        out.instantiate();
        Node *owner = memnew(Node);
        owners.push_back(owner);
        out->attach_to(owner);
        return out;
    }
};

TEST_CASE("[Networked][Liveness][Hosted] both doors keep one record") {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Wrappers made;

    const int64_t adopted_route = core->liveness_reserve_route();
    const Ref<NetwEntity> adopted = made.make();
    REQUIRE(core->liveness_bind_route(adopted_route, adopted.ptr()));
    const RID adopted_handle = adopted->get_rid_handle();
    REQUIRE(adopted_handle.is_valid());

    PackedInt64Array data_route;
    data_route.push_back(adopted_route);
    core->liveness_bind_routes_data(data_route);

    CHECK(adopted->get_rid_handle() == adopted_handle);
    CHECK(core->wrapper_for_route(adopted_route) == adopted);

    const int64_t data_first_route = core->liveness_reserve_route();
    PackedInt64Array second_data_route;
    second_data_route.push_back(data_first_route);
    core->liveness_bind_routes_data(second_data_route);
    const RID data_handle
        = core->get_liveness_core()->rid_from_route(data_first_route);
    REQUIRE(data_handle.is_valid());

    const Ref<NetwEntity> arriving = made.make();
    REQUIRE(core->liveness_bind_route(data_first_route, arriving.ptr()));

    CHECK(arriving->get_rid_handle() == data_handle);
    CHECK(core->wrapper_for_route(data_first_route) == arriving);

    const Ref<NetwEntity> second = made.make();
    CHECK_FALSE(core->liveness_bind_route(data_first_route, second.ptr()));
    CHECK(second->get_rid_handle() != data_handle);
    CHECK(core->wrapper_for_route(data_first_route) == arriving);
}

} // namespace TestZu25OneRecordLaws
