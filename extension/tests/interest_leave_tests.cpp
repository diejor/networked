#include "support/netw_test.h"

#include "netw/interest/leave.hpp"

namespace TestLeave {

using namespace godot;
using netw::interest::Decl;
using netw::interest::Engine;
using netw::interest::Leave;

constexpr int64_t ROOT = 1;
constexpr int64_t OTHER = 2;
constexpr int HIDE = Decl::LEAVE_HIDE;
constexpr int RETAIN = Decl::LEAVE_RETAIN;
constexpr int CUSTOM = Decl::LEAVE_CUSTOM;

StringName hide_key() {
    return StringName("hide");
}

StringName custom_key() {
    return StringName("custom");
}

Leave fresh() {
    return Leave();
}

const Decl *undeclared() {
    static const Decl blank;
    return &blank;
}

Ref<RefCounted> callable_holder() {
    Ref<RefCounted> holder;
    holder.instantiate();
    return holder;
}

Engine engine_with(std::initializer_list<const char *> p_layers, int p_policy) {
    Engine engine;
    for (const char *id : p_layers) {
        engine.layer_set_leave_policy(StringName(id), p_policy);
    }
    return engine;
}

bool hides(const Dictionary &p_verdict) {
    return bool(p_verdict[hide_key()]);
}

Array custom_of(const Dictionary &p_verdict) {
    return p_verdict[custom_key()];
}

TEST_CASE(
    "[Networked][Interest][Hosted] a loss with no attribution hides "
    "unless the copy is retained"
) {
    Leave book = fresh();

    Engine none;

    CHECK(hides(book.resolve(ROOT, 4, undeclared(), none)));

    Dictionary retain;
    retain[hide_key()] = false;
    book.commit(ROOT, 4, retain, false);

    CHECK(book.is_retained(ROOT, 4));
    CHECK(!hides(book.resolve(ROOT, 4, undeclared(), none)));
    CHECK(hides(book.resolve(ROOT, 9, undeclared(), none)));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an entity override resolves before the "
    "layer default"
) {
    Leave book = fresh();
    Engine engine = engine_with({"arena"}, HIDE);
    Decl decl;
    book.record(ROOT, 4, StringName("arena"));

    CHECK(hides(book.resolve(ROOT, 4, &decl, engine)));

    decl.set_leave_policy(StringName("arena"), RETAIN, Callable());
    CHECK(!hides(book.resolve(ROOT, 4, &decl, engine)));

    Engine retaining = engine_with({"arena"}, RETAIN);
    CHECK(!hides(book.resolve(ROOT, 4, undeclared(), retaining)));
}

TEST_CASE(
    "[Networked][Interest][Hosted] one layer wanting the copy gone ends the "
    "resolution"
) {
    Leave book = fresh();
    Engine engine = engine_with({"arena", "stealth"}, RETAIN);
    engine.layer_set_leave_policy(StringName("stealth"), HIDE);
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("stealth"));

    const Dictionary verdict = book.resolve(ROOT, 4, undeclared(), engine);

    CHECK(hides(verdict));
    NETW_CHECK_EQ(custom_of(verdict).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a CUSTOM leave is carried as its callback "
    "and the layer that owes it"
) {
    Leave book = fresh();
    Engine engine = engine_with({"arena", "stealth"}, RETAIN);
    Decl decl;
    const Ref<RefCounted> holder = callable_holder();
    const Callable callback(holder.ptr(), StringName("get_class"));
    decl.set_leave_policy(StringName("stealth"), CUSTOM, callback);
    book.record(ROOT, 4, StringName("arena"));
    book.record(ROOT, 4, StringName("stealth"));

    const Dictionary verdict = book.resolve(ROOT, 4, &decl, engine);

    CHECK(!hides(verdict));
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
    "hides rather than keeping a copy nothing stands in for"
) {
    Leave book = fresh();
    Engine engine = engine_with({"stealth"}, RETAIN);
    Decl decl;
    Ref<RefCounted> holder = callable_holder();
    decl.set_leave_policy(
        StringName("stealth"),
        CUSTOM,
        Callable(holder.ptr(), StringName("get_class"))
    );
    book.record(ROOT, 4, StringName("stealth"));
    holder.unref();

    ERR_PRINT_OFF;
    const Dictionary verdict = book.resolve(ROOT, 4, &decl, engine);
    ERR_PRINT_ON;

    CHECK(hides(verdict));
    NETW_CHECK_EQ(custom_of(verdict).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] a committed hide releases the copy and "
    "consumes its attribution"
) {
    Leave book = fresh();
    book.record(ROOT, 4, StringName("arena"));
    Dictionary retain;
    retain[hide_key()] = false;
    book.commit(ROOT, 4, retain, false);
    CHECK(book.is_retained(ROOT, 4));
    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);

    book.record(ROOT, 4, StringName("arena"));
    Dictionary hide;
    hide[hide_key()] = true;
    book.commit(ROOT, 4, hide, false);

    CHECK(!book.is_retained(ROOT, 4));
    NETW_CHECK_EQ(book.pending_layers(ROOT, 4).size(), 0);
}

TEST_CASE(
    "[Networked][Interest][Hosted] an ancestor's hide overrides a row's "
    "own answer"
) {
    Leave book = fresh();
    Dictionary retain;
    retain[hide_key()] = false;

    book.commit(ROOT, 4, retain, true);

    CHECK(!book.is_retained(ROOT, 4));
}

TEST_CASE(
    "[Networked][Interest][Hosted] an attribution lasts exactly one pass"
) {
    Leave book = fresh();
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
    Leave book = fresh();
    Dictionary retain;
    retain[hide_key()] = false;
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

} // namespace TestLeave
