#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

namespace TestNetwSceneRequestFrameLaws {

using namespace godot;
using netw::NetwMultiplayerCore;

Ref<NetwMultiplayerCore> hosting_core() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    return core;
}

PackedByteArray request_frame(
    int p_request_id,
    bool p_is_path,
    const Variant &p_destination,
    const Variant &p_args
) {
    Array row;
    row.push_back(p_request_id);
    row.push_back(p_is_path);
    row.push_back(p_destination);
    row.push_back(p_args);
    return netw::gd::var_to_bytes(row);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ1 a request frame is read back in the order "
    "it was written, so the one writer of this frame and the one reader of it "
    "cannot drift apart over what its fields mean"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();
    Array asked;
    asked.push_back(StringName("seat"));

    const Array row = core->scene_request_frame_row(
        request_frame(9, true, String("res://arena.tscn"), asked),
        7,
        1000
    );

    REQUIRE(row.size() == 4);
    const Array carried = row[3];
    NETW_CHECK_EQ(int(row[0]), 9);
    CHECK(bool(row[1]));
    CHECK(String(row[2]) == String("res://arena.tscn"));
    REQUIRE(carried.size() == 1);
    CHECK(StringName(carried[0]) == StringName("seat"));
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ2 a peer that is not authority reads no "
    "request at all, so a frame that reached the wrong peer decides nothing "
    "there and is not charged to its sender either"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_client(42);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    const PackedByteArray frame
        = request_frame(9, false, StringName("Arena"), Array());

    REQUIRE_FALSE(core->is_server());
    for (int at = 0; at < 40; ++at) {
        CHECK(core->scene_request_frame_row(frame, 7, 1000).is_empty());
    }
    NETW_CHECK_EQ(int(core->verdict_total(int64_t(ERR_BUSY))), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ3 a sender past the rate reads no request "
    "and is counted once, while the host is never limited, so a peer that "
    "cannot flood the join door cannot reach the same session by this one"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();
    const PackedByteArray frame
        = request_frame(9, false, StringName("Arena"), Array());

    for (int at = 0; at < 20; ++at) {
        CHECK_FALSE(core->scene_request_frame_row(frame, 1, 1000).is_empty());
    }
    NETW_CHECK_EQ(int(core->verdict_total(int64_t(ERR_BUSY))), 0);

    for (int at = 0; at < 8; ++at) {
        CHECK_FALSE(core->scene_request_frame_row(frame, 42, 1000).is_empty());
    }

    CHECK(core->scene_request_frame_row(frame, 42, 1000).is_empty());
    NETW_CHECK_EQ(int(core->verdict_total(int64_t(ERR_BUSY))), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ4 a payload that is not a four-field row is "
    "refused whole rather than read positionally, because a short row read "
    "field by field would open a request under whatever its first field "
    "decoded as"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();

    Array short_row;
    short_row.push_back(9);
    short_row.push_back(false);
    short_row.push_back(StringName("Arena"));
    Array long_row = short_row.duplicate();
    long_row.push_back(Array());
    long_row.push_back(Array());
    const PackedByteArray shortened = netw::gd::var_to_bytes(short_row);
    const PackedByteArray lengthened = netw::gd::var_to_bytes(long_row);
    const PackedByteArray scalar = netw::gd::var_to_bytes(Variant(9));

    CHECK(core->scene_request_frame_row(shortened, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(lengthened, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(scalar, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(PackedByteArray(), 7, 1000).is_empty());
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ5 a fourth field that carries no argument "
    "list is read as an empty one rather than refusing the row, because the "
    "arguments are the request handler's to judge and a caller that passed "
    "none has not malformed anything"
) {
    const Ref<NetwMultiplayerCore> core = hosting_core();

    const Array row = core->scene_request_frame_row(
        request_frame(9, false, StringName("Arena"), Variant()),
        7,
        1000
    );

    REQUIRE(row.size() == 4);
    const Variant carried = row[3];
    CHECK(carried.get_type() == Variant::ARRAY);
    NETW_CHECK_EQ(Array(carried).size(), 0);
}

} // namespace TestNetwSceneRequestFrameLaws
