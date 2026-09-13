#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/api/resolved_join.hpp"
#include "netw/session/frames.hpp"

namespace TestResolvedJoin {

using namespace godot;
using netw::AcceptFrame;
using netw::ResolvedJoin;

Ref<ResolvedJoin> a_join() {
    Ref<ResolvedJoin> join;
    join.instantiate();
    join->set_peer_id(7);
    join->set_username("valeria");
    Array args;
    args.push_back(1);
    args.push_back("x");
    join->set_arg_values(args);
    return join;
}

TEST_CASE("[Networked][Session][Hosted] J2 a join survives its own frame") {
    const Ref<ResolvedJoin> back
        = ResolvedJoin::deserialize(a_join()->serialize());

    REQUIRE(back.is_valid());
    NETW_CHECK_EQ(back->get_peer_id(), 7);
    const bool named = back->get_username() == StringName("valeria");
    CHECK(named);
    NETW_CHECK_EQ(back->get_arg_values().size(), 2);
    NETW_CHECK_EQ(int(back->get_arg_values()[0]), 1);
    const bool second = String(back->get_arg_values()[1]) == String("x");
    CHECK(second);
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
    const bool refuses_garbage = ResolvedJoin::deserialize(garbage).is_null();
    CHECK(refuses_garbage);

    const PackedByteArray whole = a_join()->serialize();
    const bool refuses_short
        = ResolvedJoin::deserialize(whole.slice(0, whole.size() - 1)).is_null();
    CHECK(refuses_short);

    PackedByteArray extended = whole;
    extended.push_back(0x00);
    const bool refuses_residue = ResolvedJoin::deserialize(extended).is_null();
    CHECK(refuses_residue);
}

TEST_CASE(
    "[Networked][Session][Hosted] J4 a join carrying no arguments still spells "
    "its values run, because every field the grammar names is written and a "
    "frame that leaves one out is not a shorter join but a refused one"
) {
    Ref<ResolvedJoin> bare;
    bare.instantiate();
    bare->set_peer_id(3);
    bare->set_username("valeria");

    const Ref<ResolvedJoin> back = ResolvedJoin::deserialize(bare->serialize());

    REQUIRE(back.is_valid());
    NETW_CHECK_EQ(back->get_peer_id(), 3);
    NETW_CHECK_EQ(back->get_arg_values().size(), 0);

    AcceptFrame frame;
    const bool decoded = netw::session::frame_read(bare->serialize(), frame);
    CHECK(decoded);
    const bool spells_values = !frame.values.is_empty();
    CHECK(spells_values);
}

} // namespace TestResolvedJoin
