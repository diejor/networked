// The per-entity interest declaration's laws.
//
// Two of them are refusals that used to be release-stripped assertions, so the
// cases below check that a rejected declaration leaves the record exactly as it
// was. A guard that reports and then writes anyway is the failure a return
// value alone would not catch.

#include "support/netw_test.h"

#include "netw/interest_decl.hpp"

namespace TestNetwInterestDecl {

using namespace godot;
using netw::NetwInterestDecl;

constexpr int DESPAWN = NetwInterestDecl::LEAVE_DESPAWN;
constexpr int RETAIN = NetwInterestDecl::LEAVE_RETAIN;
constexpr int LEAVE_CUSTOM = NetwInterestDecl::LEAVE_CUSTOM;
constexpr int HIDE = NetwInterestDecl::PERCEPTION_HIDE;
constexpr int SHOW = NetwInterestDecl::PERCEPTION_SHOW;
constexpr int PERCEPTION_CUSTOM = NetwInterestDecl::PERCEPTION_CUSTOM;

Ref<NetwInterestDecl> fresh() {
    Ref<NetwInterestDecl> decl;
    decl.instantiate();
    return decl;
}

TEST_CASE(
    "[Networked][Interest][Hosted] a declared label joins once and keeps its "
    "declaration order"
) {
    const Ref<NetwInterestDecl> decl = fresh();

    CHECK(decl->join(StringName("arena")));
    CHECK(!decl->join(StringName("arena")));
    CHECK(decl->join(StringName("stealth")));

    Array expected;
    expected.push_back(StringName("arena"));
    expected.push_back(StringName("stealth"));
    CHECK(decl->labels() == expected);
    CHECK(decl->has_label(StringName("arena")));
    CHECK(!decl->has_label(StringName("nowhere")));

    CHECK(decl->leave(StringName("arena")));
    CHECK(!decl->leave(StringName("arena")));
    NETW_CHECK_EQ(decl->labels().size(), 1);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an override resolves before the layer "
    "default, and only for the layer it names"
) {
    const Ref<NetwInterestDecl> decl = fresh();

    const StringName stealth("stealth");
    const StringName arena("arena");

    NETW_CHECK_EQ(decl->leave_policy_for(stealth, DESPAWN), DESPAWN);
    CHECK(decl->set_leave_policy(stealth, RETAIN, Callable()));

    NETW_CHECK_EQ(decl->leave_policy_for(stealth, DESPAWN), RETAIN);
    NETW_CHECK_EQ(decl->leave_policy_for(arena, DESPAWN), DESPAWN);

    CHECK(decl->set_perception_policy(stealth, SHOW, Callable()));
    NETW_CHECK_EQ(decl->perception_policy_for(stealth, HIDE), SHOW);
    NETW_CHECK_EQ(decl->perception_policy_for(arena, HIDE), HIDE);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a leaving label keeps the overrides "
    "declared for it"
) {
    const Ref<NetwInterestDecl> decl = fresh();
    const StringName stealth("stealth");
    decl->join(stealth);
    decl->set_leave_policy(stealth, RETAIN, Callable());

    decl->leave(stealth);

    CHECK(!decl->has_label(stealth));
    NETW_CHECK_EQ(decl->leave_policy_for(stealth, DESPAWN), RETAIN);
}

TEST_CASE(
    "[Networked][Interest][Hosted] CUSTOM is the only policy that carries a "
    "callback, and the only one that may"
) {
    const Ref<NetwInterestDecl> decl = fresh();
    const Ref<NetwInterestDecl> holder = fresh();
    const StringName stealth("stealth");
    const Callable callback(holder.ptr(), StringName("clear"));

    CHECK(decl->set_leave_policy(stealth, LEAVE_CUSTOM, callback));
    CHECK(decl->custom_leave_for(stealth) == callback);

    CHECK(decl->set_leave_policy(stealth, RETAIN, Callable()));
    CHECK(!decl->custom_leave_for(stealth).is_valid());

    CHECK(decl->set_perception_policy(stealth, PERCEPTION_CUSTOM, callback));
    CHECK(decl->custom_perception_for(stealth) == callback);

    CHECK(decl->set_perception_policy(stealth, SHOW, Callable()));
    CHECK(!decl->custom_perception_for(stealth).is_valid());
}

TEST_CASE(
    "[Networked][Interest][Hosted] a refused declaration leaves the record "
    "exactly as it was"
) {
    const Ref<NetwInterestDecl> decl = fresh();
    const Ref<NetwInterestDecl> holder = fresh();
    const StringName stealth("stealth");
    const Callable callback(holder.ptr(), StringName("clear"));
    decl->set_leave_policy(stealth, RETAIN, Callable());
    decl->set_perception_policy(stealth, SHOW, Callable());

    ERR_PRINT_OFF;
    CHECK(!decl->set_leave_policy(stealth, LEAVE_CUSTOM, Callable()));
    CHECK(!decl->set_leave_policy(stealth, DESPAWN, callback));
    CHECK(!decl->set_leave_policy(stealth, 7, Callable()));
    CHECK(!decl->set_leave_policy(StringName(), RETAIN, Callable()));
    CHECK(!decl->set_perception_policy(stealth, PERCEPTION_CUSTOM, Callable()));
    CHECK(!decl->set_perception_policy(stealth, HIDE, callback));
    CHECK(!decl->set_perception_policy(stealth, -1, Callable()));
    CHECK(!decl->join(StringName()));
    ERR_PRINT_ON;

    NETW_CHECK_EQ(decl->leave_policy_for(stealth, DESPAWN), RETAIN);
    NETW_CHECK_EQ(decl->perception_policy_for(stealth, HIDE), SHOW);
    CHECK(!decl->custom_leave_for(stealth).is_valid());
    CHECK(!decl->custom_perception_for(stealth).is_valid());
    NETW_CHECK_EQ(decl->labels().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a cleared declaration names nothing and "
    "reports no observers"
) {
    const Ref<NetwInterestDecl> decl = fresh();
    const Ref<NetwInterestDecl> holder = fresh();
    const StringName arena("arena");
    decl->join(arena);
    decl->set_leave_policy(
        arena,
        LEAVE_CUSTOM,
        Callable(holder.ptr(), StringName("clear"))
    );
    decl->set_reports_observers(true);

    decl->clear();

    NETW_CHECK_EQ(decl->labels().size(), 0);
    NETW_CHECK_EQ(decl->leave_policy_for(arena, DESPAWN), DESPAWN);
    CHECK(!decl->custom_leave_for(arena).is_valid());
    CHECK(!decl->get_reports_observers());
}

} // namespace TestNetwInterestDecl
