#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"

#include <godot_cpp/classes/scene_multiplayer.hpp>

using namespace godot;

namespace NetwClockLostPongFrameLaws {

namespace {

constexpr int TICKRATE = 30;
constexpr int SEAT_FRAMES = 120;
constexpr int LOSSY_FRAMES = 60;
constexpr int RECOVERY_FRAMES = 60;

struct LostPongEvidence {
    bool driven = false;
    bool client_seated = false;
    bool synchronized_under_loss = false;
    int pongs_under_loss = 0;
    bool synchronized_after_loss = false;
    int frames_to_synchronize = -1;
};

LostPongEvidence &lost_pong_evidence() {
    static LostPongEvidence evidence;
    return evidence;
}

int &client_pongs() {
    static int count = 0;
    return count;
}

void count_client_pong(const Dictionary &) {
    client_pongs() += 1;
}

class ClockLostPongScenario final : public netw_test::FrameScenario {
    int step = 0;
    int frames_in_step = 0;
    Ref<netw::LocalLoopbackSession> link;
    Ref<netw::LocalMultiplayerPeer> client_peer;
    Node *host_branch = nullptr;
    Node *client_branch = nullptr;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> client;

    void enter(int p_step) {
        step = p_step;
        frames_in_step = 0;
    }

    static Node *mount(
        const char *p_name,
        const Ref<netw::NetwMultiplayer> &p_api
    ) {
        Node *branch = memnew(Node);
        branch->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(branch);
        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(branch->get_path());
        p_api->session_set_inner(inner);
        netw::gd::scene_tree()->set_multiplayer(p_api, branch->get_path());
        netw::Netw::configure_clock(branch)->ticks_per_second(TICKRATE);
        p_api->config_settle();
        return branch;
    }

    void open() {
        link.instantiate();
        host.instantiate();
        host_branch = mount("LostPongHost", host);
        host->session_prepare_join(StringName("host"), Array());
        host->NETW_API_VIRTUAL(set_multiplayer_peer)(link->get_server_peer());

        client_peer = link->create_client_peer();
        Ref<netw::LocalLinkConditions> lossy
            = netw::LocalLinkConditions::create(1);
        lossy->set_packet_loss(1.0);
        link->set_link_conditions(client_peer.ptr(), lossy, 1);

        client.instantiate();
        client_branch = mount("LostPongClient", client);
        client->connect(
            StringName("clock_pong_received"),
            callable_mp_static(&count_client_pong)
        );
        client->session_prepare_join(StringName("joiner"), Array());
        client->NETW_API_VIRTUAL(set_multiplayer_peer)(client_peer);
    }

    void pump() {
        link->poll();
        host->NETW_API_VIRTUAL(poll)();
        client->NETW_API_VIRTUAL(poll)();
    }

    static void drop(Node *&r_branch, Ref<netw::NetwMultiplayer> &r_api) {
        if (r_api.is_valid()) {
            r_api->NETW_API_VIRTUAL(set_multiplayer_peer)(
                Ref<MultiplayerPeer>()
            );
        }
        if (r_branch != nullptr) {
            netw::gd::scene_tree()->set_multiplayer(
                Ref<MultiplayerAPI>(),
                r_branch->get_path()
            );
            r_branch->get_parent()->remove_child(r_branch);
            memdelete(r_branch);
            r_branch = nullptr;
        }
        r_api.unref();
    }

public:
    bool advance() override {
        LostPongEvidence &evidence = lost_pong_evidence();
        if (step == 0) {
            open();
            enter(1);
            return true;
        }
        pump();
        frames_in_step += 1;

        switch (step) {
            case 1: {
                if (client->NETW_API_VIRTUAL(get_unique_id)() > 1
                    && client->is_online()) {
                    evidence.client_seated = true;
                    enter(2);
                } else if (frames_in_step > SEAT_FRAMES) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                if (frames_in_step < LOSSY_FRAMES) {
                    return true;
                }
                evidence.synchronized_under_loss
                    = client->clock_is_synchronized();
                evidence.pongs_under_loss = client_pongs();
                link->clear_link_conditions(client_peer.ptr(), 1);
                enter(3);
                return true;
            }
            case 3: {
                if (client->clock_is_synchronized()) {
                    evidence.synchronized_after_loss = true;
                    evidence.frames_to_synchronize = frames_in_step;
                    enter(99);
                } else if (frames_in_step > RECOVERY_FRAMES) {
                    enter(99);
                }
                return true;
            }
            default: {
                drop(client_branch, client);
                drop(host_branch, host);
                client_peer.unref();
                link->reset();
                link.unref();
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(ClockLostPongScenario, clock_lost_pong_scenario);

} // namespace

TEST_CASE(
    "[Networked][Clock][Frame] LP1 a client whose first pong is lost keeps "
    "pinging until one answers, so a single dropped datagram never leaves "
    "the clock unsynchronized for the rest of the session"
) {
    const LostPongEvidence &evidence = lost_pong_evidence();
    REQUIRE(evidence.driven);
    CHECK(evidence.client_seated);
    CHECK_FALSE(evidence.synchronized_under_loss);
    NETW_CHECK_EQ(evidence.pongs_under_loss, 0);
    CHECK(evidence.synchronized_after_loss);
    NETW_CHECK_EQ(int(evidence.frames_to_synchronize <= 30), 1);
}

} // namespace NetwClockLostPongFrameLaws
