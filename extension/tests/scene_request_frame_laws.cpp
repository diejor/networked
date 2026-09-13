#include "support/netw_test.h"

#include "godot/utility.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session/frames.hpp"

namespace TestNetwSceneRequestFrameLaws {

using namespace godot;
using netw::NetwMultiplayer;

Ref<NetwMultiplayer> hosting_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    return core;
}

PackedByteArray request_frame(
    int p_request_id,
    const String &p_path,
    int p_scope
) {
    netw::session::SceneRequest frame;
    frame.request_id = uint64_t(p_request_id);
    frame.path = p_path;
    frame.scope = p_scope;
    return netw::session::frame_write(frame);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ1 a request frame is read back in the order "
    "it was written, so the one writer of this frame and the one reader of it "
    "cannot drift apart over what its fields mean"
) {
    const Ref<NetwMultiplayer> core = hosting_core();

    const Array row = core->scene_request_frame_row(
        request_frame(9, String("res://arena.tscn"), 1),
        7,
        1000
    );

    REQUIRE(row.size() == 3);
    NETW_CHECK_EQ(int(row[0]), 9);
    CHECK(String(row[1]) == String("res://arena.tscn"));
    NETW_CHECK_EQ(int(row[2]), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ2 a peer that is not authority reads no "
    "request at all, so a frame that reached the wrong peer decides nothing "
    "there and is not charged to its sender either"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_client(42);
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    const PackedByteArray frame
        = request_frame(9, String("res://arena.tscn"), 0);

    REQUIRE_FALSE(core->is_server());
    for (int at = 0; at < 40; ++at) {
        CHECK(core->scene_request_frame_row(frame, 7, 1000).is_empty());
    }
    NETW_CHECK_EQ(int(core->stats_get_verdict_count(ERR_BUSY)), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ3 a sender past the rate reads no request "
    "and is counted once, while the host is never limited, so a peer that "
    "cannot flood the join door cannot reach the same session by this one"
) {
    const Ref<NetwMultiplayer> core = hosting_core();
    const PackedByteArray frame
        = request_frame(9, String("res://arena.tscn"), 0);

    for (int at = 0; at < 20; ++at) {
        CHECK_FALSE(core->scene_request_frame_row(frame, 1, 1000).is_empty());
    }
    NETW_CHECK_EQ(int(core->stats_get_verdict_count(ERR_BUSY)), 0);

    for (int at = 0; at < 8; ++at) {
        CHECK_FALSE(core->scene_request_frame_row(frame, 42, 1000).is_empty());
    }

    CHECK(core->scene_request_frame_row(frame, 42, 1000).is_empty());
    NETW_CHECK_EQ(int(core->stats_get_verdict_count(ERR_BUSY)), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] SQ4 a payload that does not spell the whole "
    "request is refused rather than read positionally, because a short frame "
    "read field by field would open a request under whatever its first field "
    "decoded as"
) {
    const Ref<NetwMultiplayer> core = hosting_core();
    const PackedByteArray whole
        = request_frame(9, String("res://arena.tscn"), 1);

    PackedByteArray lengthened = whole;
    lengthened.push_back(0x00);
    const PackedByteArray shortened = whole.slice(0, whole.size() - 1);
    PackedByteArray scalar;
    scalar.push_back(9);

    CHECK(core->scene_request_frame_row(shortened, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(lengthened, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(scalar, 7, 1000).is_empty());
    CHECK(core->scene_request_frame_row(PackedByteArray(), 7, 1000).is_empty());
}

} // namespace TestNetwSceneRequestFrameLaws
