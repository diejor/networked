#include "support/netw_test.h"

#include "netw/api/schema_core.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/frame_records.hpp"
#include "support/predict_rows.h"

namespace TestNetwPredictInputBookLaws {

using namespace godot;
using netw::NetwPredictionEngine;
using netw::SchemaCore;
using netw::predict::CellProvenance;
using netw::predict::FieldDecl;
using netw::predict::TransitionBook;
using netw::table::SchemaRecord;

const SchemaRecord &button_schema() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = "PredictInput";
        SchemaCore::append_column(&made, "button", SchemaCore::U8, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

const SchemaRecord &wide_button_schema() {
    static SchemaRecord record = [] {
        SchemaRecord made;
        made.name = "PredictWideInput";
        SchemaCore::append_column(&made, "button", SchemaCore::U16, 1);
        SchemaCore::fix(&made);
        return made;
    }();
    return record;
}

int64_t framed_slot(NetwPredictionEngine *p_pool, netw::Role p_role) {
    const int64_t slot = p_pool->open();
    LocalVector<FieldDecl> input;
    FieldDecl button;
    button.key = StringName("button");
    button.type = int(Variant::INT);
    button.column_type = SchemaCore::U8;
    input.push_back(button);
    p_pool->rewire(slot, LocalVector<FieldDecl>(), input);
    p_pool->configure(
        slot,
        int(netw::Schedule::FRAME),
        int(p_role),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    p_pool->open_lane(slot);
    return slot;
}

Dictionary pressed(int p_value) {
    Dictionary out;
    out[StringName("button")] = p_value;
    return out;
}

void author(
    NetwPredictionEngine *p_pool,
    int64_t p_slot,
    int64_t p_label,
    bool p_fresh,
    int p_value
) {
    p_pool->tape_author(p_slot, p_label, p_fresh);
    p_pool->record_authored_command(
        p_slot,
        p_pool->tape_span(p_slot)[1],
        p_label,
        p_fresh,
        netw_test::input_row(p_pool, p_slot, pressed(p_value))
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][InputBook] IB1 a held FRAME transition "
    "keeps its own input entry, not fresh and carrying the last label, and "
    "the authority admits it as the same held entry of the owner's command"
) {
    NetwPredictionEngine held_owner;
    NetwPredictionEngine held_authority;
    NetwPredictionEngine *const owner = &held_owner;
    NetwPredictionEngine *const authority = &held_authority;
    const int64_t authored = framed_slot(owner, netw::Role::PREDICT);
    const int64_t consumed = framed_slot(authority, netw::Role::CONSUME);

    author(owner, authored, 10, true, 7);
    author(owner, authored, 10, false, 7);
    author(owner, authored, 11, true, 9);

    const TransitionBook *book = owner->input_book(authored);
    REQUIRE(book != nullptr);
    NETW_CHECK_EQ(int64_t(book->count()), int64_t(3));
    REQUIRE(book->holds(1));
    CHECK_FALSE(book->fresh_at(1));
    NETW_CHECK_EQ(book->label_of(1), 10);
    NETW_CHECK_EQ(book->origin_of(1), int(CellProvenance::AUTHORED));
    NETW_CHECK_EQ(
        int(netw_test::input_at(owner, authored, 1)[StringName("button")]),
        7
    );

    Array keys;
    keys.append(StringName("button"));
    netw::predict::CommandFrameRecord shipped;
    REQUIRE(netw::predict::CommandFrameRecord::decode(
        button_schema(),
        owner->build_command_frame(authored, button_schema(), keys, 8),
        shipped
    ));
    NETW_CHECK_EQ(shipped.transition_count(), 3);
    authority->admit_command_frame(consumed, shipped, keys);

    const TransitionBook *admitted = authority->input_book(consumed);
    REQUIRE(admitted != nullptr);
    REQUIRE(admitted->holds(1));
    CHECK_FALSE(admitted->fresh_at(1));
    NETW_CHECK_EQ(admitted->label_of(1), 10);
    NETW_CHECK_EQ(admitted->origin_of(1), int(CellProvenance::AUTHORED));
    CHECK(admitted->fresh_at(2));
    NETW_CHECK_EQ(
        int(netw_test::input_at(authority, consumed, 2)[StringName("button")]),
        9
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][InputBook] IB3 a command frame whose wire "
    "schema does not share the input book's codes still carries every "
    "authored value, and the authority admits them into its own book"
) {
    NetwPredictionEngine held_owner;
    NetwPredictionEngine held_authority;
    NetwPredictionEngine *const owner = &held_owner;
    NetwPredictionEngine *const authority = &held_authority;
    const int64_t authored = framed_slot(owner, netw::Role::PREDICT);
    const int64_t consumed = framed_slot(authority, netw::Role::CONSUME);

    author(owner, authored, 10, true, 7);
    author(owner, authored, 11, true, 200);

    Array keys;
    keys.append(StringName("button"));
    netw::predict::CommandFrameRecord shipped;
    REQUIRE(netw::predict::CommandFrameRecord::decode(
        wide_button_schema(),
        owner->build_command_frame(authored, wide_button_schema(), keys, 8),
        shipped
    ));
    NETW_CHECK_EQ(shipped.transition_count(), 2);
    NETW_CHECK_EQ(shipped.payload_count(), 2);
    authority->admit_command_frame(consumed, shipped, keys);

    netw::predict::StateRow command;
    REQUIRE(authority->read_command_at(consumed, 0, command));
    REQUIRE(command.has(0));
    NETW_CHECK_EQ(int(command.values[0]), 7);
    REQUIRE(authority->read_command_at(consumed, 1, command));
    REQUIRE(command.has(0));
    NETW_CHECK_EQ(int(command.values[0]), 200);
}

TEST_CASE(
    "[Networked][Predict][Hosted][InputBook] IB2 an input field the slot's "
    "input declaration does not name is dropped by the book, so a reader and "
    "the command hash see the declared fields only"
) {
    NetwPredictionEngine held_owner;
    NetwPredictionEngine *const owner = &held_owner;
    const int64_t authored = framed_slot(owner, netw::Role::PREDICT);

    Dictionary undeclared = pressed(3);
    undeclared[StringName("trigger")] = 1;
    owner->tape_author(authored, 10, true);
    REQUIRE(owner->record_authored_command(
        authored,
        owner->tape_span(authored)[1],
        10,
        true,
        netw_test::input_row(owner, authored, undeclared)
    ));

    const Dictionary read = netw_test::input_at(owner, authored, 0);
    NETW_CHECK_EQ(read.size(), 1);
    NETW_CHECK_EQ(int(read[StringName("button")]), 3);
    CHECK_FALSE(read.has(StringName("trigger")));
    NETW_CHECK_EQ(
        owner->command_hash_row(
            authored,
            netw_test::input_row(owner, authored, undeclared)
        ),
        owner->command_hash_row(
            authored,
            netw_test::input_row(owner, authored, pressed(3))
        )
    );
}

} // namespace TestNetwPredictInputBookLaws
