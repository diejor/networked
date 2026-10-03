#include "support/netw_test.h"

#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "netw/sim/body.hpp"
#include "netw/sim/contact.hpp"
#include "netw/sim/row.hpp"

namespace TestNetwSimContactWalkLaws {

using namespace godot;
using netw::sim::Mode;
using netw::sim::Row;
using netw::sim::Rows;

Row &awake_body(Rows &r_rows, const RID &p_entity, Mode p_mode) {
    Row &row = r_rows.ensure(p_entity);
    row.mode = p_mode;
    row.bodies.held.push_back(netw::sim::AuthoredBody());
    r_rows.set_awake(row, true);
    return row;
}

bool walks(Rows &r_rows, const Row &p_row) {
    LocalVector<uint32_t> walked;
    r_rows.contact_walk(walked);
    return walked.find(p_row.index) >= 0;
}

TEST_CASE(
    "[Networked][Sim][Hosted][Law] an awake body is walked for contacts only "
    "while it declares contact behaviour, fences, or reports contacts"
) {
    RID_Owner<int> owner;
    Rows rows;
    const RID plain = owner.make_rid(0);
    const RID claimer = owner.make_rid(1);
    const RID resting = owner.make_rid(2);
    const RID fencing = owner.make_rid(3);
    const RID reporting = owner.make_rid(4);

    Row &plain_row = awake_body(rows, plain, Mode::AUTHORITY);
    Row &claimer_row = awake_body(rows, claimer, Mode::AUTHORITY);
    claimer_row.declaration.claim_on_contact = true;
    Row &resting_row = awake_body(rows, resting, Mode::AUTHORITY);
    resting_row.declaration.release_on_rest = 1.0;
    Row &fencing_row = awake_body(rows, fencing, Mode::ACTIVE);
    Row &reporting_row = awake_body(rows, reporting, Mode::AUTHORITY);
    reporting_row.contact.reports = true;

    LocalVector<uint32_t> first;
    rows.contact_walk(first);
    NETW_CHECK_EQ(int(first.size()), 5);

    CHECK_FALSE(walks(rows, plain_row));
    CHECK(walks(rows, claimer_row));
    CHECK(walks(rows, resting_row));
    CHECK(walks(rows, fencing_row));
    CHECK(walks(rows, reporting_row));

    for (const RID &made : {plain, claimer, resting, fencing, reporting}) {
        owner.free(made);
    }
}

TEST_CASE(
    "[Networked][Sim][Hosted][SceneTree][Law] a body reports contacts once "
    "the game asks the engine for any"
) {
    Node *root = netw::gd::scene_root();
    REQUIRE(root != nullptr);
    RigidBody3D *body = memnew(RigidBody3D);
    root->add_child(body);
    netw::sim::Bodies bodies;
    netw::sim::AuthoredBody held;
    held.node = body->get_instance_id();
    bodies.held.push_back(held);

    CHECK_FALSE(netw::sim::reports_contacts(bodies));
    body->set_max_contacts_reported(4);
    CHECK(netw::sim::reports_contacts(bodies));

    root->remove_child(body);
    memdelete(body);
}

} // namespace TestNetwSimContactWalkLaws
