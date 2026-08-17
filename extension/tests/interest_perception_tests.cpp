// The local perception book's laws.
//
// The unknown answer is the one worth stating separately. Two of the cases
// below would pass against a plain bool, and the third would not.

#include "support/netw_test.h"

#include "netw/interest_perception.hpp"

namespace TestNetwInterestPerception {

using namespace godot;
using netw::NetwInterestDecl;
using netw::NetwInterestEngine;
using netw::NetwInterestPerception;

constexpr int64_t ROOT = 1;
constexpr int64_t OTHER = 2;
constexpr int HIDE = NetwInterestPerception::HIDE;
constexpr int SHOW = NetwInterestPerception::SHOW;
constexpr int CUSTOM = NetwInterestPerception::CUSTOM;

StringName hide_key() {
    return StringName("hide");
}

StringName custom_key() {
    return StringName("custom");
}

Ref<NetwInterestPerception> fresh() {
    Ref<NetwInterestPerception> book;
    book.instantiate();
    return book;
}

Ref<NetwInterestDecl> declaration() {
    Ref<NetwInterestDecl> decl;
    decl.instantiate();
    return decl;
}

Ref<NetwInterestEngine> engine_with(
    std::initializer_list<const char *> p_layers,
    int p_policy
) {
    Ref<NetwInterestEngine> engine;
    engine.instantiate();
    for (const char *id : p_layers) {
        engine->layer_set_perception_policy(StringName(id), p_policy);
    }
    return engine;
}

Array layer_list(std::initializer_list<const char *> p_ids) {
    Array out;
    for (const char *id : p_ids) {
        out.push_back(StringName(id));
    }
    return out;
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity nothing has been decided about "
    "is a third answer"
) {
    const Ref<NetwInterestPerception> book = fresh();

    CHECK(!book->is_known(ROOT));
    CHECK(book->set_visible(ROOT, true));
    CHECK(book->is_known(ROOT));
    CHECK(!book->set_visible(ROOT, true));
    CHECK(book->set_visible(ROOT, false));
    CHECK(!book->set_visible(ROOT, false));

    book->forget(ROOT);

    CHECK(!book->is_known(ROOT));
    CHECK(book->set_visible(ROOT, false));
}

TEST_CASE(
    "[Networked][Interest][Hosted] armed actions are taken back once and "
    "only once"
) {
    const Ref<NetwInterestPerception> book = fresh();
    const Ref<NetwInterestDecl> holder = declaration();
    Array action;
    action.push_back(Callable(holder.ptr(), StringName("clear")));
    action.push_back(StringName("stealth"));
    Array actions;
    actions.push_back(action);

    book->arm(ROOT, actions);
    book->arm(OTHER, Array());

    PackedInt64Array expected;
    expected.push_back(ROOT);
    CHECK(book->armed_keys() == expected);

    NETW_CHECK_EQ(book->disarm(ROOT).size(), 1);
    NETW_CHECK_EQ(book->disarm(ROOT).size(), 0);
    NETW_CHECK_EQ(book->armed_keys().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] forgetting a decision leaves the armed "
    "actions armed"
) {
    const Ref<NetwInterestPerception> book = fresh();
    const Ref<NetwInterestDecl> holder = declaration();
    Array action;
    action.push_back(Callable(holder.ptr(), StringName("clear")));
    action.push_back(StringName("stealth"));
    Array actions;
    actions.push_back(action);
    book->set_visible(ROOT, false);
    book->arm(ROOT, actions);

    book->forget(ROOT);

    CHECK(!book->is_known(ROOT));
    NETW_CHECK_EQ(book->armed_keys().size(), 1);

    book->clear();
    NETW_CHECK_EQ(book->armed_keys().size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a hide and a callback are both honoured, "
    "and an override resolves before a layer default"
) {
    const Ref<NetwInterestPerception> book = fresh();
    const Ref<NetwInterestEngine> engine
        = engine_with({"arena", "stealth"}, HIDE);
    const Ref<NetwInterestDecl> decl = declaration();
    const Ref<NetwInterestDecl> holder = declaration();
    const Callable callback(holder.ptr(), StringName("clear"));
    decl->set_perception_policy(StringName("stealth"), CUSTOM, callback);

    const Dictionary verdict
        = book->resolve(layer_list({"arena", "stealth"}), decl, engine);

    CHECK(bool(verdict[hide_key()]));
    const Array custom = verdict[custom_key()];
    NETW_CHECK_EQ(custom.size(), 1);
    if (custom.size() == 1) {
        const Array carried = custom[0];
        const StringName owed = carried[1];
        CHECK(owed == StringName("stealth"));
    }
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer declaring SHOW hides nothing on "
    "its own"
) {
    const Ref<NetwInterestPerception> book = fresh();
    const Ref<NetwInterestEngine> engine = engine_with({"arena"}, SHOW);

    const Dictionary verdict
        = book->resolve(layer_list({"arena"}), declaration(), engine);

    CHECK(!bool(verdict[hide_key()]));
    NETW_CHECK_EQ(Array(verdict[custom_key()]).size(), 0);

    const Ref<NetwInterestDecl> decl = declaration();
    decl->set_perception_policy(StringName("arena"), HIDE, Callable());
    CHECK(bool(book->resolve(layer_list({"arena"}), decl, engine)[hide_key()]));
}

TEST_CASE(
    "[Networked][Interest][Hosted] a layer nobody declared hides, and so "
    "does a CUSTOM whose callback is gone"
) {
    const Ref<NetwInterestPerception> book = fresh();
    const Ref<NetwInterestEngine> engine = engine_with({}, HIDE);

    const Dictionary unknown
        = book->resolve(layer_list({"nowhere"}), declaration(), engine);
    CHECK(bool(unknown[hide_key()]));

    const Ref<NetwInterestDecl> decl = declaration();
    Ref<NetwInterestDecl> holder = declaration();
    decl->set_perception_policy(
        StringName("stealth"),
        CUSTOM,
        Callable(holder.ptr(), StringName("clear"))
    );
    const Ref<NetwInterestEngine> showing = engine_with({"stealth"}, SHOW);
    holder.unref();

    ERR_PRINT_OFF;
    const Dictionary verdict
        = book->resolve(layer_list({"stealth"}), decl, showing);
    ERR_PRINT_ON;

    CHECK(bool(verdict[hide_key()]));
    NETW_CHECK_EQ(Array(verdict[custom_key()]).size(), 0);
}

} // namespace TestNetwInterestPerception
