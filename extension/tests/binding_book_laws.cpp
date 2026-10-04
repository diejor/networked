#include "support/netw_test.h"

#include <cstdint>

#include "godot/spatial_node.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/schema_core.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/predict/engine.hpp"
#include "netw/repl/property_binding.hpp"
#include "netw/repl/property_set.hpp"

namespace TestNetwBindingBookLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::NetwPredictionEngine;
using netw::SyncPipeline;
using netw::repl::BindingBook;
using netw::repl::Pin;
using netw::repl::PropertyBinding;
using netw::repl::PropertyColumn;
using netw::repl::PropertySet;
using netw::repl::SetBook;

uint64_t sealed_pose(SetBook &r_sets) {
    const uint64_t declared = r_sets.create();
    PropertySet *set = r_sets.get(declared);
    set->record = PropertySet::RECORD_STATE;
    set->bind_column(
        PropertyColumn::create(
            StringName("position"),
            Ref<netw::NetwQuantize>(),
            false,
            netw::enums::NetwMultiplayer::ColumnType(netw::SchemaCore::VECTOR2)
        )
    );
    set->sealed = true;
    return declared;
}

TEST_CASE(
    "[Networked][Store][Hosted] a retired binding a prediction slot still "
    "names stays readable, and the last unpin frees it under a stale id"
) {
    SetBook sets;
    BindingBook book(sets);
    NetwPredictionEngine pool;
    pool.attach_bindings(&book);
    const int64_t slot = pool.open();
    REQUIRE(slot >= 0);

    PropertyBinding *named = book.create(sealed_pose(sets), nullptr);
    const uint64_t id = named->id;
    pool.bind_property_sets(slot, named, nullptr);

    book.retire(id);
    const bool kept = book.get(id) == named;
    CHECK(kept);
    const bool hidden_from_live = book.live(id) == nullptr;
    CHECK(hidden_from_live);
    const bool slot_still_reads_it = pool.state_binding_of(slot) == named;
    CHECK(slot_still_reads_it);
    NETW_CHECK_EQ(int64_t(book.retired_count()), int64_t(1));

    pool.bind_property_sets(slot, nullptr, nullptr);
    const bool freed = book.get(id) == nullptr;
    CHECK(freed);
    NETW_CHECK_EQ(int64_t(book.count()), int64_t(0));
    NETW_CHECK_EQ(int64_t(book.retired_count()), int64_t(0));

    PropertyBinding *unnamed = book.create(sealed_pose(sets), nullptr);
    const uint64_t loose = unnamed->id;
    book.retire(loose);
    const bool freed_at_once = book.get(loose) == nullptr;
    CHECK(freed_at_once);
    const bool fresh_id = book.create(SetBook::NONE, nullptr)->id != loose;
    CHECK(fresh_id);
}

TEST_CASE(
    "[Networked][Store][Hosted] a binding the pipeline drops leaves the store "
    "and the pipeline at once, while the slot that names it keeps reading it"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    SyncPipeline *pipeline = session->sync_pipeline();
    REQUIRE(pipeline != nullptr);
    BindingBook &book = session->property_binding_book();
    NetwPredictionEngine *pool = session->get_prediction_engine();
    Node3D *node = memnew(Node3D);

    const uint64_t declared = sealed_pose(session->property_set_book());
    REQUIRE(int(pipeline->register_property_set(node, declared)) == int(OK));
    PropertyBinding *held
        = pipeline->derived_binding(node, PropertySet::RECORD_STATE);
    REQUIRE(held != nullptr);
    const uint64_t id = held->id;
    const bool admitted = session->store().row_of(id, 0).valid();
    CHECK(admitted);
    const int64_t slot = pool->open();
    REQUIRE(slot >= 0);
    pool->bind_property_sets(slot, held, nullptr);

    pipeline->unregister_derived(node);
    const bool off_the_store = !session->store().row_of(id, 0).valid();
    CHECK(off_the_store);
    const bool off_the_pipeline
        = pipeline->derived_binding(node, PropertySet::RECORD_STATE) == nullptr;
    CHECK(off_the_pipeline);
    const bool not_live = book.live(id) == nullptr;
    CHECK(not_live);
    const bool slot_reads_it = pool->state_binding_of(slot) == held;
    CHECK(slot_reads_it);
    NETW_CHECK_EQ(int64_t(book.count()), int64_t(1));

    pool->close(slot);
    const bool freed = book.get(id) == nullptr;
    CHECK(freed);
    NETW_CHECK_EQ(int64_t(book.count()), int64_t(0));
    memdelete(node);
}

TEST_CASE(
    "[Networked][Store][Hosted] a session that clears its state frees every "
    "binding at once, and the slots that named them read them gone"
) {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    SyncPipeline *pipeline = session->sync_pipeline();
    REQUIRE(pipeline != nullptr);
    NetwPredictionEngine *pool = session->get_prediction_engine();
    Node3D *first = memnew(Node3D);
    Node3D *second = memnew(Node3D);

    const uint64_t declared = sealed_pose(session->property_set_book());
    REQUIRE(int(pipeline->register_property_set(first, declared)) == int(OK));
    REQUIRE(int(pipeline->register_property_set(second, declared)) == int(OK));
    const int64_t named = pool->open();
    const int64_t both = pool->open();
    pool->bind_property_sets(
        named,
        pipeline->derived_binding(first, PropertySet::RECORD_STATE),
        nullptr
    );
    pool->bind_property_sets(
        both,
        pipeline->derived_binding(second, PropertySet::RECORD_STATE),
        pipeline->derived_binding(second, PropertySet::RECORD_STATE)
    );
    NETW_CHECK_EQ(
        int64_t(session->property_binding_book().count()),
        int64_t(2)
    );

    session->clear_session_state();
    NETW_CHECK_EQ(
        int64_t(session->property_binding_book().retired_count()),
        int64_t(0)
    );
    NETW_CHECK_EQ(
        int64_t(session->property_binding_book().count()),
        int64_t(0)
    );
    const bool named_gone = pool->state_binding_of(named) == nullptr;
    CHECK(named_gone);
    const bool both_gone = pool->state_binding_of(both) == nullptr
        && pool->input_binding_of(both) == nullptr;
    CHECK(both_gone);
    pool->close(named);
    pool->close(both);
    memdelete(second);
    memdelete(first);
}

} // namespace TestNetwBindingBookLaws
