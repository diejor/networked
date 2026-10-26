#include "support/fault_log.h"
#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/script.hpp"
#include "netw/api/promise.hpp"
#include "netw/api/simulation.hpp"
#include "netw/game_call.hpp"

#if defined(NETW_TIER_HOSTED)

namespace TestNetwGameCallLaws {

using namespace godot;

constexpr const char *HANDLERS = R"(extends RefCounted

var calls := 0

func pair(_a, _b) -> void:
	calls += 1

func pair_or_one(_a, _b = 1) -> void:
	calls += 1

func any(...rest: Array) -> void:
	calls += 1

func none() -> void:
	calls += 1

func lambda() -> Callable:
	return func(_a): calls += 1
)";

const netw::GameCall PAIR_SITE
    = {netw::sys::SESSION, "The test handler", "func(a, b)", 2};

Ref<RefCounted> handlers() {
    const Ref<Script> shape
        = netw::gd::instantiate_class(StringName("GDScript"));
    shape->set_source_code(String(HANDLERS));
    shape->reload();
    const Ref<RefCounted> made = shape->call("new");
    REQUIRE(made.is_valid());
    return made;
}

Callable method_of(const Ref<RefCounted> &p_on, const char *p_name) {
    return Callable(p_on.ptr(), StringName(p_name));
}

Array args_of(int p_count) {
    Array args;
    for (int at = 0; at < p_count; ++at) {
        args.push_back(at);
    }
    return args;
}

struct AcceptsRow {
    const char *method;
    int count;
    bool accepts;
};

TEST_CASE(
    "[Networked][GameCall] a method accepts a count between its required and "
    "total arguments, a rest parameter accepts any count above that, and a "
    "lambda is never refused before it runs"
) {
    const Ref<RefCounted> on = handlers();
    const AcceptsRow rows[] = {
        {"pair", 2, true},
        {"pair", 1, false},
        {"pair", 3, false},
        {"pair_or_one", 1, true},
        {"pair_or_one", 2, true},
        {"pair_or_one", 0, false},
        {"any", 0, true},
        {"any", 5, true},
    };
    for (const AcceptsRow &row : rows) {
        CAPTURE(row.method);
        CAPTURE(row.count);
        CHECK(
            netw::game_call_accepts(method_of(on, row.method), row.count)
            == row.accepts
        );
    }
    const Callable lambda = on->call(StringName("lambda"));
    CHECK(netw::game_call_accepts(lambda, 3));
}

TEST_CASE(
    "[Networked][GameCall] a call with the wrong arguments does not run and "
    "reports one error for that handler however often it is called"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The test handler could not be called with 1 "
        "arguments, so it did not run. Declare it as func(a, b).",
        1
    );

    const Ref<RefCounted> on = handlers();
    const Callable pair = method_of(on, "pair");

    bool called = true;
    netw::game_call(pair, args_of(1), PAIR_SITE, &called);
    CHECK_FALSE(called);
    netw::game_call(pair, args_of(1), PAIR_SITE);
    netw::game_call(pair, args_of(2), PAIR_SITE, &called);

    CHECK(called);
    NETW_CHECK_EQ(int(on->get(StringName("calls"))), 1);
}

TEST_CASE(
    "[Networked][GameCall] a handler refused when it is declared is not "
    "reported again when it is called"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The test handler takes the wrong arguments, so it "
        "will not run. Declare it as func(a, b).",
        1
    );

    const Ref<RefCounted> on = handlers();
    const Callable wrong = method_of(on, "any");
    const Callable single = Callable(on.ptr(), StringName("lambda"));

    CHECK(netw::game_call_declared(method_of(on, "pair"), PAIR_SITE));
    CHECK(netw::game_call_declared(wrong, PAIR_SITE));
    CHECK_FALSE(netw::game_call_declared(single, PAIR_SITE));
    netw::game_call(single, args_of(2), PAIR_SITE);
}

TEST_CASE(
    "[Networked][GameCall] a catch_error callback that cannot take the code "
    "and detail reports the failure it would otherwise have swallowed"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][session] The NetwPromise.catch_error callback could not be "
        "called with 2 arguments, so it did not run. Declare it as "
        "func(code: Error, detail: String).",
        1
    );

    const Ref<RefCounted> on = handlers();
    Ref<netw::NetwPromise> pending;
    pending.instantiate();
    pending->catch_error(method_of(on, "none"));

    pending->reject(ERR_TIMEOUT, String("late"));

    NETW_CHECK_EQ(int(on->get(StringName("calls"))), 0);
}

TEST_CASE(
    "[Networked][GameCall] a simulation step with the wrong parameters is "
    "reported when it is set, before any tick runs"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][prediction] NetwSimulation.step takes the wrong arguments, "
        "so it will not run. Declare it as func(delta: float, tick: int, "
        "is_fresh: bool) -> void.",
        1
    );

    const Ref<RefCounted> on = handlers();
    Ref<netw::NetwSimulation> simulation;
    simulation.instantiate();

    simulation->set_step(method_of(on, "none"));
}

} // namespace TestNetwGameCallLaws

#endif
