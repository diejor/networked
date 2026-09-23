#include "support/netw_test.h"

#include "netw/sim/resolve.hpp"
#include "netw/sim/row.hpp"
#include "netw/sim/select.hpp"

namespace TestNetwSimSelectLaws {

using namespace godot;
using netw::sim::Candidate;
using netw::sim::Choice;
using netw::sim::Facts;
using netw::sim::Mode;
using netw::sim::Pick;
using netw::sim::Policy;
using netw::sim::Rows;
using netw::sim::Selection;

Candidate candidate(
    int64_t p_key,
    double p_distance,
    Pick p_pick = Pick::AUTOMATIC
) {
    Candidate out;
    out.key = p_key;
    out.order_key = p_key;
    out.distance_squared = p_distance * p_distance;
    out.pick = p_pick;
    out.eligible = true;
    return out;
}

LocalVector<Candidate> candidates(
    const Candidate &p_first,
    const Candidate &p_second
) {
    LocalVector<Candidate> out;
    out.push_back(p_first);
    out.push_back(p_second);
    return out;
}

Facts proxy_copy() {
    Facts facts;
    facts.declared = true;
    facts.state_rows = true;
    return facts;
}

TEST_CASE("[Networked][Sim][Hosted][Select] a chosen entity is selected") {
    Selection selection;
    selection.commit(
        candidates(
            candidate(1, 20.0, Pick::CHOSEN),
            candidate(2, 1.0, Pick::EXCLUDED)
        ),
        10
    );
    NETW_CHECK_EQ(selection.promoted_count(), 1);
    CHECK(selection.member(1)->promoted);
    CHECK_FALSE(selection.member(2)->promoted);
    NETW_CHECK_EQ(selection.member(1)->tenure.begin, 11);
    CHECK_FALSE(selection.member(1)->tenure.contains(10));
    CHECK(selection.member(1)->tenure.contains(11));
}

TEST_CASE("[Networked][Sim][Hosted][Select] nearest keeps what it selected") {
    Selection selection;
    selection.policy = Policy::NEAREST;
    selection.count = 1;
    selection.commit(candidates(candidate(1, 10.0), candidate(2, 10.5)), 0);
    CHECK(selection.member(1)->promoted);
    selection.commit(candidates(candidate(1, 10.0), candidate(2, 9.5)), 1);
    CHECK(selection.member(1)->promoted);
    CHECK_FALSE(selection.member(2)->promoted);
    selection.commit(candidates(candidate(1, 10.0), candidate(2, 5.0)), 2);
    CHECK_FALSE(selection.member(1)->promoted);
    CHECK(selection.member(2)->promoted);
}

TEST_CASE("[Networked][Sim][Hosted][Select] contact defers a handoff") {
    Selection selection;
    selection.policy = Policy::NEAREST;
    selection.count = 1;
    selection.commit(candidates(candidate(1, 10.0), candidate(2, 20.0)), 0);
    CHECK(selection.member(1)->promoted);

    Candidate near = candidate(2, 1.0);
    Candidate touching = candidate(1, 10.0);
    touching.contact = true;
    selection.commit(candidates(touching, near), 1);
    CHECK(selection.member(1)->promoted);
    CHECK_FALSE(selection.member(2)->promoted);

    selection.commit(candidates(candidate(1, 10.0), near), 2);
    CHECK_FALSE(selection.member(1)->promoted);
    CHECK(selection.member(2)->promoted);
}

TEST_CASE("[Networked][Sim][Hosted][Select] a deferral keeps its budget") {
    Selection selection;
    selection.policy = Policy::NEAREST;
    selection.count = 1;
    selection.commit(candidates(candidate(1, 10.0), candidate(2, 20.0)), 0);
    CHECK(selection.member(1)->promoted);

    Candidate touching = candidate(1, 30.0);
    touching.contact = true;
    LocalVector<Candidate> rows;
    rows.push_back(touching);
    rows.push_back(candidate(2, 1.0));
    rows.push_back(candidate(3, 2.0));
    selection.commit(rows, 1);
    NETW_CHECK_EQ(selection.promoted_count(), 1);
    CHECK(selection.member(1)->promoted);
}

TEST_CASE("[Networked][Sim][Hosted][Select] a radius has an exit margin") {
    Selection selection;
    selection.policy = Policy::WITHIN;
    selection.meters = 10.0;
    selection.commit(candidates(candidate(1, 9.0), candidate(2, 12.0)), 0);
    CHECK(selection.member(1)->promoted);
    selection.commit(candidates(candidate(1, 10.5), candidate(2, 12.0)), 1);
    CHECK(selection.member(1)->promoted);
    selection.commit(candidates(candidate(1, 11.1), candidate(2, 12.0)), 2);
    CHECK_FALSE(selection.member(1)->promoted);
}

TEST_CASE("[Networked][Sim][Hosted][Select] a departure lingers to the floor") {
    Selection selection;
    selection.commit(
        candidates(
            candidate(1, 1.0, Pick::CHOSEN),
            candidate(2, 2.0, Pick::EXCLUDED)
        ),
        4
    );
    LocalVector<Candidate> remaining;
    remaining.push_back(candidate(2, 2.0, Pick::EXCLUDED));
    selection.commit(remaining, 8);
    CHECK(selection.member(1)->tenure.contains(8));
    CHECK_FALSE(selection.member(1)->tenure.contains(9));
    NETW_CHECK_EQ(selection.lingering_count(), 1);
    selection.release_lingering(8);
    NETW_CHECK_EQ(selection.lingering_count(), 1);
    selection.release_lingering(9);
    CHECK((selection.member(1) == nullptr));
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] a chosen entity does not spend the "
    "automatic budget"
) {
    Selection selection;
    selection.policy = Policy::NEAREST;
    selection.count = 1;
    selection.commit(
        candidates(candidate(1, 20.0, Pick::CHOSEN), candidate(2, 1.0)),
        0
    );
    NETW_CHECK_EQ(selection.promoted_count(), 2);
    CHECK(selection.member(1)->promoted);
    CHECK(selection.member(2)->promoted);
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] one automatic policy per subject, the "
    "last call replaces it, and a negative budget selects nothing"
) {
    Choice choice;
    CHECK_FALSE(choice.declared());

    netw::sim::simulate_nearest(choice, 3, StringName("near"));
    CHECK(choice.declared());
    NETW_CHECK_EQ(int(choice.policy), int(Policy::NEAREST));
    NETW_CHECK_EQ(choice.count, 3);
    NETW_REQUIRE_EQ(choice.layers.size(), 1);

    netw::sim::simulate_within(choice, 4.0, StringName("far"));
    NETW_CHECK_EQ(int(choice.policy), int(Policy::WITHIN));
    NETW_REQUIRE_EQ(choice.layers.size(), 1);
    CHECK((choice.layers[0] == StringName("far")));

    netw::sim::simulate_nearest(choice, -2, StringName());
    NETW_CHECK_EQ(int(choice.policy), int(Policy::NONE));
    NETW_CHECK_EQ(choice.count, 0);
    netw::sim::simulate_within(choice, -1.0, StringName());
    NETW_CHECK_EQ(int(choice.policy), int(Policy::NONE));

    Selection selection;
    selection.policy = choice.policy;
    selection.commit(candidates(candidate(1, 0.0), candidate(2, 0.5)), 0);
    NETW_CHECK_EQ(selection.promoted_count(), 0);

    netw::sim::simulate_all(choice, StringName());
    NETW_CHECK_EQ(int(choice.policy), int(Policy::ALL));
    netw::sim::simulate_none(choice);
    NETW_CHECK_EQ(int(choice.policy), int(Policy::NONE));
    CHECK_FALSE(choice.declared());
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] an explicit selection is named once and "
    "forget withdraws it"
) {
    RID_Owner<int> owner;
    const RID member = owner.make_rid(0);
    Choice choice;
    CHECK(netw::sim::choose(choice, member, Pick::CHOSEN));
    CHECK_FALSE(netw::sim::choose(choice, member, Pick::CHOSEN));
    NETW_CHECK_EQ(choice.named.size(), 1);
    REQUIRE(choice.named_of(member) != nullptr);
    NETW_CHECK_EQ(int(choice.named_of(member)->pick), int(Pick::CHOSEN));
    CHECK(choice.declared());

    CHECK(netw::sim::forget(choice, member));
    CHECK_FALSE(netw::sim::forget(choice, member));
    CHECK((choice.named_of(member) == nullptr));
    CHECK_FALSE(choice.declared());
    owner.free(member);
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] X01 two subjects select one member and "
    "one withdraws, so the member stays ACTIVE while the other selects it"
) {
    RID_Owner<int> owner;
    const RID member = owner.make_rid(0);
    const RID first = owner.make_rid(1);
    const RID second = owner.make_rid(2);
    Rows rows;
    NETW_CHECK_EQ(int(rows.resolve(member, proxy_copy())), int(Mode::PROXY));

    CHECK(rows.note_selected(member, first));
    CHECK(rows.note_selected(member, second));
    CHECK_FALSE(rows.note_selected(member, second));
    NETW_CHECK_EQ(rows.selection_count(member), 2);
    NETW_CHECK_EQ(int(rows.resolve(member, proxy_copy())), int(Mode::ACTIVE));

    CHECK(rows.erase_selected(member, first));
    NETW_CHECK_EQ(rows.selection_count(member), 1);
    NETW_CHECK_EQ(int(rows.resolve(member, proxy_copy())), int(Mode::ACTIVE));

    CHECK(rows.erase_selected(member, second));
    NETW_CHECK_EQ(int(rows.resolve(member, proxy_copy())), int(Mode::PROXY));
    owner.free(member);
    owner.free(first);
    owner.free(second);
}

struct Precedence {
    const char *label;
    bool selected;
    Facts facts;
    Mode expected;
};

Facts authored_here() {
    Facts facts = proxy_copy();
    facts.session_authority_here = true;
    return facts;
}

Facts predicted_here() {
    Facts facts;
    facts.declared = true;
    facts.predicted = true;
    facts.input_rows = true;
    facts.controller_here = true;
    return facts;
}

Facts latched_copy() {
    Facts facts;
    facts.declared = true;
    facts.predicted = true;
    facts.input_rows = true;
    facts.fallback_latched = true;
    return facts;
}

Facts closed_copy() {
    Facts facts = latched_copy();
    facts.fallback_latched = false;
    facts.delay_closed = true;
    return facts;
}

Facts inputless_copy() {
    Facts facts;
    facts.declared = true;
    facts.predicted = true;
    return facts;
}

Facts active_replica() {
    Facts facts = proxy_copy();
    facts.replicas = netw::sim::Replicas::ACTIVE;
    return facts;
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] X02 a selection counted in the book "
    "runs a copy ACTIVE only where nothing outranks it"
) {
    const Precedence rows_table[] = {
        {"unselected copy", false, proxy_copy(), Mode::PROXY},
        {"selected copy", true, proxy_copy(), Mode::ACTIVE},
        {"replicas ACTIVE, nobody selects",
         false,
         active_replica(),
         Mode::ACTIVE},
        {"selected, authored here", true, authored_here(), Mode::AUTHORITY},
        {"selected, predicted here", true, predicted_here(), Mode::PREDICT},
        {"selected, fallback latched", true, latched_copy(), Mode::PROXY},
        {"selected, delay closed", true, closed_copy(), Mode::PROXY},
        {"selected, inputless", true, inputless_copy(), Mode::ACTIVE},
    };
    RID_Owner<int> owner;
    const RID subject = owner.make_rid(0);
    for (const Precedence &row : rows_table) {
        NETW_FORMAT_TEXT(label_text, row.label);
        CAPTURE(label_text);
        const RID member = owner.make_rid(1);
        Rows rows;
        rows.ensure(member).declaration.replicas = row.facts.replicas;
        if (row.selected) {
            REQUIRE(rows.note_selected(member, subject));
        }
        NETW_CHECK_EQ(int(rows.resolve(member, row.facts)), int(row.expected));
        owner.free(member);
    }
    owner.free(subject);
}

TEST_CASE(
    "[Networked][Sim][Hosted][Select] a released subject leaves no member "
    "selected, and names each member its release moved"
) {
    RID_Owner<int> owner;
    const RID subject = owner.make_rid(0);
    const RID member = owner.make_rid(1);
    const RID other = owner.make_rid(2);
    Rows rows;
    rows.resolve(member, proxy_copy());

    netw::sim::Row &row = rows.ensure(subject);
    LocalVector<Candidate> chosen;
    chosen.push_back(candidate(int64_t(member.get_id()), 1.0, Pick::CHOSEN));
    row.selection.commit(chosen, 0);
    REQUIRE(rows.note_selected(member, subject));
    REQUIRE(rows.note_selected(member, other));
    CHECK((rows.entity_of_key(int64_t(member.get_id())) == member));

    const LocalVector<RID> moved = rows.release(subject);
    NETW_REQUIRE_EQ(moved.size(), 1);
    CHECK((moved[0] == member));
    NETW_CHECK_EQ(rows.selection_count(member), 1);
    CHECK((rows.row_of(subject) == nullptr));

    const LocalVector<RID> again = rows.drop_selections(subject);
    NETW_CHECK_EQ(again.size(), 0);
    owner.free(subject);
    owner.free(member);
    owner.free(other);
}

} // namespace TestNetwSimSelectLaws
