#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/engine.hpp"
#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/time.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/netw_multiplayer.hpp"

#include <godot_cpp/classes/scene_multiplayer.hpp>

using namespace godot;

namespace NetwClockPumpFrameLaws {

namespace {

constexpr int PORT_RANGE_START = 30400;
constexpr int PORT_RANGE_SIZE = 100;
constexpr int64_t STEP_BUDGET_MSEC = 8000;
constexpr int TICKRATE = 30;
constexpr int SCALED_FRAMES = 60;

struct PumpEvidence {
    bool driven = false;
    int port = 0;
    bool host_online = false;
    bool client_seated = false;
    bool client_synchronized = false;
    int client_pongs_after_sync = 0;
    int host_pongs = 0;
    int64_t client_ticks_at_sync = 0;
    int64_t client_ticks_after_run = 0;
    int frames_after_sync = 0;
    int64_t polls_without_a_tick = 0;
    bool resynchronized = false;
    int64_t configured_tickrate_after_resync = 0;
    int64_t full_speed_ticks = 0;
    int64_t half_speed_ticks = 0;
    bool client_clock_configured = false;
    bool client_rooted = false;
    bool client_manual = false;
};

PumpEvidence &pump_evidence() {
    static PumpEvidence evidence;
    return evidence;
}

int &client_pong_count() {
    static int count = 0;
    return count;
}

int &host_pong_count() {
    static int count = 0;
    return count;
}

void count_client_pong(const Dictionary &) {
    client_pong_count() += 1;
}

void count_host_pong(const Dictionary &) {
    host_pong_count() += 1;
}

class ClockPumpScenario final : public netw_test::FrameScenario {
    int step = 0;
    int64_t step_opened = 0;
    int frames_in_step = 0;
    Node *host_branch = nullptr;
    Node *client_branch = nullptr;
    Ref<netw::NetwMultiplayer> host;
    Ref<netw::NetwMultiplayer> client;
    Ref<MultiplayerPeer> client_peer;

    static int64_t now_msec() {
        return int64_t(Time::get_singleton()->get_ticks_msec());
    }

    bool over_budget() const {
        return now_msec() - step_opened > STEP_BUDGET_MSEC;
    }

    void enter(int p_step) {
        step = p_step;
        step_opened = now_msec();
        frames_in_step = 0;
    }

    static Ref<MultiplayerPeer> enet_peer() {
        return Ref<MultiplayerPeer>(
            ClassDB::instantiate(StringName("ENetMultiplayerPeer"))
        );
    }

    Node *mount(const char *p_name, const Ref<netw::NetwMultiplayer> &p_api) {
        Node *branch = memnew(Node);
        branch->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(branch);
        Ref<SceneMultiplayer> inner;
        inner.instantiate();
        inner->set_root_path(branch->get_path());
        p_api->session_set_inner(inner);
        netw::gd::scene_tree()->set_multiplayer(p_api, branch->get_path());
        return branch;
    }

    void declare_clock(Node *p_branch) {
        netw::Netw::configure_clock(p_branch)->tickrate(TICKRATE);
    }

    int open_host() {
        for (int candidate = PORT_RANGE_START;
             candidate < PORT_RANGE_START + PORT_RANGE_SIZE;
             candidate++) {
            const Ref<MultiplayerPeer> peer = enet_peer();
            if (peer.is_null()) {
                return 0;
            }
            if (Error(int(peer->call(StringName("create_server"), candidate)))
                != OK) {
                continue;
            }
            host.instantiate();
            host_branch = mount("ClockPumpHost", host);
            declare_clock(host_branch);
            host->config_settle();
            host->connect(
                StringName("clock_pong_received"),
                callable_mp_static(&count_host_pong)
            );
            host->session_prepare_join(StringName("host"), Array());
            host->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
            return candidate;
        }
        return 0;
    }

    bool open_client(int p_port) {
        const Ref<MultiplayerPeer> peer = enet_peer();
        if (peer.is_null()) {
            return false;
        }
        if (Error(
                int(peer->call(
                    StringName("create_client"),
                    String("127.0.0.1"),
                    p_port
                ))
            )
            != OK) {
            return false;
        }
        client_peer = peer;
        client.instantiate();
        client_branch = mount("ClockPumpClient", client);
        declare_clock(client_branch);
        client->config_settle();
        client->connect(
            StringName("clock_pong_received"),
            callable_mp_static(&count_client_pong)
        );
        client->session_prepare_join(StringName("joiner"), Array());
        client->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
        return true;
    }

    void pump() {
        if (host.is_valid()) {
            host->NETW_API_VIRTUAL(poll)();
        }
        if (client.is_valid()) {
            client->NETW_API_VIRTUAL(poll)();
        }
    }

    void drop(Node *&r_branch, Ref<netw::NetwMultiplayer> &r_api) {
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
        client_peer.unref();
    }

public:
    bool advance() override {
        PumpEvidence &evidence = pump_evidence();
        pump();
        frames_in_step += 1;

        switch (step) {
            case 0: {
                evidence.port = open_host();
                if (evidence.port == 0) {
                    evidence.driven = true;
                    return false;
                }
                enter(1);
                return true;
            }
            case 1: {
                if (host->is_online()) {
                    evidence.host_online = true;
                    if (!open_client(evidence.port)) {
                        enter(99);
                        return true;
                    }
                    enter(2);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 2: {
                evidence.client_clock_configured
                    = client->clock_engine().get_configured();
                evidence.client_rooted = client->session_root() != nullptr;
                evidence.client_manual
                    = client->clock_engine().get_manual_tick();
                if (client->NETW_API_VIRTUAL(get_unique_id)() > 1) {
                    evidence.client_seated = true;
                    enter(3);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 3: {
                if (client->clock_is_synchronized()) {
                    evidence.client_synchronized = true;
                    evidence.client_ticks_at_sync = client->clock_get_tick();
                    client_pong_count() = 0;
                    enter(4);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            case 4: {
                const int64_t before = client->clock_get_tick();
                client->NETW_API_VIRTUAL(poll)();
                client->NETW_API_VIRTUAL(poll)();
                if (client->clock_get_tick() == before) {
                    evidence.polls_without_a_tick += 2;
                }
                if (client_pong_count() >= 3 || over_budget()) {
                    evidence.client_pongs_after_sync = client_pong_count();
                    evidence.host_pongs = host_pong_count();
                    evidence.client_ticks_after_run = client->clock_get_tick();
                    evidence.frames_after_sync = frames_in_step;
                    enter(5);
                }
                return true;
            }
            case 5: {
                evidence.full_speed_ticks = client->clock_get_tick();
                Engine::get_singleton()->set_time_scale(0.5);
                enter(6);
                return true;
            }
            case 6: {
                if (frames_in_step < SCALED_FRAMES) {
                    return true;
                }
                evidence.half_speed_ticks
                    = client->clock_get_tick() - evidence.full_speed_ticks;
                Engine::get_singleton()->set_time_scale(1.0);
                enter(7);
                return true;
            }
            case 7: {
                if (frames_in_step < SCALED_FRAMES) {
                    return true;
                }
                evidence.full_speed_ticks = client->clock_get_tick()
                    - evidence.full_speed_ticks - evidence.half_speed_ticks;
                client->NETW_API_VIRTUAL(set_multiplayer_peer)(
                    Ref<MultiplayerPeer>()
                );
                enter(8);
                return true;
            }
            case 8: {
                client_peer->close();
                if (Error(
                        int(client_peer->call(
                            StringName("create_client"),
                            String("127.0.0.1"),
                            evidence.port
                        ))
                    )
                    != OK) {
                    enter(99);
                    return true;
                }
                client->session_prepare_join(StringName("joiner"), Array());
                client->NETW_API_VIRTUAL(set_multiplayer_peer)(client_peer);
                enter(9);
                return true;
            }
            case 9: {
                if (client->clock_is_synchronized()) {
                    evidence.resynchronized = true;
                    evidence.configured_tickrate_after_resync
                        = int64_t(client->clock_get_param(
                            netw::NetwMultiplayer::CLOCK_PARAM_TICKRATE
                        ));
                    enter(99);
                } else if (over_budget()) {
                    enter(99);
                }
                return true;
            }
            default: {
                Engine::get_singleton()->set_time_scale(1.0);
                drop(client_branch, client);
                drop(host_branch, host);
                evidence.driven = true;
                return false;
            }
        }
    }
};

NETW_FRAME_SCENARIO(ClockPumpScenario, clock_pump_scenario);

} // namespace

TEST_CASE(
    "[Networked][Clock][Frame] CP1 the clock advances on the SceneTree's own "
    "physics frame with nothing calling a step verb, so a session that "
    "declared a clock is ticking before anything asks it to"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    CHECK(evidence.host_online);
    CHECK(evidence.client_seated);
    CHECK(evidence.client_clock_configured);
    CHECK(evidence.client_rooted);
    CHECK_FALSE(evidence.client_manual);
    CHECK(evidence.client_synchronized);
    NETW_CHECK_EQ(int(evidence.client_ticks_after_run > 0), 1);
    NETW_CHECK_EQ(
        int(evidence.client_ticks_after_run > evidence.client_ticks_at_sync),
        1
    );
}

TEST_CASE(
    "[Networked][Clock][Frame] CP2 an extra poll advances no tick, because "
    "the physics frame is the ONE automatic pump and a session polled twice "
    "in a frame is not a session running twice as fast"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    NETW_CHECK_EQ(int(evidence.polls_without_a_tick > 0), 1);
}

TEST_CASE(
    "[Networked][Clock][Frame] CP3 periodic pings keep arriving after the "
    "initial synchronization, so a long session keeps measuring its link "
    "rather than trusting the first sample it ever took"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    NETW_CHECK_EQ(int(evidence.client_pongs_after_sync >= 3), 1);
}

TEST_CASE(
    "[Networked][Clock][Frame] CP4 a host never pings itself, because there "
    "is no link between a host and its own clock to measure"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    NETW_CHECK_EQ(int(evidence.host_pongs), 0);
}

TEST_CASE(
    "[Networked][Clock][Frame] CP5 half the engine's time scale advances "
    "about half the ticks over the same frame count, because the pump reads "
    "the frame's ACTUAL delta rather than the inverse of the physics rate"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    NETW_CHECK_EQ(int(evidence.full_speed_ticks > 0), 1);
    NETW_CHECK_EQ(
        int(evidence.half_speed_ticks * 2 <= evidence.full_speed_ticks * 3 / 2),
        1
    );
}

TEST_CASE(
    "[Networked][Clock][Frame] CP6 a reconnect re-synchronizes on a fresh "
    "transport without reopening initialization, so the tickrate the session "
    "consumed once is the tickrate it comes back on"
) {
    const PumpEvidence &evidence = pump_evidence();
    REQUIRE(evidence.driven);
    if (evidence.port == 0) {
        return;
    }
    CHECK(evidence.resynchronized);
    NETW_CHECK_EQ(int(evidence.configured_tickrate_after_resync), TICKRATE);
}

} // namespace NetwClockPumpFrameLaws
