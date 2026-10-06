#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <memory>

#include "godot/callable.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "godot/script.hpp"
#include "godot/variant.hpp"
#include "netw/api/loopback.hpp"
#include "support/fault_log.h"
#include "support/minted_script.h"

namespace TestTransferKitLaws {

using namespace godot;
using netw::LocalLoopbackSession;
using netw::LocalMultiplayerPeer;

const char *CONTRACT_PATH
    = "res://addons/networked_test/transfer/transfer_contract.gd";

const char *DRIVER_SOURCE = R"(extends NetwTransferDriver

var build: Callable
var lose: Callable
var joiner: Callable


func make_endpoints(count: int) -> Array[MultiplayerPeer]:
	var made: Array[MultiplayerPeer] = []
	made.assign(build.call(count))
	return made


func lose_server() -> void:
	lose.call()


func join() -> MultiplayerPeer:
	return joiner.call()
)";

struct Loop {
    Ref<LocalLoopbackSession> session;
    bool checks_terms = true;

    Array build(int p_count) {
        if (session.is_valid()) {
            session->reset();
        }
        session.instantiate();
        session->enable_server_transfer();
        session->checks_terms = checks_terms;
        Array endpoints;
        endpoints.push_back(session->get_server_peer());
        for (int at = 1; at < p_count; ++at) {
            endpoints.push_back(session->create_client_peer());
        }
        return endpoints;
    }

    Ref<LocalMultiplayerPeer> join() {
        const Ref<LocalMultiplayerPeer> joined = session->create_client_peer();
        joined->NETW_PEER_VIRTUAL(poll)();        return joined;
    }
};

enum Verb {
    VERB_BUILD,
    VERB_LOSE,
    VERB_JOIN,
};

class LoopCall final : public CallableCustom {
    std::shared_ptr<Loop> loop;
    Verb verb;
    ObjectID anchor;

    static bool same(const CallableCustom *a, const CallableCustom *b) {
        return a == b;
    }

    static bool before(const CallableCustom *a, const CallableCustom *b) {
        return a < b;
    }

public:
    LoopCall(
        const std::shared_ptr<Loop> &p_loop,
        Verb p_verb,
        const Object *p_anchor
    )
        : loop(p_loop), verb(p_verb),
          anchor(netw::gd::instance_id(p_anchor)) {
    }

    uint32_t hash() const override {
        return uint32_t(uintptr_t(this));
    }

    String get_as_text() const override {
        return String("LoopCall");
    }

    CompareEqualFunc get_compare_equal_func() const override {
        return &LoopCall::same;
    }

    CompareLessFunc get_compare_less_func() const override {
        return &LoopCall::before;
    }

    ObjectID get_object() const override {
        return anchor;
    }

    void call(
        const Variant **p_arguments,
        int p_argcount,
        Variant &r_return_value,
        netw::gd::CallError &r_call_error
    ) const override {
        if (verb == VERB_BUILD && p_argcount == 1) {
            r_return_value = loop->build(int(*p_arguments[0]));
        } else if (verb == VERB_LOSE) {
            r_return_value = int(loop->session->lose_server());
        } else if (verb == VERB_JOIN) {
            r_return_value = loop->join();
        }
        netw::gd::call_ok(r_call_error);
    }
};

struct Kit {
    std::shared_ptr<Loop> loop = std::make_shared<Loop>();
    Node *parent = memnew(Node);
    Variant contract;

    explicit Kit(bool p_checks_terms) {
        loop->checks_terms = p_checks_terms;
        parent->set_name("TransferKit");
        netw::gd::scene_root()->add_child(parent);
    }

    ~Kit() {
        contract = Variant();
        netw::gd::scene_root()->remove_child(parent);
        memdelete(parent);
        if (loop->session.is_valid()) {
            loop->session->reset();
        }
    }

    Kit(const Kit &) = delete;
    Kit &operator=(const Kit &) = delete;

    Callable verb(Verb p_verb) const {
        return Callable(memnew(LoopCall(loop, p_verb, parent)));
    }

    bool open() {
        const Ref<Script> driver_script
            = netw_test::minted_script(DRIVER_SOURCE);
        const Ref<Script> contract_script
            = netw::gd::load_script(String(CONTRACT_PATH));
        if (driver_script.is_null() || contract_script.is_null()) {
            return false;
        }
        const Variant driver = driver_script->call("new");
        Object *made = driver;
        made->set("build", verb(VERB_BUILD));
        made->set("lose", verb(VERB_LOSE));
        made->set("joiner", verb(VERB_JOIN));
        contract = contract_script->call("new", driver, parent);
        return contract.get_type() == Variant::OBJECT;
    }

    Variant ask(const char *p_method) {
        Object *judge = contract;
        return judge->call(StringName(p_method));
    }
};

TEST_CASE(
    "[Networked][Transfer][Kit][SceneTree] the loopback meets every law the "
    "transfer kit judges, on loss and on hand off, with a joiner arriving "
    "while the survivors install"
) {
    Kit kit(true);
    NETW_REQUIRE_EQ(int(kit.open()), 1);

    const Array failures = kit.ask("run");

    NETW_CHECK_EQ(failures.size(), 0);
    for (int at = 0; at < failures.size(); ++at) {
        const CharString text = String(failures[at]).utf8();
        NETW_FORMAT_TEXT(failure, text.get_data());
        CAPTURE(failure);
        CHECK(failures.is_empty());
    }
    Object *judge = kit.contract;
    const Array judged = judge->get("judged");
    for (const char *label :
         {"C1 x lost",
          "C4 x lost",
          "C7 x lost",
          "C8 x lost",
          "inner x lost",
          "C1 x handed_on",
          "C4 x handed_on",
          "C7 x handed_on",
          "C8 x handed_on",
          "inner x handed_on"}) {
        NETW_FORMAT_TEXT(law, label);
        CAPTURE(law);
        CHECK(judged.has(String(label)));
    }
}

TEST_CASE(
    "[Networked][Transfer][Kit][SceneTree] a loopback that reads packets "
    "from any term breaks the cut, and the kit names C4"
) {
    Kit kit(false);
    NETW_REQUIRE_EQ(int(kit.open()), 1);

    const netw_test::FaultWatch faults;
    kit.ask("run");

    const PackedStringArray failed = kit.ask("failed_laws");
    CHECK(failed.has(String("C4")));
    const int64_t uncached = faults.errors_containing("not found in cache");
    const int64_t unfound
        = faults.errors_containing("Requested node was not found");
    const int64_t stranger
        = faults.errors_containing("connected_peers.has(sender)");
    NETW_CHECK_GT(uncached, 0);
    NETW_CHECK_GT(unfound, 0);
    NETW_CHECK_GT(stranger, 0);
    NETW_CHECK_EQ(faults.errors(), uncached + unfound + stranger);
}

} // namespace TestTransferKitLaws

#endif
