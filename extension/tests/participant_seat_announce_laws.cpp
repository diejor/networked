#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/object.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/join_request.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/api/scene_handle.hpp"
#include "support/entity_facets.h"
#include "support/netw_call_log.h"
#include "support/netw_recorder.h"

namespace TestParticipantSeatAnnounceLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;
using netw_test::Recorder;

Ref<netw::NetwParticipant> a_seat_announcing_row() {
    Ref<netw::NetwParticipant> row;
    row.instantiate();
    return row;
}

Ref<NetwMultiplayer> peered_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

struct Seated {
    Ref<NetwMultiplayer> core;
    Ref<netw::NetwParticipant> row;
    int64_t peer = 0;
};

Seated a_local_participant(const Ref<NetwMultiplayer> &p_core) {
    Seated made;
    made.core = p_core;
    made.peer = int64_t(p_core->get_unique_id());
    made.row = a_seat_announcing_row();
    p_core->participant_adopt(made.peer, made.row);
    p_core->participant_publish_joined(made.peer);
    return made;
}

struct Bound {
    Ref<netw::NetwEntity> wrapper;
    netw::NetwEntityRecord *record = nullptr;
    RID handle;
    Node *owner = nullptr;
};

Bound bind_scene(const Ref<NetwMultiplayer> &p_core, Node *p_parent) {
    Bound out;
    out.owner = memnew(Node);
    p_parent->add_child(out.owner);
    out.wrapper.instantiate();
    out.handle = p_core->get_liveness_core()->entity_create();
    out.record = out.wrapper->get_record();
    out.record->adopt_handle(out.handle);
    out.record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        out.handle,
        route,
        out.wrapper,
        out.record,
        out.owner
    ));
    return out;
}

TEST_CASE(
    "[Networked][Session][Hosted] PJ1 a joined participant is one an accepted "
    "join names, so a peer with a row but no join is not in the roster the "
    "session publishes"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t local = int64_t(core->get_unique_id());
    const int64_t guest = local + 7;

    core->participant_ensure(guest);
    CHECK(core->participant_has(guest));
    CHECK(core->participant_joined_of(guest).is_null());
    CHECK(core->participant_joined_all().is_empty());
    CHECK(core->participant_local().is_null());

    Ref<netw::ResolvedJoin> join;
    join.instantiate();
    join->set_peer_id(guest);
    REQUIRE(core->session_remember_join(join));
    CHECK(core->participant_joined_of(guest).is_valid());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 1);
    CHECK(core->participant_local().is_null());

    Ref<netw::ResolvedJoin> mine;
    mine.instantiate();
    mine->set_peer_id(local);
    REQUIRE(core->session_remember_join(mine));
    CHECK(core->participant_local().is_valid());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 2);

    core->session_forget_peer(guest);
    CHECK(core->participant_joined_of(guest).is_null());
    NETW_CHECK_EQ(int(core->participant_joined_all().size()), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] PS1 seating a participant and telling the "
    "session are one act, so a caller cannot take the seat and leave every "
    "listener behind: the silent write moves the same seat and reaches "
    "nobody, which is a failure no reading of the seat can see"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const Seated local = a_local_participant(core);
    const RID arena = core->get_liveness_core()->entity_create();
    const RID annex = core->get_liveness_core()->entity_create();
    Recorder session(core.ptr(), Vector<StringName>({"scene_local_changed"}));

    CHECK(core->participant_take_seat(local.peer, arena));
    CHECK(core->participant_seat(local.peer) == arena);
    NETW_CHECK_EQ(session.count("scene_local_changed"), 0);

    CHECK(core->participant_seat_move(local.peer, annex));
    CHECK(core->participant_seat(local.peer) == annex);
    NETW_CHECK_EQ(session.count("scene_local_changed"), 1);

    CHECK(core->participant_leave_seat(local.peer, annex));
    CHECK_FALSE(core->participant_seat(local.peer).is_valid());
    NETW_CHECK_EQ(session.count("scene_local_changed"), 1);

    CHECK(core->participant_seat_move(local.peer, arena));
    CHECK(core->participant_seat_clear(local.peer, arena));
    CHECK_FALSE(core->participant_seat(local.peer).is_valid());
    NETW_CHECK_EQ(session.count("scene_local_changed"), 3);
}

TEST_CASE(
    "[Networked][Session][Hosted] PS2 the announcement is spent on a change "
    "and never on a repeat, so a re-seat into the scene already held and a "
    "release naming a scene no longer held both answer false and say "
    "nothing, which is what lets an admission edge fire as often as it likes"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const Seated local = a_local_participant(core);
    const RID arena = core->get_liveness_core()->entity_create();
    const RID annex = core->get_liveness_core()->entity_create();
    Recorder session(core.ptr(), Vector<StringName>({"scene_local_changed"}));

    CHECK(core->participant_seat_move(local.peer, arena));
    NETW_CHECK_EQ(session.count("scene_local_changed"), 1);

    CHECK_FALSE(core->participant_seat_move(local.peer, arena));
    NETW_CHECK_EQ(session.count("scene_local_changed"), 1);

    CHECK_FALSE(core->participant_seat_clear(local.peer, annex));
    CHECK(core->participant_seat(local.peer) == arena);
    NETW_CHECK_EQ(session.count("scene_local_changed"), 1);

    CHECK(core->participant_seat_clear(local.peer, arena));
    NETW_CHECK_EQ(session.count("scene_local_changed"), 2);

    CHECK_FALSE(core->participant_seat_clear(local.peer, arena));
    NETW_CHECK_EQ(session.count("scene_local_changed"), 2);
}

TEST_CASE(
    "[Networked][Session][Hosted] PS3 the edge names the seat left and the "
    "seat taken as the scenes' own handles, and a clear names nothing as the "
    "destination, so a listener reads where this peer went without asking "
    "the roster again"
) {
    Ref<NetwMultiplayer> core = peered_core();
    Node *root = memnew(Node);
    const Bound arena = bind_scene(core, root);
    const Bound annex = bind_scene(core, root);
    const Seated local = a_local_participant(core);
    const Ref<netw::NetwSceneHandle> arena_handle
        = core->scene_handle_of(arena.handle);
    const Ref<netw::NetwSceneHandle> annex_handle
        = core->scene_handle_of(annex.handle);
    REQUIRE(arena_handle.is_valid());
    REQUIRE(annex_handle.is_valid());
    REQUIRE(arena_handle != annex_handle);
    Recorder session(core.ptr(), Vector<StringName>({"scene_local_changed"}));

    CHECK(core->participant_seat_move(local.peer, arena.handle));
    REQUIRE(session.args("scene_local_changed", 0).size() == 2);
    CHECK(
        Object::cast_to<Object>(session.args("scene_local_changed", 0)[0])
        == nullptr
    );
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("scene_local_changed", 0)[1]),
        arena_handle.ptr()
    );

    CHECK(core->participant_seat_move(local.peer, annex.handle));
    REQUIRE(session.args("scene_local_changed", 1).size() == 2);
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("scene_local_changed", 1)[0]),
        arena_handle.ptr()
    );
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("scene_local_changed", 1)[1]),
        annex_handle.ptr()
    );

    CHECK(core->participant_seat_clear(local.peer, annex.handle));
    REQUIRE(session.args("scene_local_changed", 2).size() == 2);
    NETW_CHECK_EQ(
        Object::cast_to<Object>(session.args("scene_local_changed", 2)[0]),
        annex_handle.ptr()
    );
    CHECK(
        Object::cast_to<Object>(session.args("scene_local_changed", 2)[1])
        == nullptr
    );

    memdelete(root);
}

TEST_CASE(
    "[Networked][Session][Hosted] PS4 another peer taking a seat is that "
    "peer's move and not this session's, so the row is told and the session "
    "republishes nothing"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const CallLog heard;
    const Seated local = a_local_participant(core);
    const int64_t other = local.peer + 1;
    const Ref<netw::NetwParticipant> theirs = a_seat_announcing_row();
    core->participant_adopt(other, theirs);
    core->participant_publish_joined(other);
    const RID arena = core->get_liveness_core()->entity_create();
    Recorder session(core.ptr(), Vector<StringName>({"scene_local_changed"}));
    theirs->connect("scene_changed", heard.callable("theirs"));

    CHECK(core->participant_seat_move(other, arena));
    NETW_CHECK_EQ(heard.count("theirs"), 1);
    NETW_CHECK_EQ(session.count("scene_local_changed"), 0);

    CHECK(core->participant_seat_clear(other, arena));
    NETW_CHECK_EQ(heard.count("theirs"), 2);
    NETW_CHECK_EQ(session.count("scene_local_changed"), 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] PS5 a seat belongs to a roster row, so a "
    "peer the roster never opened is refused by both verbs and holds no "
    "seat afterwards, which is what lets an admission edge run ahead of the "
    "join frame and park instead of inventing a participant"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const Seated local = a_local_participant(core);
    const int64_t stranger = local.peer + 7;
    const RID arena = core->get_liveness_core()->entity_create();
    Recorder session(core.ptr(), Vector<StringName>({"scene_local_changed"}));

    CHECK_FALSE(core->participant_seat_move(stranger, arena));
    CHECK_FALSE(core->participant_seat(stranger).is_valid());
    CHECK_FALSE(core->participant_seat_clear(stranger, arena));
    NETW_CHECK_EQ(session.count("scene_local_changed"), 0);
}

} // namespace TestParticipantSeatAnnounceLaws
