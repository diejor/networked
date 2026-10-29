#include "support/frame_drive.h"
#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/minted_script.h"
#include "support/sim_stand.h"

#include "godot/node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/session.hpp"

using namespace godot;

namespace TestRequestAwaitFrameLaws {

using netw::NetwMultiplayer;
using netw::NetwPromise;
using netw::NetwSession;

constexpr const char *PROBE_ID = "request_await_probe";
constexpr const char *PROBE_PATH = "res://tests/support/awaiting_rpc.gd";
constexpr int TICKRATE = 30;
constexpr int AWAIT_FRAMES = 6;
constexpr int64_t AWAITED_VALUE = 42;
constexpr int STALE_FRAMES = 8;
constexpr int64_t STALE_VALUE = 777;
constexpr int FRESH_FRAMES = 20;
constexpr int64_t FRESH_VALUE = 888;
constexpr int LOSE_AT = 2;
constexpr int TRANSFER_PUMPS = 6;
constexpr int LAST_FRAME = 40;

Script *probe_script = nullptr;

Node *build_probe(const Variant &p_name) {
    if (probe_script == nullptr) {
        return nullptr;
    }
    Node *made = Object::cast_to<Node>(probe_script->call("new"));
    if (made != nullptr) {
        made->set_name(String(p_name));
    }
    return made;
}

struct AwaitEvidence {
    bool driven = false;
    bool seated = false;
    bool lost = false;
    bool suspended_at_loss = false;
    bool stale_finished = false;
    bool completed = false;
    int result_type = -1;
    int64_t result = -1;
    int asked_frame = -1;
    int settled_frame = -1;
};

AwaitEvidence &evidence_of(bool p_transfers) {
    static AwaitEvidence settles;
    static AwaitEvidence transfers;
    return p_transfers ? transfers : settles;
}

class AwaitScenario final : public netw_test::FrameScenario {
    const bool transfers;
    int frame = 0;
    bool done = false;
    netw_test::SimStand *stand = nullptr;
    Ref<Script> script;
    int route = 0;
    int asker = -1;
    int answerer = 0;
    Ref<NetwPromise> stale;
    Ref<NetwPromise> asked;

    AwaitEvidence &evidence() const {
        return evidence_of(transfers);
    }

    Array finished() const {
        Node *probe = stand->node_at(answerer, route);
        return probe != nullptr ? Array(probe->get("finished")) : Array();
    }

    Ref<NetwPromise> ask(int p_from, int p_frames, int64_t p_value) {
        Array args;
        args.push_back(p_frames);
        args.push_back(p_value);
        return stand->session(p_from)->rpc_request_call(
            stand->session(answerer)->get_unique_id(),
            Callable(stand->node_at(p_from, route), StringName("slow")),
            args
        );
    }

    bool open() {
        script = netw_test::script_from(PROBE_PATH);
        if (script.is_null()) {
            return false;
        }
        probe_script = script.ptr();
        const int clients = transfers ? 2 : 1;
        stand = new netw_test::SimStand(clients, transfers);
        if (!stand->ready()) {
            return false;
        }
        stand->teach(StringName(PROBE_ID), callable_mp_static(&build_probe));
        stand->mount();
        Node *arena = stand->arena();
        stand->arm(TICKRATE);
        for (int at = -1; at < clients; ++at) {
            if (transfers) {
                stand->session(at)->transfer_set_mode(
                    NetwSession::TRANSFER_CONTINUE
                );
            }
        }
        stand->session(-1)->session_submit_join(StringName("seat0"), Array());
        stand->pump(4 * (clients + 1));
        NetwMultiplayer *server = stand->session(-1);
        Array args;
        args.push_back(String("Probe"));
        const RID made
            = server->spawn_registered(StringName(PROBE_ID), args, nullptr);
        Node *built = server->entity_get_node(made);
        if (built == nullptr || arena == nullptr) {
            return false;
        }
        arena->add_child(built);
        stand->pump(8);
        route = int(server->entity_get_route(made));
        if (transfers) {
            asker = stand->peer_id(0) < stand->peer_id(1) ? 0 : 1;
            answerer = 1 - asker;
        }
        if (stand->node_at(answerer, route) == nullptr
            || stand->node_at(asker, route) == nullptr) {
            return false;
        }
        evidence().seated = true;
        return true;
    }

    void lose_server() {
        AwaitEvidence &seen = evidence();
        seen.suspended_at_loss = finished().is_empty();
        seen.lost = stand->loopback()->lose_server() == OK;
        stand->pump(TRANSFER_PUMPS);
        asked = ask(asker, FRESH_FRAMES, FRESH_VALUE);
        seen.asked_frame = frame;
    }

    void read() {
        AwaitEvidence &seen = evidence();
        seen.completed = asked.is_valid() && asked->get_is_completed();
        const Variant result
            = asked.is_valid() ? asked->get_result() : Variant();
        seen.result_type = int(result.get_type());
        seen.result = result.get_type() == Variant::INT ? int64_t(result) : -1;
        seen.stale_finished = finished().has(STALE_VALUE);
    }

    void close() {
        evidence().driven = true;
        stale = Ref<NetwPromise>();
        asked = Ref<NetwPromise>();
        delete stand;
        stand = nullptr;
        probe_script = nullptr;
        script = Ref<Script>();
    }

public:
    explicit AwaitScenario(bool p_transfers) : transfers(p_transfers) {
    }

    bool advance() override {
        if (netw::gd::scene_root() == nullptr || done) {
            return false;
        }
        if (frame == 0) {
            if (!open()) {
                close();
                done = true;
                return false;
            }
            if (transfers) {
                stale = ask(-1, STALE_FRAMES, STALE_VALUE);
            } else {
                asked = ask(-1, AWAIT_FRAMES, AWAITED_VALUE);
                evidence().asked_frame = frame;
            }
        }
        if (transfers && frame == LOSE_AT) {
            lose_server();
        }
        AwaitEvidence &seen = evidence();
        if (seen.settled_frame < 0 && asked.is_valid()
            && asked->get_is_settled()) {
            seen.settled_frame = frame;
        }
        if (frame >= LAST_FRAME) {
            read();
            close();
            done = true;
            return false;
        }
        stand->step_ticks(1);
        ++frame;
        return true;
    }
};

NETW_FRAME_SCENARIO(AwaitScenario, request_await_settles, false);
NETW_FRAME_SCENARIO(AwaitScenario, request_await_transfers, true);

TEST_CASE(
    "[Networked][Rpc][Frame] RQ1 a requested method that awaits replies "
    "with its return value once it returns, not with the suspended call"
) {
    const AwaitEvidence &seen = evidence_of(false);
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    CHECK(seen.completed);
    NETW_CHECK_EQ(seen.result_type, int(Variant::INT));
    NETW_CHECK_EQ(seen.result, AWAITED_VALUE);
    NETW_CHECK_GE(seen.settled_frame, seen.asked_frame + AWAIT_FRAMES);
}

TEST_CASE(
    "[Networked][Rpc][Frame] RQ2 a method still awaiting when the server is "
    "lost sends no reply, so it cannot settle the successor's own request"
) {
    const AwaitEvidence &seen = evidence_of(true);
    REQUIRE(seen.driven);
    REQUIRE(seen.seated);
    REQUIRE(seen.lost);
    REQUIRE(seen.suspended_at_loss);
    REQUIRE(seen.stale_finished);
    CHECK(seen.completed);
    NETW_CHECK_EQ(seen.result, FRESH_VALUE);
    NETW_CHECK_GE(seen.settled_frame, seen.asked_frame + FRESH_FRAMES);
}

} // namespace TestRequestAwaitFrameLaws

#endif
