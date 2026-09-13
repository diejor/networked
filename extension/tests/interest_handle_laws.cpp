#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interest_handle.hpp"
#include "netw/api/interest_layer.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "support/netw_call_log.h"

namespace TestNetwInterestHandleLaws {

using namespace godot;
using netw::NetwEntity;
using netw::NetwInterestHandle;
using netw::NetwInterestLayer;
using netw::NetwMultiplayer;
using netw_test::CallLog;

struct Owned {
    Node *owner = nullptr;
    Ref<NetwEntity> entity;
    Ref<NetwInterestHandle> interest;
};

Owned an_entity(const char *p_name) {
    Owned made;
    made.owner = memnew(Node);
    made.owner->set_name(p_name);
    made.entity = NetwEntity::ensure(made.owner);
    made.interest = made.entity->get_interest();
    return made;
}

TEST_CASE(
    "[Networked][Interest][Hosted] IH1 the facet is cached, so two reads of "
    "entity.interest answer the same handle and a caller may register on "
    "either"
) {
    Owned made = an_entity("Ent");

    CHECK(made.entity->get_interest() == made.entity->get_interest());
    CHECK(made.entity->get_interest() == made.interest);

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Interest][Hosted][SceneTree] IH2 a declared label survives a "
    "tree exit and reapplies on re-entry, and layer_ids answers a copy a "
    "caller cannot clear out from under the handle"
) {
    Ref<NetwMultiplayer> core;
    core.instantiate();
    Node *root = netw::gd::scene_root();
    CHECK(root != nullptr);
    if (root == nullptr) {
        return;
    }
    Node *host = memnew(Node);
    host->set_name("Host");
    root->add_child(host);

    Owned made = an_entity("Player");
    REQUIRE(made.interest.is_valid());
    made.interest->join(StringName("a"));
    made.interest->join(StringName("b"));
    made.interest->join(StringName("a"));

    TypedArray<StringName> declared = made.interest->layer_ids();
    NETW_CHECK_EQ(declared.size(), 2);
    declared.clear();
    NETW_CHECK_EQ(made.interest->layer_ids().size(), 2);

    made.interest->leave(StringName("a"));
    made.interest->leave(StringName("a"));
    NETW_CHECK_EQ(made.interest->layer_ids().size(), 1);
    CHECK(StringName(made.interest->layer_ids()[0]) == StringName("b"));

    host->remove_child(made.owner);
    root->remove_child(host);
    memdelete(host);
    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IH3 an observer callback is handed the peer "
    "alone, because the layer an observation arrived through is the session's "
    "business rather than the watcher's"
) {
    Owned made = an_entity("Ent");
    REQUIRE(made.interest.is_valid());
    const CallLog heard;
    made.interest->on_observed(heard.callable("entered"));
    made.interest->on_unobserved(heard.callable("left"));

    made.entity
        ->emit_signal(StringName("observer_entered"), StringName("sight"), 7);
    made.entity
        ->emit_signal(StringName("observer_left"), StringName("sight"), 7);

    NETW_CHECK_EQ(heard.count("entered"), 1);
    NETW_CHECK_EQ(heard.count("left"), 1);
    NETW_CHECK_EQ(made.interest->reports_observers(), true);

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IH4 a leave policy overrides the fallback "
    "for its own layer only, and a CUSTOM policy keeps the callback it was "
    "declared with"
) {
    Owned made = an_entity("Ent");
    REQUIRE(made.interest.is_valid());
    const CallLog heard;
    const Callable custom = heard.callable("custom");

    made.interest->on_leave_policy(
        StringName("stealth"),
        NetwMultiplayer::LEAVE_POLICY_RETAIN,
        Callable()
    );

    NETW_CHECK_EQ(
        made.interest->leave_policy_for(
            StringName("stealth"),
            netw::interest::Decl::LEAVE_HIDE
        ),
        int64_t(netw::interest::Decl::LEAVE_RETAIN)
    );
    NETW_CHECK_EQ(
        made.interest->leave_policy_for(
            StringName("other"),
            netw::interest::Decl::LEAVE_HIDE
        ),
        int64_t(netw::interest::Decl::LEAVE_HIDE)
    );

    made.interest->on_leave_policy(
        StringName("stealth"),
        NetwMultiplayer::LEAVE_POLICY_CUSTOM,
        custom
    );

    NETW_CHECK_EQ(
        made.interest->leave_policy_for(
            StringName("stealth"),
            netw::interest::Decl::LEAVE_HIDE
        ),
        int64_t(netw::interest::Decl::LEAVE_CUSTOM)
    );
    CHECK(made.interest->custom_leave_for(StringName("stealth")) == custom);

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IH5 join carries the two policies for the "
    "layer it joins, so one declaration says which layer and what leaving it "
    "means rather than three calls that could name different layers"
) {
    Owned made = an_entity("Policed");

    made.interest->join(
        StringName("team:red"),
        netw::interest::Decl::LEAVE_RETAIN,
        netw::interest::Decl::PERCEPTION_SHOW
    );
    made.interest->join(StringName("sight"));

    NETW_CHECK_EQ(made.interest->layer_ids().size(), 2);
    NETW_CHECK_EQ(
        made.interest->leave_policy_for(
            StringName("team:red"),
            netw::interest::Decl::LEAVE_HIDE
        ),
        int64_t(netw::interest::Decl::LEAVE_RETAIN)
    );
    NETW_CHECK_EQ(
        made.interest->leave_policy_for(
            StringName("sight"),
            netw::interest::Decl::LEAVE_HIDE
        ),
        int64_t(netw::interest::Decl::LEAVE_HIDE)
    );

    memdelete(made.owner);
}

TEST_CASE(
    "[Networked][Interest][Hosted] IH6 a callback with no layer named reaches "
    "every layer the entity has joined, and reads that membership rather than "
    "the expression it was written in, so a later join is not reached back to"
) {
    Owned made = an_entity("Watched");
    CallLog heard;

    made.interest->join(StringName("a"));
    made.interest->join(StringName("b"));
    made.interest->on_enter(heard.callable("enter"), StringName());
    made.interest->join(StringName("c"));

    made.interest->dispatch_enter(StringName("a"), 7);
    made.interest->dispatch_enter(StringName("b"), 7);
    made.interest->dispatch_enter(StringName("c"), 7);

    NETW_CHECK_EQ(heard.count("enter"), 2);

    memdelete(made.owner);
}

} // namespace TestNetwInterestHandleLaws
