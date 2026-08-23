// The entity option records' laws.
//
// A record with no logic still has a contract, and it is entirely in the
// defaults and in what a field's absence means. Both are things no caller ever
// writes down, so both are what a port silently changes: a despawn record that
// came up with `flush_save` false would skip every save in the project with
// nothing to read, and a reparent target initialized to a zero vector would
// teleport every reparent to the world origin.

#include "support/netw_test.h"

#include "netw/api/entity_options.hpp"

using namespace godot;

namespace TestNetwEntityOptions {

using godot::Ref;
using godot::StringName;
using godot::Variant;
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
    "[Networked][Entity][Hosted] O3 an unset reparent target is nothing, not "
    "the origin"
) {
    Ref<NetwReparentOpts> opts;
    opts.instantiate();

    NETW_CHECK_EQ(opts->target_global_position.get_type(), Variant::NIL);
    CHECK_FALSE(opts->preserve_history);
    CHECK(opts->reason == StringName());

    // Asking for the origin is a request the record can carry, and it has to
    // read differently from having asked for nothing.
    opts->set_target_global_position(godot::Vector3());
    NETW_CHECK_EQ(opts->target_global_position.get_type(), Variant::VECTOR3);
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

    // The listeners share one record, so a later one writing the field must
    // not undo an earlier one's refusal. Order would otherwise decide who is
    // allowed to steer.
    request->set_denied(false);
    CHECK(request->denied);
    NETW_CHECK_EQ(request->requester, 42);
}

} // namespace TestNetwEntityOptions
