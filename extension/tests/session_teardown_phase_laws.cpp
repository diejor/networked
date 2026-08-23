#include "support/netw_test.h"

#include <memory>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "godot/variant.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/session_core.hpp"
#include "support/netw_recorder.h"

namespace TestNetwSessionTeardownPhaseLaws {

using namespace godot;
using netw::NetwMultiplayerCore;
using netw::SessionCore;
using netw_test::Recorder;

const char *ENDED = "session_ended";
const char *RECLAIMED = "session_reclaimed";

enum PhaseAct {
    ACT_NOTE,
    ACT_OBSERVE,
    ACT_FREE,
};

struct PhaseLog {
    Vector<StringName> order;
    ObjectID watched;
    Vector<int> watched_was_live;
};

class PhaseSink final : public CallableCustom {
    std::shared_ptr<PhaseLog> log;
    StringName tag;
    PhaseAct act;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    PhaseSink(
        const std::shared_ptr<PhaseLog> &p_log,
        const StringName &p_tag,
        PhaseAct p_act,
        const Object *p_anchor
    )
        : log(p_log), tag(p_tag), act(p_act),
          anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("PhaseSink::") + String(tag);
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &PhaseSink::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &PhaseSink::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **,
        int,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        log->order.push_back(tag);
        Object *held = netw::gd::object_of(log->watched);
        if (act == ACT_OBSERVE) {
            log->watched_was_live.push_back(held != nullptr ? 1 : 0);
        } else if (act == ACT_FREE && held != nullptr) {
            memdelete(held);
        }
        r_return_value = Variant();
        netw::gd::call_ok(r_call_error);
    }
};

Ref<NetwMultiplayerCore> online() {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    core->session_plane().transition(SessionCore::STATE_CONNECTING);
    core->session_plane().transition(SessionCore::STATE_ONLINE);
    return core;
}

Callable sink(
    const std::shared_ptr<PhaseLog> &p_log,
    const char *p_tag,
    PhaseAct p_act,
    const Object *p_anchor
) {
    return Callable(
        memnew(PhaseSink(p_log, StringName(p_tag), p_act, p_anchor))
    );
}

Vector<StringName> both_phases_reversed() {
    Vector<StringName> watched;
    watched.push_back(StringName(RECLAIMED));
    watched.push_back(StringName(ENDED));
    return watched;
}

TEST_CASE(
    "[Networked][Session][Hosted] TP1 the emitter decides which teardown "
    "phase runs first, so a listener connected to the reclaim ahead of every "
    "announcement listener still runs after all of them"
) {
    Ref<NetwMultiplayerCore> core = online();
    const Recorder heard(core.ptr(), both_phases_reversed());

    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);

    const Vector<StringName> order = heard.order();
    REQUIRE(order.size() == 2);
    CHECK(order[0] == StringName(ENDED));
    CHECK(order[1] == StringName(RECLAIMED));
}

TEST_CASE(
    "[Networked][Session][Hosted] TP2 a node a reclaim listener frees is "
    "still live for every announcement listener, which is the whole reason "
    "the phases are two"
) {
    Ref<NetwMultiplayerCore> core = online();
    Node *stage = memnew(Node);
    const std::shared_ptr<PhaseLog> log = std::make_shared<PhaseLog>();
    log->watched = netw::gd::instance_id(stage);
    core->connect(ENDED, sink(log, "observe", ACT_OBSERVE, core.ptr()));
    core->connect(RECLAIMED, sink(log, "free", ACT_FREE, core.ptr()));

    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);

    REQUIRE(log->order.size() == 2);
    CHECK(log->order[0] == StringName("observe"));
    CHECK(log->order[1] == StringName("free"));
    REQUIRE(log->watched_was_live.size() == 1);
    NETW_CHECK_EQ(log->watched_was_live[0], 1);
    CHECK(netw::gd::object_of(log->watched) == nullptr);
}

TEST_CASE(
    "[Networked][Session][Hosted] TP3 freeing on the announcement hands the "
    "next announcement listener a node that no longer exists, and connecting "
    "first is what buys that, so connect order is no defense"
) {
    Ref<NetwMultiplayerCore> core = online();
    Node *stage = memnew(Node);
    const std::shared_ptr<PhaseLog> log = std::make_shared<PhaseLog>();
    log->watched = netw::gd::instance_id(stage);
    core->connect(ENDED, sink(log, "free", ACT_FREE, core.ptr()));
    core->connect(ENDED, sink(log, "observe", ACT_OBSERVE, core.ptr()));

    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);

    REQUIRE(log->order.size() == 2);
    CHECK(log->order[0] == StringName("free"));
    REQUIRE(log->watched_was_live.size() == 1);
    NETW_CHECK_EQ(log->watched_was_live[0], 0);
}

TEST_CASE(
    "[Networked][Session][Hosted] TP4 the reclaim rides the announcement's "
    "own edge, so it fires once per ending and never for a bring-up that was "
    "not live"
) {
    Ref<NetwMultiplayerCore> failed;
    failed.instantiate();
    const Recorder unlived(failed.ptr(), both_phases_reversed());
    failed->session_plane().transition(SessionCore::STATE_CONNECTING);
    failed->session_plane().transition(SessionCore::STATE_OFFLINE);

    NETW_CHECK_EQ(unlived.count(StringName(ENDED)), 0);
    NETW_CHECK_EQ(unlived.count(StringName(RECLAIMED)), 0);

    Ref<NetwMultiplayerCore> core = online();
    const Recorder heard(core.ptr(), both_phases_reversed());
    core->session_plane().transition(SessionCore::STATE_ONLINE);

    NETW_CHECK_EQ(heard.count(StringName(RECLAIMED)), 0);

    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);
    core->session_plane().transition(SessionCore::STATE_DISCONNECTING);

    NETW_CHECK_EQ(heard.count(StringName(ENDED)), 1);
    NETW_CHECK_EQ(heard.count(StringName(RECLAIMED)), 1);
}

TEST_CASE(
    "[Networked][Session][Hosted] TP5 republishing the phases through the "
    "session shell preserves them, so a listener on the shell's announcement "
    "reads a node the machine's reclaim listener has not freed yet"
) {
    Ref<NetwMultiplayerCore> core;
    core.instantiate();
    SessionCore &machine = core->session_plane();
    machine.transition(SessionCore::STATE_CONNECTING);
    machine.transition(SessionCore::STATE_ONLINE);

    Node *stage = memnew(Node);
    const std::shared_ptr<PhaseLog> log = std::make_shared<PhaseLog>();
    log->watched = netw::gd::instance_id(stage);
    core->connect(ENDED, sink(log, "observe", ACT_OBSERVE, core.ptr()));
    core->connect(RECLAIMED, sink(log, "free", ACT_FREE, core.ptr()));
    const Recorder heard(core.ptr(), both_phases_reversed());

    machine.transition(SessionCore::STATE_DISCONNECTING);

    const Vector<StringName> order = heard.order();
    REQUIRE(order.size() == 2);
    CHECK(order[0] == StringName(ENDED));
    CHECK(order[1] == StringName(RECLAIMED));
    REQUIRE(log->watched_was_live.size() == 1);
    NETW_CHECK_EQ(log->watched_was_live[0], 1);
    CHECK(netw::gd::object_of(log->watched) == nullptr);
}

} // namespace TestNetwSessionTeardownPhaseLaws
