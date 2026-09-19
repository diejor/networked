#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/entity_record.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/participant.hpp"
#include "netw/liveness_core.hpp"
#include "netw/scene_core.hpp"
#include "netw/scene_membership.hpp"

namespace TestNetwSceneMembershipReasonLaws {

using namespace godot;
using netw::NetwMultiplayer;
using netw::SceneMembership;

constexpr int64_t ONE = 11;
constexpr int64_t TWO = 12;

Ref<netw::NetwLivenessCore> handles() {
    Ref<netw::NetwLivenessCore> minted;
    minted.instantiate();
    return minted;
}

int subscribed_edges(const SceneMembership &p_book) {
    int total = 0;
    const LocalVector<SceneMembership::Edge> &edges = p_book.pending_edges();
    for (uint32_t at = 0; at < edges.size(); ++at) {
        total += edges[at].subscribed ? 1 : 0;
    }
    return total;
}

int removals_for(
    const SceneMembership &p_book,
    SceneMembership::Removal p_removal
) {
    int total = 0;
    const LocalVector<SceneMembership::Edge> &edges = p_book.pending_edges();
    for (uint32_t at = 0; at < edges.size(); ++at) {
        total += edges[at].removal == p_removal ? 1 : 0;
    }
    return total;
}

Ref<NetwMultiplayer> peered_core() {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Ref<netw::LocalMultiplayerPeer> peer;
    peer.instantiate();
    peer->create_server();
    core->NETW_API_VIRTUAL(set_multiplayer_peer)(peer);
    return core;
}

Ref<netw::NetwPlayer> adopted(
    const Ref<NetwMultiplayer> &p_core,
    int64_t p_peer
) {
    Ref<netw::NetwPlayer> row;
    row.instantiate();
    p_core->player_adopt(p_peer, row);
    REQUIRE(p_core->player_has(p_peer));
    return row;
}

struct Declared {
    Ref<netw::NetwEntity> wrapper;
    RID handle;
    Node *owner = nullptr;
};

Declared declare_scene(
    const Ref<NetwMultiplayer> &p_core,
    Node *p_parent,
    const char *p_stem
) {
    Declared made;
    made.owner = memnew(Node);
    made.owner->set_name(p_stem);
    p_parent->add_child(made.owner);
    made.wrapper.instantiate();
    made.handle = p_core->get_liveness_core()->entity_create();
    netw::NetwEntityRecord *record = made.wrapper->get_record();
    record->adopt_handle(made.handle);
    record->set_declares_scene(true);
    const int64_t route = p_core->get_liveness_core()->reserve_route();
    REQUIRE(p_core->liveness_bind(
        made.handle,
        route,
        made.wrapper,
        record,
        made.owner
    ));
    made.wrapper->attach_to(made.owner);
    REQUIRE(!p_core->scene_layer_id(made.handle).is_empty());
    return made;
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM1 two bodies of one player in one "
    "scene are two reasons, so the first one leaving keeps the subscription "
    "and the last one leaving ends it, which is the count a single boolean "
    "could not carry"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();

    CHECK(book.body_enter(ONE, arena, 101));
    CHECK(book.body_enter(ONE, arena, 102));
    NETW_CHECK_EQ(book.bodies_in(ONE, arena), 2);
    NETW_CHECK_EQ(subscribed_edges(book), 1);

    CHECK(book.body_exit(ONE, arena, 101));
    CHECK(book.subscribes(ONE, arena));
    NETW_CHECK_EQ(removals_for(book, SceneMembership::REMOVAL_LAST_BODY), 0);

    CHECK(book.body_exit(ONE, arena, 102));
    CHECK_FALSE(book.subscribes(ONE, arena));
    NETW_CHECK_EQ(removals_for(book, SceneMembership::REMOVAL_LAST_BODY), 1);
    NETW_CHECK_EQ(book.size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM2 one body entering the same scene twice is "
    "one reason and announces one edge, so a repeated notification, a "
    "deferred flush and a duplicate frame all settle to the same answer"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();

    CHECK(book.body_enter(ONE, arena, 101));
    CHECK_FALSE(book.body_enter(ONE, arena, 101));
    CHECK_FALSE(book.body_enter(ONE, arena, 101));
    NETW_CHECK_EQ(book.bodies_in(ONE, arena), 1);
    NETW_CHECK_EQ(book.pending_edges().size(), 1);

    CHECK(book.body_exit(ONE, arena, 101));
    CHECK_FALSE(book.body_exit(ONE, arena, 101));
    NETW_CHECK_EQ(book.pending_edges().size(), 2);
    NETW_CHECK_EQ(book.size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM3 a player subscribes to several "
    "scenes at once, so watching one scene neither releases another nor "
    "touches a second player's reasons"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();
    const RID barn = minted->entity_create();

    CHECK(book.body_enter(ONE, arena, 101));
    CHECK(book.watch(ONE, barn));
    CHECK(book.body_enter(TWO, arena, 201));

    CHECK(book.subscribes(ONE, arena));
    CHECK(book.subscribes(ONE, barn));
    NETW_CHECK_EQ(book.subscriptions_of(ONE), 2);

    const PackedInt64Array members = book.members_of(arena);
    NETW_CHECK_EQ(members.size(), 2);
    NETW_CHECK_EQ(members[0], ONE);
    NETW_CHECK_EQ(members[1], TWO);

    CHECK(book.unwatch(ONE, barn));
    CHECK(book.subscribes(ONE, arena));
    CHECK_FALSE(book.subscribes(ONE, barn));
    NETW_CHECK_EQ(removals_for(book, SceneMembership::REMOVAL_UNWATCHED), 1);
    NETW_CHECK_EQ(book.subscriptions_of(TWO), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM4 an explicit watch and a body in the same "
    "scene are separate reasons, so unwatching leaves a live body's "
    "subscription standing and the body leaving leaves the watch standing"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();

    CHECK(book.watch(ONE, arena));
    CHECK(book.body_enter(ONE, arena, 101));
    NETW_CHECK_EQ(book.bodies_in(ONE, arena), 1);
    CHECK(book.watches(ONE, arena));

    CHECK(book.unwatch(ONE, arena));
    CHECK(book.subscribes(ONE, arena));
    CHECK_FALSE(book.watches(ONE, arena));
    NETW_CHECK_EQ(book.pending_edges().size(), 1);

    CHECK(book.watch(ONE, arena));
    CHECK(book.body_exit(ONE, arena, 101));
    CHECK(book.subscribes(ONE, arena));
    NETW_CHECK_EQ(book.pending_edges().size(), 1);
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM5 a membership that ends and a scene that "
    "retires each drop every reason they own and name themselves in the edge "
    "they publish, so a reader of the edge knows why a viewer left"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();
    const RID barn = minted->entity_create();

    CHECK(book.body_enter(ONE, arena, 101));
    CHECK(book.watch(ONE, barn));
    CHECK(book.body_enter(TWO, barn, 201));
    book.clear_edges();

    book.forget_member(ONE);
    NETW_CHECK_EQ(book.subscriptions_of(ONE), 0);
    NETW_CHECK_EQ(
        removals_for(book, SceneMembership::REMOVAL_MEMBER_GONE),
        2
    );
    CHECK(book.subscribes(TWO, barn));
    book.clear_edges();

    book.retire_scene(barn);
    NETW_CHECK_EQ(book.size(), 0);
    NETW_CHECK_EQ(
        removals_for(book, SceneMembership::REMOVAL_SCENE_RETIRED),
        1
    );
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM6 a reason needs a member and a scene, so "
    "an invalid scene and a zero member are refused rather than stored, and "
    "the watch sentinel is not a body a caller can pass"
) {
    SceneMembership book;
    const Ref<netw::NetwLivenessCore> minted = handles();
    const RID arena = minted->entity_create();

    CHECK_FALSE(book.body_enter(0, arena, 101));
    CHECK_FALSE(book.body_enter(ONE, RID(), 101));
    CHECK_FALSE(book.body_enter(ONE, arena, SceneMembership::WATCH));
    CHECK_FALSE(book.body_exit(ONE, arena, SceneMembership::WATCH));
    CHECK_FALSE(book.watch(0, arena));
    CHECK_FALSE(book.watch(ONE, RID()));
    NETW_CHECK_EQ(book.size(), 0);
    NETW_CHECK_EQ(book.pending_edges().size(), 0);
}

TEST_CASE(
    "[Networked][Scene][Hosted] JM8 a live scene's own layer is answered as a "
    "null reference and reports the refusal, a layer no live scene owns is "
    "answered whatever it is named, and renaming the scene root moves the "
    "refusal to the id the root now spells"
) {
    const Ref<NetwMultiplayer> core = peered_core();
    Node *root = memnew(Node);
    const Declared arena = declare_scene(core, root, "Arena");
    const StringName owned = core->scene_layer_id(arena.handle);
    core->get_scene_core()->scene_enter(
        arena.handle,
        StringName("Arena"),
        false
    );
    REQUIRE(core->get_scene_core()->is_live(arena.handle));

    const RID layer = core->interest_layer_create(owned);
    REQUIRE(layer.is_valid());

    ERR_PRINT_OFF;
    CHECK(core->interest_layer_named(owned).is_null());
    CHECK(core->interest_layer_view(layer).is_null());
    CHECK(core->interest_layer(owned).is_null());
    ERR_PRINT_ON;
    CHECK(core->layer_record_named(owned).is_valid());

    const StringName unowned = StringName("scene:Ghost");
    REQUIRE(core->interest_layer_create(unowned).is_valid());
    CHECK(core->interest_layer_named(unowned).is_valid());
    CHECK(core->interest_layer(StringName("stealth")).is_valid());

    arena.owner->set_name("Colosseum");
    const StringName renamed = core->scene_layer_id(arena.handle);
    CHECK(renamed != owned);
    REQUIRE(core->interest_layer_create(renamed).is_valid());

    ERR_PRINT_OFF;
    CHECK(core->interest_layer_named(renamed).is_null());
    ERR_PRINT_ON;
    CHECK(core->interest_layer_named(owned).is_valid());

    memdelete(root);
}

} // namespace TestNetwSceneMembershipReasonLaws
