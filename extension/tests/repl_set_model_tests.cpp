#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/repl/set_model.hpp"
#include "netw/sync_model.hpp"

namespace TestNetwReplSetModel {

using godot::RID;
using godot::StringName;
using netw::WritePolicy;
using netw::repl::SET_AUDIENCE_PUBLIC;
using netw::repl::SET_AUDIENCE_SERVER_ONLY;
using netw::repl::SET_CONSUMED;
using netw::repl::SET_DERIVED;
using netw::repl::SetModel;
using netw::repl::SetRow;

const int64_t ROUTE = 3;

int64_t ordinal_of(const SetModel &p_model, const char *p_key, int64_t p_kind) {
    const SetRow *row
        = p_model.row_for(ROUTE, p_kind, StringName(p_key), 0);
    return row == nullptr ? -1 : row->ordinal;
}

TEST_CASE("[Networked][Repl][Hosted] the order declarations arrive in does not "
          "reach the ordinals") {
    SetModel left;
    left.declare(ROUTE, SET_DERIVED, StringName("B"), 0, RID(), 0, 2);
    left.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);

    SetModel right;
    right.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);
    right.declare(ROUTE, SET_DERIVED, StringName("B"), 0, RID(), 0, 2);

    NETW_CHECK_EQ(ordinal_of(left, "A", SET_CONSUMED), 0);
    NETW_CHECK_EQ(ordinal_of(left, "B", SET_DERIVED), 1);
    NETW_CHECK_EQ(
        ordinal_of(right, "A", SET_CONSUMED),
        ordinal_of(left, "A", SET_CONSUMED)
    );
    NETW_CHECK_EQ(
        ordinal_of(right, "B", SET_DERIVED),
        ordinal_of(left, "B", SET_DERIVED)
    );
}

TEST_CASE("[Networked][Repl][Hosted] every consumed set holds a lower ordinal "
          "than every derived one") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1);
    model.declare(ROUTE, SET_CONSUMED, StringName("Z"), 0, RID(), 0, 1);

    NETW_CHECK_EQ(ordinal_of(model, "Z", SET_CONSUMED), 0);
    NETW_CHECK_EQ(ordinal_of(model, "A", SET_DERIVED), 1);
}

TEST_CASE("[Networked][Repl][Hosted] one key under two records is two rows") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1);
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 1, 2);

    const SetRow *first = model.row_for(ROUTE, SET_DERIVED, StringName("A"), 0);
    const SetRow *second = model.row_for(ROUTE, SET_DERIVED, StringName("A"), 1);

    REQUIRE(first != nullptr);
    REQUIRE(second != nullptr);
    NETW_CHECK_EQ(first->ordinal, 0);
    NETW_CHECK_EQ(second->ordinal, 1);
}

TEST_CASE("[Networked][Repl][Hosted] redeclaring a row updates it rather than "
          "adding a second") {
    SetModel model;
    model.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);
    const int64_t again
        = model.declare(ROUTE, SET_CONSUMED, StringName("A"), 4, RID(), 0, 9);

    const SetRow *row = model.row(ROUTE, 0);
    REQUIRE(row != nullptr);
    NETW_CHECK_EQ(again, 0);
    NETW_CHECK_EQ(row->comp, 4);
    NETW_CHECK_EQ(row->schema_hash, 9);
    REQUIRE(model.route_rows(ROUTE) != nullptr);
    NETW_CHECK_EQ(int64_t(model.route_rows(ROUTE)->size()), 1);
}

TEST_CASE("[Networked][Repl][Hosted] dropping a row closes the ordinal gap it "
          "left") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1);
    model.declare(ROUTE, SET_DERIVED, StringName("B"), 0, RID(), 0, 1);
    model.declare(ROUTE, SET_DERIVED, StringName("C"), 0, RID(), 0, 1);

    model.drop(ROUTE, SET_DERIVED, StringName("B"), 0);

    NETW_CHECK_EQ(ordinal_of(model, "A", SET_DERIVED), 0);
    NETW_CHECK_EQ(ordinal_of(model, "C", SET_DERIVED), 1);
}

TEST_CASE("[Networked][Repl][Hosted] a route with no declarations left is "
          "forgotten rather than kept empty") {
    SetModel model;
    model.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);
    model.drop(ROUTE, SET_CONSUMED, StringName("A"), 0);

    CHECK(model.route_rows(ROUTE) == nullptr);
    CHECK(model.row(ROUTE, 0) == nullptr);
}

TEST_CASE("[Networked][Repl][Hosted] a declaration naming no route or no key "
          "is refused") {
    SetModel model;

    NETW_CHECK_EQ(
        model.declare(0, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1),
        -1
    );
    NETW_CHECK_EQ(
        model.declare(ROUTE, SET_CONSUMED, StringName(), 0, RID(), 0, 1),
        -1
    );
    CHECK(model.route_rows(ROUTE) == nullptr);
}

TEST_CASE("[Networked][Repl][Hosted] an ordinal outside the route answers "
          "nothing rather than a neighbour") {
    SetModel model;
    model.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);

    CHECK(model.row(ROUTE, -1) == nullptr);
    CHECK(model.row(ROUTE, 1) == nullptr);
    CHECK(model.row(ROUTE + 1, 0) == nullptr);
}

TEST_CASE("[Networked][Repl][Hosted] a route's declarations die with the "
          "route") {
    SetModel model;
    model.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);
    model.declare(ROUTE + 1, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);

    model.clear_route(ROUTE);

    CHECK(model.route_rows(ROUTE) == nullptr);
    CHECK(model.route_rows(ROUTE + 1) != nullptr);
}

TEST_CASE("[Networked][Repl][Hosted] a declaration's gate columns are carried "
          "by the row rather than resolved per pass") {
    SetModel model;
    model.declare(
        ROUTE,
        SET_DERIVED,
        StringName("A"),
        0,
        RID(),
        0,
        1,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );

    const SetRow *row = model.row(ROUTE, 0);
    REQUIRE(row != nullptr);
    NETW_CHECK_EQ(row->policy, int64_t(WritePolicy::CONTROLLER));
    NETW_CHECK_EQ(row->audience, int64_t(SET_AUDIENCE_SERVER_ONLY));
}

TEST_CASE("[Networked][Repl][Hosted] a declaration that names no gate columns "
          "is authority-policed and public") {
    SetModel model;
    model.declare(ROUTE, SET_CONSUMED, StringName("A"), 0, RID(), 0, 1);

    const SetRow *row = model.row(ROUTE, 0);
    REQUIRE(row != nullptr);
    NETW_CHECK_EQ(row->policy, int64_t(WritePolicy::AUTHORITY));
    NETW_CHECK_EQ(row->audience, int64_t(SET_AUDIENCE_PUBLIC));
}

TEST_CASE("[Networked][Repl][Hosted] redeclaring a row restates its gate "
          "columns rather than keeping the first ones") {
    SetModel model;
    model.declare(
        ROUTE,
        SET_DERIVED,
        StringName("A"),
        0,
        RID(),
        0,
        1,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1);

    const SetRow *row = model.row(ROUTE, 0);
    REQUIRE(row != nullptr);
    NETW_CHECK_EQ(row->policy, int64_t(WritePolicy::AUTHORITY));
    NETW_CHECK_EQ(row->audience, int64_t(SET_AUDIENCE_PUBLIC));
}

TEST_CASE("[Networked][Repl][Hosted] a row's gate columns survive the reindex "
          "a later declaration forces") {
    SetModel model;
    model.declare(
        ROUTE,
        SET_DERIVED,
        StringName("Z"),
        0,
        RID(),
        0,
        1,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1);

    const SetRow *moved = model.row_for(ROUTE, SET_DERIVED, StringName("Z"), 0);
    REQUIRE(moved != nullptr);
    NETW_CHECK_EQ(moved->ordinal, 1);
    NETW_CHECK_EQ(moved->policy, int64_t(WritePolicy::CONTROLLER));
    NETW_CHECK_EQ(moved->audience, int64_t(SET_AUDIENCE_SERVER_ONLY));
}

SetRow gated(int64_t p_record, int64_t p_policy, int64_t p_audience) {
    SetRow out;
    out.route = ROUTE;
    out.kind = SET_DERIVED;
    out.key = StringName("A");
    out.record = p_record;
    out.policy = p_policy;
    out.audience = p_audience;
    return out;
}

TEST_CASE("[Networked][Repl][Hosted] a state row is server-authored whatever "
          "its policy says") {
    const SetRow row = gated(
        netw::repl::SET_RECORD_STATE,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_PUBLIC
    );

    CHECK(netw::repl::row_authors(row, 1, false, 7));
    CHECK_FALSE(netw::repl::row_authors(row, 7, true, 7));
}

TEST_CASE("[Networked][Repl][Hosted] a non-state row follows its policy") {
    const SetRow by_authority = gated(
        netw::repl::SET_RECORD_BROADCAST,
        int64_t(WritePolicy::AUTHORITY),
        SET_AUDIENCE_PUBLIC
    );
    const SetRow by_controller = gated(
        netw::repl::SET_RECORD_INPUT,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );
    const SetRow by_anyone = gated(
        netw::repl::SET_RECORD_BROADCAST,
        int64_t(WritePolicy::ANY_PEER),
        SET_AUDIENCE_PUBLIC
    );

    CHECK(netw::repl::row_authors(by_authority, 7, true, 0));
    CHECK_FALSE(netw::repl::row_authors(by_authority, 7, false, 7));
    CHECK(netw::repl::row_authors(by_controller, 7, false, 7));
    CHECK_FALSE(netw::repl::row_authors(by_controller, 7, true, 8));
    CHECK(netw::repl::row_authors(by_anyone, 7, false, 0));
}

TEST_CASE("[Networked][Repl][Hosted] a server-only row reaches the server "
          "alone, and nobody when the server authors it") {
    const SetRow row = gated(
        netw::repl::SET_RECORD_INPUT,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );
    godot::PackedInt32Array live;
    live.push_back(1);
    live.push_back(7);
    live.push_back(8);

    const godot::PackedInt32Array from_client
        = netw::repl::row_recipients(row, 7, live);
    const godot::PackedInt32Array from_server
        = netw::repl::row_recipients(row, 1, live);

    NETW_CHECK_EQ(int64_t(from_client.size()), int64_t(1));
    NETW_CHECK_EQ(int64_t(from_client[0]), int64_t(1));
    NETW_CHECK_EQ(int64_t(from_server.size()), int64_t(0));
}

TEST_CASE("[Networked][Repl][Hosted] a public row reaches every live peer but "
          "its own author") {
    const SetRow row = gated(
        netw::repl::SET_RECORD_STATE,
        int64_t(WritePolicy::AUTHORITY),
        SET_AUDIENCE_PUBLIC
    );
    godot::PackedInt32Array live;
    live.push_back(1);
    live.push_back(7);
    live.push_back(8);

    const godot::PackedInt32Array out = netw::repl::row_recipients(row, 1, live);

    NETW_CHECK_EQ(int64_t(out.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(out[0]), int64_t(7));
    NETW_CHECK_EQ(int64_t(out[1]), int64_t(8));
}

TEST_CASE("[Networked][Repl][Hosted] an ordinal naming no row authors nothing "
          "and reaches nobody") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    model->declare(
        ROUTE,
        SET_DERIVED,
        StringName("A"),
        0,
        RID(),
        0,
        1,
        int64_t(WritePolicy::ANY_PEER),
        SET_AUDIENCE_PUBLIC
    );
    godot::PackedInt32Array live;
    live.push_back(7);

    CHECK(model->authors(ROUTE, 0, 7, true, 7));
    CHECK_FALSE(model->authors(ROUTE, 1, 7, true, 7));
    CHECK_FALSE(model->authors(ROUTE + 1, 0, 7, true, 7));
    NETW_CHECK_EQ(
        int64_t(model->recipients(ROUTE, 1, 1, live).size()),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        int64_t(model->recipients(ROUTE, 0, 1, live).size()),
        int64_t(1)
    );
}

TEST_CASE("[Networked][Repl][Hosted] a received state row accepts the server "
          "alone, whatever its policy says") {
    const SetRow row = gated(
        netw::repl::SET_RECORD_STATE,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_PUBLIC
    );

    CHECK(netw::repl::row_admits_sender(row, 1, 7, 7));
    CHECK_FALSE(netw::repl::row_admits_sender(row, 7, 7, 7));
}

TEST_CASE("[Networked][Repl][Hosted] a received non-state row trusts the "
          "server and then its own policy") {
    const SetRow by_authority = gated(
        netw::repl::SET_RECORD_BROADCAST,
        int64_t(WritePolicy::AUTHORITY),
        SET_AUDIENCE_PUBLIC
    );
    const SetRow by_controller = gated(
        netw::repl::SET_RECORD_INPUT,
        int64_t(WritePolicy::CONTROLLER),
        SET_AUDIENCE_SERVER_ONLY
    );

    CHECK(netw::repl::row_admits_sender(by_authority, 1, 8, 0));
    CHECK(netw::repl::row_admits_sender(by_authority, 7, 7, 0));
    CHECK_FALSE(netw::repl::row_admits_sender(by_authority, 7, 8, 7));
    CHECK(netw::repl::row_admits_sender(by_controller, 7, 8, 7));
    CHECK_FALSE(netw::repl::row_admits_sender(by_controller, 7, 7, 8));
}

TEST_CASE("[Networked][Repl][Hosted] an ordinal nobody declared a descriptor "
          "for is admitted, and a disagreeing one is not") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 11);
    model.declare(ROUTE, SET_DERIVED, StringName("B"), 0, RID(), 0, 22);

    CHECK(model.admits_schema(ROUTE, 0));

    godot::HashMap<int64_t, int64_t> noted;
    noted[0] = 11;
    noted[1] = 99;
    model.note_descriptors(ROUTE, noted);

    CHECK(model.admits_schema(ROUTE, 0));
    CHECK_FALSE(model.admits_schema(ROUTE, 1));
    CHECK(model.admits_schema(ROUTE + 1, 0));
}

TEST_CASE("[Networked][Repl][Hosted] an ordinal a descriptor names but no row "
          "declares is refused") {
    SetModel model;
    godot::HashMap<int64_t, int64_t> noted;
    noted[0] = 11;
    model.note_descriptors(ROUTE, noted);

    CHECK_FALSE(model.admits_schema(ROUTE, 0));
}

TEST_CASE("[Networked][Repl][Hosted] noting nothing forgets a route's "
          "descriptors rather than recording an empty set") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 11);
    godot::HashMap<int64_t, int64_t> noted;
    noted[0] = 99;
    model.note_descriptors(ROUTE, noted);
    REQUIRE_FALSE(model.admits_schema(ROUTE, 0));

    model.note_descriptors(ROUTE, godot::HashMap<int64_t, int64_t>());

    CHECK(model.admits_schema(ROUTE, 0));
}

TEST_CASE("[Networked][Repl][Hosted] a route's noted descriptors die with the "
          "route") {
    SetModel model;
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 11);
    godot::HashMap<int64_t, int64_t> noted;
    noted[0] = 99;
    model.note_descriptors(ROUTE, noted);

    model.clear_route(ROUTE);
    model.declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 11);

    CHECK(model.admits_schema(ROUTE, 0));
}

TEST_CASE("[Networked][Repl][Hosted] an on-demand event fans out from the "
          "host and funnels to the server from a client") {
    godot::PackedInt32Array live;
    live.push_back(1);
    live.push_back(7);
    live.push_back(8);

    const godot::PackedInt32Array from_host
        = netw::repl::event_recipients(true, 1, 0, live);
    const godot::PackedInt32Array from_client
        = netw::repl::event_recipients(false, 7, 0, live);

    NETW_CHECK_EQ(int64_t(from_host.size()), int64_t(3));
    NETW_CHECK_EQ(int64_t(from_client.size()), int64_t(1));
    NETW_CHECK_EQ(int64_t(from_client[0]), int64_t(1));
}

TEST_CASE("[Networked][Repl][Hosted] a relayed event skips the peer it came "
          "from, so a sender never receives its own event back") {
    godot::PackedInt32Array live;
    live.push_back(1);
    live.push_back(7);
    live.push_back(8);

    const godot::PackedInt32Array out
        = netw::repl::event_recipients(true, 1, 7, live);

    NETW_CHECK_EQ(int64_t(out.size()), int64_t(2));
    NETW_CHECK_EQ(int64_t(out[0]), int64_t(1));
    NETW_CHECK_EQ(int64_t(out[1]), int64_t(8));
}

TEST_CASE("[Networked][Repl][Hosted] the server itself sends no on-demand "
          "event to the server") {
    godot::PackedInt32Array live;
    live.push_back(1);

    NETW_CHECK_EQ(
        int64_t(netw::repl::event_recipients(false, 1, 0, live).size()),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        int64_t(netw::repl::event_recipients(false, 7, 1, live).size()),
        int64_t(0)
    );
}

TEST_CASE("[Networked][Repl][Hosted] a binding is found by the ordinal its "
          "row currently holds, not the one it was attached under") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    godot::Ref<godot::RefCounted> zed;
    zed.instantiate();

    model->declare(ROUTE, SET_DERIVED, StringName("Z"), 0, RID(), 0, 1, 0, 0);
    model->attach(ROUTE, SET_DERIVED, StringName("Z"), 0, zed);
    CHECK(model->binding_of(ROUTE, 0) == zed);

    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1, 0, 0);

    CHECK(model->binding_of(ROUTE, 1) == zed);
    CHECK(model->binding_of(ROUTE, 0).is_null());
}

TEST_CASE("[Networked][Repl][Hosted] detaching a binding leaves its row, and "
          "attaching nothing detaches") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    godot::Ref<godot::RefCounted> held;
    held.instantiate();
    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1, 0, 0);
    model->attach(ROUTE, SET_DERIVED, StringName("A"), 0, held);

    model->detach(ROUTE, SET_DERIVED, StringName("A"), 0);

    CHECK(model->binding_of(ROUTE, 0).is_null());
    CHECK(model->row(ROUTE, 0).is_valid());

    model->attach(ROUTE, SET_DERIVED, StringName("A"), 0, held);
    REQUIRE(model->binding_of(ROUTE, 0) == held);
    model->attach(
        ROUTE,
        SET_DERIVED,
        StringName("A"),
        0,
        godot::Ref<godot::RefCounted>()
    );
    CHECK(model->binding_of(ROUTE, 0).is_null());
}

TEST_CASE("[Networked][Repl][Hosted] a route's bindings answer in ordinal "
          "order and only for the kind asked for") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    godot::Ref<godot::RefCounted> first;
    godot::Ref<godot::RefCounted> second;
    godot::Ref<godot::RefCounted> consumed;
    first.instantiate();
    second.instantiate();
    consumed.instantiate();

    model->declare(ROUTE, SET_DERIVED, StringName("B"), 0, RID(), 0, 1, 0, 0);
    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1, 0, 0);
    model->declare(ROUTE, SET_CONSUMED, StringName("C"), 0, RID(), 0, 1, 0, 0);
    model->attach(ROUTE, SET_DERIVED, StringName("B"), 0, second);
    model->attach(ROUTE, SET_DERIVED, StringName("A"), 0, first);
    model->attach(ROUTE, SET_CONSUMED, StringName("C"), 0, consumed);

    const godot::TypedArray<godot::RefCounted> derived
        = model->route_bindings(ROUTE, SET_DERIVED);

    NETW_CHECK_EQ(int(derived.size()), 2);
    CHECK(godot::Ref<godot::RefCounted>(derived[0]) == first);
    CHECK(godot::Ref<godot::RefCounted>(derived[1]) == second);
    NETW_CHECK_EQ(int(model->route_bindings(ROUTE, SET_CONSUMED).size()), 1);
}

TEST_CASE("[Networked][Repl][Hosted] a route's bindings die with the route") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    godot::Ref<godot::RefCounted> held;
    held.instantiate();
    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1, 0, 0);
    model->attach(ROUTE, SET_DERIVED, StringName("A"), 0, held);

    model->clear_route(ROUTE);
    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 0, 1, 0, 0);

    CHECK(model->binding_of(ROUTE, 0).is_null());
}

TEST_CASE("[Networked][Repl][Hosted] one key under two records keeps two "
          "bindings, because the record is part of a row's identity") {
    godot::Ref<netw::NetwSyncModel> model;
    model.instantiate();
    godot::Ref<godot::RefCounted> state;
    godot::Ref<godot::RefCounted> input;
    state.instantiate();
    input.instantiate();

    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 1, 1, 0, 0);
    model->declare(ROUTE, SET_DERIVED, StringName("A"), 0, RID(), 2, 1, 0, 0);
    model->attach(ROUTE, SET_DERIVED, StringName("A"), 1, state);
    model->attach(ROUTE, SET_DERIVED, StringName("A"), 2, input);

    CHECK(model->binding_of(ROUTE, 0) == state);
    CHECK(model->binding_of(ROUTE, 1) == input);
    CHECK(model->binding_of(ROUTE, 0) != model->binding_of(ROUTE, 1));
}

} // namespace TestNetwReplSetModel
