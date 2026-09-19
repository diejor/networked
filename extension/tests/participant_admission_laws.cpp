#include "support/netw_test.h"

#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "support/netw_recorder.h"

namespace TestParticipantAdmissionLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::Recorder;

Ref<netw::NetwPlayer> a_row() {
    Ref<netw::NetwPlayer> row;
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

TEST_CASE(
    "[Networked][Session][Hosted] PA1 admission is a fact about a row, so a "
    "peer the roster never opened cannot be admitted and a peer already "
    "admitted is not admitted a second time"
) {
    Ref<NetwMultiplayer> core = peered_core();

    NETW_CHECK_EQ(core->player_admit(7), false);
    NETW_CHECK_EQ(core->player_admitted_of(7).is_valid(), false);

    core->player_adopt(7, a_row());

    NETW_CHECK_EQ(core->player_admit(7), true);
    NETW_CHECK_EQ(core->player_admit(7), false);
    NETW_CHECK_EQ(core->player_admitted_of(7).is_valid(), true);
}

TEST_CASE(
    "[Networked][Session][Hosted] PA2 a connected peer is a row before it is "
    "a player, so adopting one admits nothing and the two reads answer "
    "different rosters"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const Ref<netw::NetwPlayer> row = a_row();

    core->player_adopt(7, row);

    NETW_CHECK_EQ(core->player_of(7).ptr(), row.ptr());
    NETW_CHECK_EQ(core->player_admitted_of(7).is_valid(), false);
    NETW_CHECK_EQ(core->player_all().size(), 1);
    NETW_CHECK_EQ(core->player_admitted_all().size(), 0);

    core->player_admit(7);

    NETW_CHECK_EQ(core->player_admitted_of(7).ptr(), row.ptr());
    NETW_CHECK_EQ(core->player_admitted_all().size(), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] PA3 the local admitted row is this peer's "
    "and no other, so a session reads its own player without being told "
    "which peer it is"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t mine = core->get_unique_id();
    const Ref<netw::NetwPlayer> ours = a_row();
    core->player_adopt(mine, ours);
    core->player_adopt(mine + 1, a_row());

    NETW_CHECK_EQ(core->player_admitted_local().is_valid(), false);

    core->player_admit(mine + 1);

    NETW_CHECK_EQ(core->player_admitted_local().is_valid(), false);

    core->player_admit(mine);

    NETW_CHECK_EQ(core->player_admitted_local().ptr(), ours.ptr());
}

TEST_CASE(
    "[Networked][Session][Hosted] PA4 admission is earlier than the "
    "announcement, so the join handler running between them reads an "
    "admitted player while no join edge has fired yet"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t mine = core->get_unique_id();
    const Ref<netw::NetwPlayer> ours = a_row();
    core->player_adopt(mine, ours);
    Recorder session(
        core.ptr(),
        Vector<StringName>({
            "player_local_joined",
            "player_joined",
        })
    );

    core->player_admit(mine);

    NETW_CHECK_EQ(core->player_admitted_local().ptr(), ours.ptr());
    NETW_CHECK_EQ(session.count("player_local_joined"), 0);
    NETW_CHECK_EQ(session.count("player_joined"), 0);

    core->player_publish_joined(mine);

    NETW_CHECK_EQ(session.count("player_local_joined"), 1);
    NETW_CHECK_EQ(core->player_admitted_local().ptr(), ours.ptr());
}

TEST_CASE(
    "[Networked][Session][Hosted] PA5 admitted rows answer in peer order and "
    "hold only the admitted, so two reads in one frame agree and an "
    "un-joined observer never appears among the players"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const Ref<netw::NetwPlayer> low = a_row();
    const Ref<netw::NetwPlayer> high = a_row();
    const Ref<netw::NetwPlayer> watching = a_row();
    core->player_adopt(9, high);
    core->player_adopt(5, watching);
    core->player_adopt(2, low);
    core->player_admit(9);
    core->player_admit(2);

    const TypedArray<Object> admitted = core->player_admitted_all();

    REQUIRE(admitted.size() == 2);
    NETW_CHECK_EQ(Object::cast_to<Object>(admitted[0]), low.ptr());
    NETW_CHECK_EQ(Object::cast_to<Object>(admitted[1]), high.ptr());
    NETW_CHECK_EQ(core->player_all().size(), 3);
}

TEST_CASE(
    "[Networked][Session][Hosted] PA6 admission never outlives the row it "
    "describes, so a peer that reconnects into a fresh row is a connected "
    "observer again rather than an admitted player"
) {
    Ref<NetwMultiplayer> core = peered_core();
    const int64_t mine = core->get_unique_id();
    core->player_adopt(mine, a_row());
    core->player_adopt(7, a_row());
    core->player_admit(mine);
    core->player_admit(7);

    core->player_forget(7);
    core->player_adopt(7, a_row());

    NETW_CHECK_EQ(core->player_has(7), true);
    NETW_CHECK_EQ(core->player_admitted_of(7).is_valid(), false);

    core->player_clear();
    core->player_adopt(mine, a_row());

    NETW_CHECK_EQ(core->player_admitted_local().is_valid(), false);
    NETW_CHECK_EQ(core->player_admitted_all().size(), 0);
}

} // namespace TestParticipantAdmissionLaws
