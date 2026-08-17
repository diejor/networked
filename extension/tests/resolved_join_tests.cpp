// Laws for ResolvedJoin, the accepted-join record and its frame.
//
// The frame is read from bytes a remote peer sent, so the laws that matter are
// the refusals: what a payload has to spell before it becomes a join, and what
// it may leave out. The golden below is the byte string the GDScript form
// wrote, kept because a peer running either form has to read the other's frame.

#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/resolved_join.hpp"

namespace TestResolvedJoin {

using namespace godot;
using netw::ResolvedJoin;

// peer_id 7, username "valeria", arg_values [1, "x"], is_debug true, as
// `var_to_bytes` wrote it from GDScript's Lua-style dictionary literal. The
// keys are StringName rather than String, which is the whole reason this is
// recorded rather than described.
constexpr const char *GOLDEN =
    "1b000000040000001500000007000000706565725f696400020000000700000015000000"
    "08000000757365726e616d65150000000700000076616c6572696100150000000a000000"
    "6172675f76616c75657300001c0000000200000002000000010000000400000001000000"
    "78000000150000000800000069735f64656275670100000001000000";

Ref<ResolvedJoin> a_join() {
    Ref<ResolvedJoin> join;
    join.instantiate();
    join->set_peer_id(7);
    join->set_username("valeria");
    Array args;
    args.push_back(1);
    args.push_back("x");
    join->set_arg_values(args);
    join->set_is_debug(true);
    return join;
}

TEST_CASE(
    "[Networked][Session][Hosted] J1 the frame is the bytes the GDScript form "
    "wrote, so a peer running either one reads the other"
) {
    CHECK(netw::gd::hex_of(a_join()->serialize()) == String(GOLDEN));
}

TEST_CASE(
    "[Networked][Session][Hosted] J2 a join survives its own frame"
) {
    const Ref<ResolvedJoin> back =
        ResolvedJoin::deserialize(a_join()->serialize());

    REQUIRE(back.is_valid());
    NETW_CHECK_EQ(back->get_peer_id(), 7);
    CHECK(back->get_username() == StringName("valeria"));
    NETW_CHECK_EQ(back->get_arg_values().size(), 2);
    NETW_CHECK_EQ(int(back->get_arg_values()[0]), 1);
    CHECK(String(back->get_arg_values()[1]) == String("x"));
    NETW_CHECK_EQ(back->get_is_debug(), true);
}

TEST_CASE(
    "[Networked][Session][Hosted] J3 a payload that does not spell a join is "
    "refused whole, rather than read half way"
) {
    PackedByteArray garbage;
    garbage.push_back(1);
    garbage.push_back(2);
    garbage.push_back(3);
    garbage.push_back(4);
    CHECK(ResolvedJoin::deserialize(garbage).is_null());

    // A record is the only shape a join is written as, so anything else is not
    // a join that lost a field.
    Array not_a_record;
    not_a_record.push_back(1);
    CHECK(
        ResolvedJoin::deserialize(netw::gd::var_to_bytes(not_a_record)).is_null()
    );

    Dictionary no_peer;
    no_peer[StringName("username")] = StringName("valeria");
    CHECK(ResolvedJoin::deserialize(netw::gd::var_to_bytes(no_peer)).is_null());

    Dictionary no_name;
    no_name[StringName("peer_id")] = 7;
    CHECK(ResolvedJoin::deserialize(netw::gd::var_to_bytes(no_name)).is_null());

    // A peer is a number. Admitting a named one would seat the join against a
    // peer id nothing else in the session can answer for.
    Dictionary worded_peer;
    worded_peer[StringName("peer_id")] = "seven";
    worded_peer[StringName("username")] = StringName("valeria");
    CHECK(
        ResolvedJoin::deserialize(netw::gd::var_to_bytes(worded_peer)).is_null()
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] J4 the two optional fields default, so an "
    "older peer's shorter frame is still a join"
) {
    Dictionary minimal;
    minimal[StringName("peer_id")] = 3;
    minimal[StringName("username")] = StringName("valeria");

    const Ref<ResolvedJoin> back =
        ResolvedJoin::deserialize(netw::gd::var_to_bytes(minimal));

    REQUIRE(back.is_valid());
    NETW_CHECK_EQ(back->get_peer_id(), 3);
    NETW_CHECK_EQ(back->get_arg_values().size(), 0);
    NETW_CHECK_EQ(back->get_is_debug(), false);
}

} // namespace TestResolvedJoin
