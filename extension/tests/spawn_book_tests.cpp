#include "support/netw_test.h"

#include "godot/node.hpp"
#include "netw/liveness_core.hpp"
#include "netw/spawn/book.hpp"

namespace TestSpawnBook {

using namespace godot;
using netw::spawn::Book;
using netw::spawn::Record;

Record at(int64_t route, int64_t parent_route = 0) {
    Record record;
    record.set_route(route);
    record.set_parent_route(parent_route);
    return record;
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a record is armed under its own route and "
    "taken exactly once"
) {
    Book book;

    book.arm(at(7));

    CHECK(book.has_armed(7));
    NETW_CHECK_EQ(book.armed_count(), 1);
    Record taken;
    CHECK(book.take_armed(7, taken));
    NETW_CHECK_EQ(taken.get_route(), int64_t(7));
    CHECK_FALSE(book.has_armed(7));
    CHECK_FALSE(book.take_armed(7, taken));
    NETW_CHECK_EQ(book.armed_count(), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a node is booked while it is armed and while "
    "it is issued, and a node neither book holds is not"
) {
    Book book;
    Node *armed = memnew(Node);
    Node *issued = memnew(Node);
    Node *stranger = memnew(Node);

    Record armed_record = at(7);
    armed_record.bind_node(armed);
    book.arm(armed_record);

    Record issued_record = at(8);
    issued_record.bind_node(issued);
    book.issue(issued_record);

    CHECK(book.books_node(armed));
    CHECK(book.books_node(issued));
    CHECK_FALSE(book.books_node(stranger));
    CHECK_FALSE(book.books_node(nullptr));

    Record taken;
    book.take_armed(7, taken);
    CHECK_FALSE(book.books_node(armed));
    CHECK(book.books_node(issued));

    memdelete(armed);
    memdelete(issued);
    memdelete(stranger);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the issued book answers in arm order, which is "
    "not ancestry order"
) {
    Book book;
    book.issue(at(1));
    book.issue(at(2));
    book.issue(at(3));

    PackedInt64Array armed_order;
    armed_order.push_back(1);
    armed_order.push_back(2);
    armed_order.push_back(3);
    CHECK(book.spawned_routes() == armed_order);
    CHECK(book.ancestry_order() == armed_order);

    book.spawned_of(2)->set_parent_route(3);

    PackedInt64Array by_ancestry;
    by_ancestry.push_back(1);
    by_ancestry.push_back(3);
    by_ancestry.push_back(2);
    CHECK(book.spawned_routes() == armed_order);
    CHECK(book.ancestry_order() == by_ancestry);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a chain armed back to front still comes out "
    "root first"
) {
    Book book;
    book.issue(at(1, 2));
    book.issue(at(2, 3));
    book.issue(at(3, 4));
    book.issue(at(4, 0));

    PackedInt64Array expected;
    expected.push_back(4);
    expected.push_back(3);
    expected.push_back(2);
    expected.push_back(1);
    CHECK(book.ancestry_order() == expected);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] an issued route is dropped by route and its "
    "record answers nothing after"
) {
    Book book;
    book.issue(at(5));

    CHECK(book.has_spawned(5));
    NETW_CHECK_EQ(book.spawned_count(), 1);
    CHECK(book.drop_spawned(5));
    CHECK_FALSE(book.drop_spawned(5));
    CHECK_FALSE(book.has_spawned(5));
    CHECK(book.spawned_of(5) == nullptr);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a received route is claimed while its node is "
    "still orphaned"
) {
    Book book;
    Node *node = memnew(Node);

    CHECK_FALSE(book.is_recv(3));
    book.enroll_recv(3, node);
    CHECK(book.is_recv(3));
    NETW_CHECK_EQ(book.recv_count(), 1);

    CHECK(book.drop_recv(3));
    CHECK_FALSE(book.drop_recv(3));
    CHECK_FALSE(book.is_recv(3));

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] the arm, issue and received books are three "
    "books, and clearing drops all of them"
) {
    Book book;
    Node *node = memnew(Node);
    book.arm(at(1));
    book.issue(at(2));
    book.enroll_recv(3, node);

    CHECK_FALSE(book.has_spawned(1));
    CHECK_FALSE(book.has_armed(2));
    CHECK_FALSE(book.has_armed(3));

    book.clear();

    NETW_CHECK_EQ(book.armed_count(), 0);
    NETW_CHECK_EQ(book.spawned_count(), 0);
    NETW_CHECK_EQ(book.recv_count(), 0);

    memdelete(node);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a despawn is ordered children before the "
    "ancestor that contains them"
) {
    Book book;
    book.issue(at(1));
    book.issue(at(2, 1));
    book.issue(at(3, 2));
    book.issue(at(4, 1));

    const PackedInt64Array order = book.despawn_order(1);

    NETW_CHECK_EQ(int(order.size()), 4);
    NETW_CHECK_EQ(order[order.size() - 1], int64_t(1));
    int64_t seen_three = -1;
    int64_t seen_two = -1;
    for (int at_index = 0; at_index < order.size(); ++at_index) {
        if (order[at_index] == 3) {
            seen_three = at_index;
        }
        if (order[at_index] == 2) {
            seen_two = at_index;
        }
    }
    NETW_CHECK_LT(seen_three, seen_two);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a despawn carries its own subtree and nothing "
    "beside it"
) {
    Book book;
    book.issue(at(1));
    book.issue(at(2, 1));
    book.issue(at(9));
    book.issue(at(10, 9));

    const PackedInt64Array order = book.despawn_order(1);

    NETW_CHECK_EQ(int(order.size()), 2);
    CHECK(order.has(1));
    CHECK(order.has(2));
    CHECK_FALSE(order.has(9));
    CHECK_FALSE(order.has(10));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a route the book never issued despawns "
    "nothing"
) {
    Book book;
    book.issue(at(1));

    NETW_CHECK_EQ(int(book.despawn_order(77).size()), 0);
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a child is admitted only where its "
    "materialized parent already is"
) {
    Book book;
    book.issue(at(1));
    book.issue(at(2, 1));

    CHECK_FALSE(book.parent_admits(2, 7));

    book.spawned_of(1)->add_recipient(7);

    CHECK(book.parent_admits(2, 7));
    CHECK_FALSE(book.parent_admits(2, 8));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a rootless record and one whose parent the "
    "book does not hold are both unclamped"
) {
    Book book;
    book.issue(at(1));
    book.issue(at(2, 99));

    CHECK(book.parent_admits(1, 7));
    CHECK(book.parent_admits(2, 7));
    CHECK(book.parent_admits(77, 7));
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a recipe names the stem an auto-assigned "
    "entity id is built from"
) {
    Record scene = at(1);
    scene.set_recipe(Book::RECIPE_SCENE);
    scene.set_scene_path("res://levels/Arena.tscn");

    Record fn = at(2);
    fn.set_recipe(Book::RECIPE_FN);
    fn.set_fn_method(StringName("make_player"));

    Record registered = at(3);
    registered.set_recipe(Book::RECIPE_FN_REGISTRY);
    registered.set_fn_registry_id(StringName("bot"));

    CHECK(Book::recipe_base(&scene, "res://x/Y.tscn", "N") == "Arena");
    CHECK(Book::recipe_base(&fn, "res://x/Y.tscn", "N") == "make_player");
    CHECK(Book::recipe_base(&registered, "res://x/Y.tscn", "N") == "bot");
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a recipe with no stem of its own falls to the "
    "node's scene and then to its name"
) {
    Record spawner = at(1);
    spawner.set_recipe(Book::RECIPE_SPAWNER);

    CHECK(Book::recipe_base(&spawner, "res://x/Body.tscn", "Node2") == "Body");
    CHECK(Book::recipe_base(&spawner, "", "Node2") == "Node2");
    CHECK(Book::recipe_base(nullptr, "", "Node2") == "Node2");
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a spawn for a route already standing here is a "
    "duplicate, and one for a DEAD route is not"
) {
    Book book;

    CHECK(book.spawn_is_duplicate(1, netw::NetwLivenessCore::STATE_LIVE));
    CHECK(book.spawn_is_duplicate(1, netw::NetwLivenessCore::STATE_LINGERING));
    CHECK_FALSE(book.spawn_is_duplicate(1, netw::NetwLivenessCore::STATE_DEAD));
    CHECK_FALSE(
        book.spawn_is_duplicate(1, netw::NetwLivenessCore::STATE_UNKNOWN)
    );
}

TEST_CASE(
    "[Networked][Spawn][Hosted] a route this peer already materialized is a "
    "duplicate whatever its liveness says"
) {
    Book book;
    Node *node = memnew(Node);
    book.enroll_recv(5, node);

    CHECK(book.spawn_is_duplicate(5, netw::NetwLivenessCore::STATE_DEAD));
    CHECK(book.spawn_is_duplicate(5, netw::NetwLivenessCore::STATE_UNKNOWN));
    CHECK_FALSE(book.spawn_is_duplicate(6, netw::NetwLivenessCore::STATE_DEAD));

    memdelete(node);
}

} // namespace TestSpawnBook
