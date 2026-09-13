#include "support/netw_test.h"

#include "netw/api/entity_options.hpp"

using namespace godot;

namespace TestNetwEntityOptions {

using godot::Ref;
using godot::StringName;
using netw::NetwControlRequest;
using netw::NetwDespawnOpts;
using netw::NetwReparentOpts;

TEST_CASE(
    "[Networked][Entity][Hosted] O1 a fresh despawn record carries the "
    "defaults teardown is safe under"
) {
    Ref<NetwDespawnOpts> opts;
    opts.instantiate();

    CHECK(opts->reason == StringName());
    CHECK(opts->flush_save);
    CHECK(opts->defer_free);
    CHECK_FALSE(opts->linger);
    NETW_CHECK_CLOSE(opts->linger_seconds, 1.0, 1e-9);
}

TEST_CASE(
    "[Networked][Entity][Hosted] O2 naming a despawn stamps the reason and "
    "leaves every other default standing"
) {
    Ref<NetwDespawnOpts> opts = NetwDespawnOpts::create("killed");

    CHECK(opts->reason == StringName("killed"));
    CHECK(opts->flush_save);
    CHECK(opts->defer_free);
    CHECK_FALSE(opts->linger);
    NETW_CHECK_CLOSE(opts->linger_seconds, 1.0, 1e-9);

    CHECK(NetwDespawnOpts::create(StringName())->reason == StringName());
}

TEST_CASE(
    "[Networked][Entity][Hosted] O3 a reparent option carries its reason"
) {
    Ref<NetwReparentOpts> opts;
    opts.instantiate();

    CHECK(opts->reason == StringName());
    opts->set_reason(StringName("boarded"));
    CHECK(opts->reason == StringName("boarded"));
}

TEST_CASE(
    "[Networked][Entity][Hosted] O4 a refused control request stays refused, "
    "whatever the listeners after it say"
) {
    Ref<NetwControlRequest> request;
    request.instantiate();
    request->set_requester(42);

    CHECK_FALSE(request->denied);
    request->deny();
    CHECK(request->denied);

    request->set_denied(false);
    CHECK(request->denied);
    NETW_CHECK_EQ(request->requester, 42);
}

} // namespace TestNetwEntityOptions
