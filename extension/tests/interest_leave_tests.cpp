#include "support/netw_test.h"

#include "netw/interest_leave.hpp"

namespace TestInterestLeave {

using namespace godot;
using netw::NetwInterestDecl;
using netw::InterestEngine;
using netw::InterestLeave;

constexpr int64_t ROOT = 1;
constexpr int64_t OTHER = 2;
constexpr int DESPAWN = NetwInterestDecl::LEAVE_DESPAWN;
constexpr int RETAIN = NetwInterestDecl::LEAVE_RETAIN;
constexpr int CUSTOM = NetwInterestDecl::LEAVE_CUSTOM;

StringName despawn_key() {
    return StringName("despawn");
}

StringName custom_key() {
    return StringName("custom");
}

InterestLeave fresh() {
    return InterestLeave();
}

Ref<NetwInterestDecl> declaration() {
    Ref<NetwInterestDecl> decl;
    decl.instantiate();
    return decl;
}

InterestEngine engine_with(
    std::initializer_list<const char *> p_layers,
    int p_policy
) {
    InterestEngine engine;
    for (const char *id : p_layers) {
        engine.layer_set_leave_policy(StringName(id), p_policy);
    }
    return engine;
}

bool despawns(const Dictionary &p_verdict) {
    return bool(p_verdict[despawn_key()]);
}

Array custom_of(const Dictionary &p_verdict) {
    return p_verdict[custom_key()];
}

TEST_CASE(
    "[Networked][Interest][Hosted] a loss with no attribution despawns "
    "unless the copy is retained"
) {
    InterestLeave book = fresh();

    InterestEngine none;

    CHECK(despawns(book.resolve(ROOT, 4, declaration(), none)));

    Dictionary retain;
    retain[despawn_key()] = false;
    book.commit(ROOT, 4, retain, false);

    CHECK(book.is_retained(ROOT, 4));
    CHECK(!despawns(book.resolve(ROOT, 4, declaration(), none)));
    CHECK(despawns(book.resolve(ROOT, 9, declaration(), none)));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity override resolves before the "
    "layer default"
) {
    InterestLeave book = fresh();
    InterestEngine engine = engine_with({"arena"}, DESPAWN);
    const Ref<NetwInterestDecl> decl = declaration();
    book.record(ROOT, 4, StringName("arena"));

    CHECK(despawns(book.resolve(ROOT, 4, decl, engine)));

    decl->set_leave_policy(StringName("arena"), RETAIN, Callable());
    CHECK(!despawns(book.resolve(ROOT, 4, decl, engine)));

    InterestEngine retaining = engine_with({"arena"}, RETAIN);
    CHECK(!despawns(book.resolve(ROOT, 4, declaration(), retaining)));
}

TEST_CASE(
    "[Networked][Interest][Hosted] one layer wanting the copy gone ends the "
    "resolution"
) {
    InterestLeave book = fresh();
    InterestEngine engine
        = engine_with({"arena", "stealth"}, RETAIN);
    engine.layer_set_leave_policy(StringName("stealth"), DESPAWN);
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("stealth"));

    const Dictionary verdict = book.resolve(ROOT, 4, declaration(), engine);

    CHECK(despawns(verdict));
    NETW_CHECK_EQ(custom_of(verdict).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a CUSTOM leave is carried as its callback "
    "and the layer that owes it"
) {
    InterestLeave book = fresh();
    InterestEngine engine
        = engine_with({"arena", "stealth"}, RETAIN);
    const Ref<NetwInterestDecl> decl = declaration();
    const Ref<NetwInterestDecl> holder = declaration();
    const Callable callback(holder.ptr(), StringName("clear"));
    decl->set_leave_policy(StringName("stealth"), CUSTOM, callback);
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("stealth"));

    const Dictionary verdict = book.resolve(ROOT, 4, decl, engine);

    CHECK(!despawns(verdict));
    const Array actions = custom_of(verdict);
    NETW_CHECK_EQ(actions.size(), 1);
    if (actions.size() == 1) {
        const Array action = actions[0];
        const Callable carried = action[0];
        CHECK(carried == callback);
        const StringName owed = action[1];
        CHECK(owed == StringName("stealth"));
    }
}

TEST_CASE(
    "[Networked][Interest][Hosted] a CUSTOM leave whose callback is gone "
    "despawns rather than keeping a copy nothing stands in for"
) {
    InterestLeave book = fresh();
    InterestEngine engine = engine_with({"stealth"}, RETAIN);
    const Ref<NetwInterestDecl> decl = declaration();
    Ref<NetwInterestDecl> holder = declaration();
    decl->set_leave_policy(
        StringName("stealth"),
        CUSTOM,
        Callable(holder.ptr(), StringName("clear"))
    );
    book.record(ROOT, 4, StringName("stealth"));
    holder.unref();

    ERR_PRINT_OFF;
    const Dictionary verdict = book.resolve(ROOT, 4, decl, engine);
    ERR_PRINT_ON;

    CHECK(despawns(verdict));
    NETW_CHECK_EQ(custom_of(verdict).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a committed despawn releases the copy and "
    "consumes its attribution"
) {
    InterestLeave book = fresh();
    book.record(ROOT, 4, StringName("arena"));
    Dictionary retain;
    retain[despawn_key()] = false;
    book.commit(ROOT, 4, retain, false);
    CHECK(book.is_retained(ROOT, 4));
    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);

    book.record(ROOT, 4, StringName("arena"));
    Dictionary despawn;
    despawn[despawn_key()] = true;
    book.commit(ROOT, 4, despawn, false);

    CHECK(!book.is_retained(ROOT, 4));
    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an ancestor's despawn overrides a row's "
    "own answer"
) {
    InterestLeave book = fresh();
    Dictionary retain;
    retain[despawn_key()] = false;

    book.commit(ROOT, 4, retain, true);

    CHECK(!book.is_retained(ROOT, 4));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an attribution lasts exactly one pass"
) {
    InterestLeave book = fresh();
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("stealth"));
    book.record(ROOT, 4, StringName());

    Array expected;
    expected.push_back(StringName("arena"));
    expected.push_back(StringName("stealth"));
    CHECK(book.pending_layers(ROOT, 4) == expected);

    book.finish_sweep();

    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a peer that left and an entity that died "
    "keep nothing"
) {
    InterestLeave book = fresh();
    Dictionary retain;
    retain[despawn_key()] = false;
    book.commit(ROOT, 4, retain, false);
    book.commit(ROOT, 9, retain, false);
    book.commit(OTHER, 4, retain, false);
    book.record(ROOT, 4, StringName("arena"));
    book.record(OTHER, 4, StringName("arena"));

    book.forget_peer(4);

    CHECK(!book.is_retained(ROOT, 4));
    CHECK(book.is_retained(ROOT, 9));
    CHECK(!book.is_retained(OTHER, 4));
    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);

    book.forget_entity(ROOT);
    CHECK(!book.is_retained(ROOT, 9));

    book.commit(OTHER, 9, retain, false);
    book.clear();
    CHECK(!book.is_retained(OTHER, 9));
}

} // namespace TestInterestLeave
