#include "support/fault_log.h"

#include "support/netw_test.h"

#include "netw/interest/decl.hpp"

namespace TestDecl {

using namespace godot;
using netw::interest::Decl;

constexpr int LEAVE_HIDE = Decl::LEAVE_HIDE;
constexpr int RETAIN = Decl::LEAVE_RETAIN;
constexpr int LEAVE_CUSTOM = Decl::LEAVE_CUSTOM;
constexpr int HIDE = Decl::PERCEPTION_HIDE;
constexpr int SHOW = Decl::PERCEPTION_SHOW;
constexpr int PERCEPTION_CUSTOM = Decl::PERCEPTION_CUSTOM;

Ref<RefCounted> callable_holder() {
    Ref<RefCounted> holder;
    holder.instantiate();
    return holder;
}

TEST_CASE(
    "[Networked][Interest][Hosted] a declared label joins once and keeps its "
    "declaration order"
) {
    Decl decl;

    CHECK(decl.join(StringName("arena")));
    CHECK(!decl.join(StringName("arena")));
    CHECK(decl.join(StringName("stealth")));

    Array expected;
    expected.push_back(StringName("arena"));
    expected.push_back(StringName("stealth"));
    CHECK(decl.labels() == expected);
    CHECK(decl.has_label(StringName("arena")));
    CHECK(!decl.has_label(StringName("nowhere")));

    CHECK(decl.leave(StringName("arena")));
    CHECK(!decl.leave(StringName("arena")));
    NETW_CHECK_EQ(decl.labels().size(), 1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an override resolves before the layer "
    "default, and only for the layer it names"
) {
    Decl decl;

    const StringName stealth("stealth");
    const StringName arena("arena");

    NETW_CHECK_EQ(decl.leave_policy_for(stealth, LEAVE_HIDE), LEAVE_HIDE);
    CHECK(decl.set_leave_policy(stealth, RETAIN, Callable()));

    NETW_CHECK_EQ(decl.leave_policy_for(stealth, LEAVE_HIDE), RETAIN);
    NETW_CHECK_EQ(decl.leave_policy_for(arena, LEAVE_HIDE), LEAVE_HIDE);

    CHECK(decl.set_perception_policy(stealth, SHOW, Callable()));
    NETW_CHECK_EQ(decl.perception_policy_for(stealth, HIDE), SHOW);
    NETW_CHECK_EQ(decl.perception_policy_for(arena, HIDE), HIDE);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a leaving label keeps the overrides "
    "declared for it"
) {
    Decl decl;
    const StringName stealth("stealth");
    decl.join(stealth);
    decl.set_leave_policy(stealth, RETAIN, Callable());

    decl.leave(stealth);

    CHECK(!decl.has_label(stealth));
    NETW_CHECK_EQ(decl.leave_policy_for(stealth, LEAVE_HIDE), RETAIN);
}

TEST_CASE(
    "[Networked][Interest][Hosted] CUSTOM is the only policy that carries a "
    "callback, and the only one that may"
) {
    Decl decl;
    const Ref<RefCounted> holder = callable_holder();
    const StringName stealth("stealth");
    const Callable callback(holder.ptr(), StringName("get_class"));

    CHECK(decl.set_leave_policy(stealth, LEAVE_CUSTOM, callback));
    CHECK(decl.custom_leave_for(stealth) == callback);

    CHECK(decl.set_leave_policy(stealth, RETAIN, Callable()));
    CHECK(!decl.custom_leave_for(stealth).is_valid());

    CHECK(decl.set_perception_policy(stealth, PERCEPTION_CUSTOM, callback));
    CHECK(decl.custom_perception_for(stealth) == callback);

    CHECK(decl.set_perception_policy(stealth, SHOW, Callable()));
    CHECK(!decl.custom_perception_for(stealth).is_valid());
}

TEST_CASE(
    "[Networked][Interest][Hosted] a refused declaration leaves the record "
    "exactly as it was"
) {
    netw_test::FaultWatch expected_faults;
    expected_faults.expect_error(
        "[error][interest] Condition \"policy == custom_policy && "
        "!custom.is_valid()\" is true. Returning: false. "
        "NetwInterest.on_leave_policy got a CUSTOM policy with no "
        "callback. Pass the callback that decides it.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"policy != custom_policy && "
        "custom.is_valid()\" is true. Returning: false. "
        "NetwInterest.on_leave_policy got a callback with a policy "
        "that is not CUSTOM. Use the CUSTOM policy, or drop the "
        "callback.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"policy < 0 || policy > "
        "custom_policy\" is true. Returning: false. "
        "NetwInterest.on_leave_policy got policy 7, which does not "
        "exist.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"layer_id.is_empty()\" is true."
        " Returning: false. NetwInterest.on_leave_policy needs a "
        "layer name, and it was empty.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"policy == custom_policy && "
        "!custom.is_valid()\" is true. Returning: false. "
        "NetwInterest.on_perception_policy got a CUSTOM policy with"
        " no callback. Pass the callback that decides it.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"policy != custom_policy && "
        "custom.is_valid()\" is true. Returning: false. "
        "NetwInterest.on_perception_policy got a callback with a "
        "policy that is not CUSTOM. Use the CUSTOM policy, or drop "
        "the callback.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"policy < 0 || policy > "
        "custom_policy\" is true. Returning: false. "
        "NetwInterest.on_perception_policy got policy -1, which "
        "does not exist.",
        1
    );
    expected_faults.expect_error(
        "[error][interest] Condition \"layer_id.is_empty()\" is true."
        " Returning: false. NetwInterest.join needs a layer name, "
        "and it was empty.",
        1
    );

    Decl decl;
    const Ref<RefCounted> holder = callable_holder();
    const StringName stealth("stealth");
    const Callable callback(holder.ptr(), StringName("get_class"));
    decl.set_leave_policy(stealth, RETAIN, Callable());
    decl.set_perception_policy(stealth, SHOW, Callable());
    CHECK(!decl.set_leave_policy(stealth, LEAVE_CUSTOM, Callable()));
    CHECK(!decl.set_leave_policy(stealth, LEAVE_HIDE, callback));
    CHECK(!decl.set_leave_policy(stealth, 7, Callable()));
    CHECK(!decl.set_leave_policy(StringName(), RETAIN, Callable()));
    CHECK(!decl.set_perception_policy(stealth, PERCEPTION_CUSTOM, Callable()));
    CHECK(!decl.set_perception_policy(stealth, HIDE, callback));
    CHECK(!decl.set_perception_policy(stealth, -1, Callable()));
    CHECK(!decl.join(StringName()));

    NETW_CHECK_EQ(decl.leave_policy_for(stealth, LEAVE_HIDE), RETAIN);
    NETW_CHECK_EQ(decl.perception_policy_for(stealth, HIDE), SHOW);
    CHECK(!decl.custom_leave_for(stealth).is_valid());
    CHECK(!decl.custom_perception_for(stealth).is_valid());
    NETW_CHECK_EQ(decl.labels().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a cleared declaration names nothing and "
    "reports no observers"
) {
    Decl decl;
    const Ref<RefCounted> holder = callable_holder();
    const StringName arena("arena");
    decl.join(arena);
    decl.set_leave_policy(
        arena,
        LEAVE_CUSTOM,
        Callable(holder.ptr(), StringName("get_class"))
    );
    decl.set_reports_observers(true);

    decl.clear();

    NETW_CHECK_EQ(decl.labels().size(), 0);
    NETW_CHECK_EQ(decl.leave_policy_for(arena, LEAVE_HIDE), LEAVE_HIDE);
    CHECK(!decl.custom_leave_for(arena).is_valid());
    CHECK(!decl.get_reports_observers());
}

} // namespace TestDecl
