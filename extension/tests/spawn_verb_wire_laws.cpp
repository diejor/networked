#include "support/loopback_rig.h"

#include "netw/spawn/record.hpp"
#include "netw/wire/stream.hpp"

namespace TestSpawnVerbWire {

using namespace godot;

PackedByteArray verb_head(int64_t p_route, int64_t p_epoch) {
    netw::wire::WriteStream stream;
    netw::spawn::VerbHead head;
    head.route = uint64_t(p_route);
    head.epoch = uint64_t(p_epoch);
    REQUIRE(netw::spawn::VerbHead::wire.run(stream, head));
    REQUIRE(stream.align_verify());
    return stream.to_bytes();
}

TEST_CASE(
    "[Networked][Spawn][Hosted] VW1 the verb head transcribed from WIRE.md "
    "9.6 packs to the bytes tools/wire_decode.py holds against SpawnVerbHead"
) {
    const PackedByteArray bytes = verb_head(300, 2);

    REQUIRE(bytes.size() == 3);
    NETW_CHECK_EQ(bytes[0], 0xac);
    NETW_CHECK_EQ(bytes[1], 0x02);
    NETW_CHECK_EQ(bytes[2], 0x02);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] VW2 route zero is no verb head at all, "
    "because route zero is peer scoped and names no entity to exist"
) {
    netw::wire::ReadStream reader(verb_head(0, 0));
    int64_t route = -1;
    int64_t epoch = -1;

    CHECK_FALSE(netw::NetwMultiplayer::verb_head_read(reader, route, epoch));
}

} // namespace TestSpawnVerbWire

#if defined(NETW_TIER_HOSTED)

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/liveness_core.hpp"

namespace TestSpawnVerbWireHosted {

using namespace godot;
using namespace netw_test;
using netw::NetwEntity;

Node *build_body(const Variant &) {
    Node *made = memnew(Node);
    made->set_name("Body");
    return made;
}

Array one_arg() {
    Array out;
    out.push_back(String("body"));
    return out;
}

Array one_type() {
    Array out;
    out.push_back(int(Variant::STRING));
    return out;
}

int64_t counter_of(LoopbackRig &p_rig, int p_client, const char *p_key) {
    return int64_t(
        p_rig.spawn_plane(p_client)->counters().get(StringName(p_key), 0)
    );
}

PackedByteArray with_extra_varint(const PackedByteArray &p_frame, int p_at) {
    PackedByteArray out = p_frame.slice(0, p_at);
    out.push_back(0x01);
    out.append_array(p_frame.slice(p_at, p_frame.size()));
    return out;
}

TEST_CASE(
    "[Networked][Spawn] VW3 a SPAWN carrying one field the sender never wrote "
    "is refused whole, rather than applying in-range garbage to every field "
    "after the insertion point"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    rig.hold(0);

    const int route = rig.spawn_registered(
        StringName("vw_body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena,
        Variant(),
        false
    );
    rig.pump(10);
    const PackedByteArray frame = rig.spawn_frame_of(route);
    REQUIRE_FALSE(frame.is_empty());

    const int64_t refused = counter_of(rig, 0, "drops_spawn_truncated");
    rig.deliver_spawn(0, with_extra_varint(frame, 4));

    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_truncated"), refused + 1);
    CHECK(rig.route_node(route, 0) == nullptr);
}

TEST_CASE(
    "[Networked][Spawn] VW4 a SPAWN cut short of its own tail is refused "
    "whole, so a frame that did not decode to exhaustion applies nothing"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");
    rig.hold(0);

    const int route = rig.spawn_registered(
        StringName("vw_body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena,
        Variant(),
        false
    );
    rig.pump(10);
    const PackedByteArray frame = rig.spawn_frame_of(route);
    REQUIRE(frame.size() > 4);

    const int64_t refused = counter_of(rig, 0, "drops_spawn_truncated");
    rig.deliver_spawn(0, frame.slice(0, frame.size() - 1));

    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_truncated"), refused + 1);
    CHECK(rig.route_node(route, 0) == nullptr);
}

TEST_CASE(
    "[Networked][Spawn] VW5 a frame naming a life the receiver has already "
    "buried is refused, because an unreliable frame authored before a death "
    "can arrive after the reliable frame that opened the next life"
) {
    LoopbackRig rig(1);
    rig.mount();
    Node *arena = rig.mirror_child("Arena");

    const int route = rig.spawn_registered(
        StringName("vw_body"),
        callable_mp_static(&build_body),
        one_arg(),
        one_type(),
        arena,
        Variant(),
        true
    );
    rig.pump(10);
    REQUIRE(rig.route_node(route, 0) != nullptr);

    const int64_t stale = counter_of(rig, 0, "drops_spawn_stale_life");
    rig.deliver_despawn(0, route, 3);
    rig.pump(5);

    rig.deliver_despawn(0, route, 1);
    NETW_CHECK_EQ(counter_of(rig, 0, "drops_spawn_stale_life"), stale + 1);
}

} // namespace TestSpawnVerbWireHosted

#endif
