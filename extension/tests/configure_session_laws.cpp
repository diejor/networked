#include "support/netw_test.h"

#include "support/fault_log.h"

#include "godot/multiplayer.hpp"
#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/context.hpp"
#include "netw/api/link_conditions.hpp"
#include "netw/api/loopback.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"
#include "netw/api/session_config.hpp"
#include "netw/session_decl.hpp"

namespace TestNetwConfigureSession {

using namespace godot;
using netw::MultiplayerViewport;
using netw::Netw;
using netw::NetwMultiplayer;
using netw::NetwSessionConfig;
using netw_test::FaultWatch;

namespace {

struct Mounted {
    MultiplayerViewport *tree = nullptr;
    Node *orphan = nullptr;

    explicit Mounted(const char *p_name) {
        tree = memnew(MultiplayerViewport);
        tree->set_name(StringName(p_name));
        netw::gd::scene_root()->add_child(tree);
    }

    ~Mounted() {
        api()->NETW_API_VIRTUAL(set_multiplayer_peer)(Ref<MultiplayerPeer>());
        tree->get_parent()->remove_child(tree);
        memdelete(tree);
        if (orphan != nullptr) {
            memdelete(orphan);
        }
    }

    NetwMultiplayer *api() const {
        return tree->get_api().ptr();
    }

    Ref<MultiplayerPeer> peer() const {
        return api()->NETW_API_VIRTUAL(get_multiplayer_peer)();
    }

    void assign(const Ref<MultiplayerPeer> &p_peer) const {
        api()->NETW_API_VIRTUAL(set_multiplayer_peer)(p_peer);
    }
};

Node *child_named(Node *p_parent, const char *p_name) {
    Node *node = memnew(Node);
    node->set_name(StringName(p_name));
    p_parent->add_child(node);
    return node;
}

Ref<MultiplayerPeer> a_server() {
    Ref<netw::LocalMultiplayerPeer> made;
    made.instantiate();
    NETW_CHECK_EQ(int(made->create_server()), int(OK));
    return made;
}

int64_t tag_of(const char *p_app) {
    return NetwMultiplayer::session_app_tag(StringName(p_app));
}

struct Configuring {
    Mounted *branch = nullptr;
    Ref<MultiplayerPeer> intruder;
    int calls = 0;
    int64_t tag_seen = 0;
    int64_t tickrate_seen = 0;
};

Configuring &configuring() {
    static Configuring held;
    return held;
}

void assign_while_configuring() {
    Configuring &seen = configuring();
    seen.calls += 1;
    seen.tag_seen = seen.branch->api()->auth_app_tag_of();
    seen.tickrate_seen = int64_t(seen.branch->api()->clock_get_param(
        NetwMultiplayer::CLOCK_PARAM_TICKRATE
    ));
    seen.branch->assign(seen.intruder);
}

} // namespace

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS3 the fluent tail reaches the "
    "auth app tag and not only the stored app id, because both readers are "
    "handed the same owned value at consumption"
) {
    Mounted branch("CS3Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app(StringName("tagged-arena"));

    branch.api()->config_settle();

    NETW_CHECK_EQ(branch.api()->auth_app_tag_of(), tag_of("tagged-arena"));
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS5 completed configuration "
    "is applied before a direct peer assignment returns, and the deferred "
    "pass and a repeated assignment that follow change nothing"
) {
    Mounted branch("CS5Tree");
    const Ref<NetwSessionConfig> draft
        = Netw::configure_session(child_named(branch.tree, "Session"));
    draft->app(StringName("still-authoring"));
    const Ref<MultiplayerPeer> offered = a_server();
    const FaultWatch watch;

    branch.assign(offered);

    CHECK(branch.peer() == offered);
    NETW_CHECK_EQ(branch.api()->auth_app_tag_of(), tag_of("still-authoring"));
    CHECK(branch.api()->config_is_consumed(
        netw::session_decl::KIND_SESSION_CONFIG
    ));

    branch.api()->config_settle();
    branch.assign(offered);

    CHECK(branch.peer() == offered);
    NETW_CHECK_EQ(branch.api()->auth_app_tag_of(), tag_of("still-authoring"));
    NETW_CHECK_EQ(watch.log->warnings_logged(), 0);
    NETW_CHECK_EQ(watch.log->errors_logged(), 0);

    draft->app(StringName("too-late"));
    NETW_CHECK_EQ(
        watch.log->warning_count_containing("after the session started"),
        1
    );
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS6 settings declared in a "
    "constructor before the node enters the tree are applied by a peer "
    "assignment in the first ready, with no frame in between"
) {
    Mounted branch("CS6Tree");
    Node *declaring = memnew(Node);
    declaring->set_name(StringName("Game"));
    Netw::configure_clock(declaring)->ticks_per_second(60);
    CHECK_FALSE(branch.api()->clock_is_configured());

    branch.tree->add_child(declaring);
    const Ref<MultiplayerPeer> offered = a_server();
    branch.assign(offered);

    CHECK(branch.peer() == offered);
    CHECK(branch.api()->clock_is_configured());
    const int tickrate
        = branch.api()->clock_get_param(NetwMultiplayer::CLOCK_PARAM_TICKRATE);
    NETW_CHECK_EQ(tickrate, 60);
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS7 settings the session cannot "
    "apply refuse the new peer before the standing one is torn down, leave "
    "every draft open, and a retry after the fix applies them"
) {
    Mounted branch("CS7Tree");
    const Ref<MultiplayerPeer> standing = a_server();
    branch.assign(standing);
    REQUIRE(branch.peer() == standing);
    const int64_t state = int64_t(branch.api()->session_get_state());

    Node *first = child_named(branch.tree, "First");
    const FaultWatch watch;
    String said;
    Node *removable = nullptr;
    Ref<NetwSessionConfig> open_draft;

    SUBCASE("two nodes declare the session") {
        open_draft = Netw::configure_session(first);
        open_draft->app(StringName("first-app"));
        removable = child_named(branch.tree, "Second");
        Netw::configure_session(removable)->app(StringName("other-app"));
        said = "more than one node";
    }
    SUBCASE("the declaration holds something other than settings") {
        netw::session_decl::declare(
            first,
            netw::session_decl::KIND_SESSION_CONFIG,
            Callable(),
            Variant(int64_t(5)),
            "configure_session"
        );
        said = "cannot be read";
    }

    const Ref<MultiplayerPeer> offered = a_server();
    branch.assign(offered);
    branch.assign(offered);

    CHECK(branch.peer() == standing);
    NETW_CHECK_EQ(int64_t(branch.api()->session_get_state()), state);
    CHECK_FALSE(branch.api()->config_is_consumed(
        netw::session_decl::KIND_SESSION_CONFIG
    ));
    CHECK(watch.log->error_count_containing(said) >= 1);

    if (open_draft.is_valid()) {
        open_draft->app(StringName("fixed-app"));
        branch.tree->remove_child(removable);
        branch.orphan = removable;
    } else {
        Ref<NetwSessionConfig> repaired;
        repaired.instantiate();
        repaired->app(StringName("fixed-app"));
        netw::session_decl::declare(
            first,
            netw::session_decl::KIND_SESSION_CONFIG,
            Callable(),
            Variant(repaired),
            "configure_session"
        );
    }
    branch.assign(offered);

    CHECK(branch.peer() == offered);
    NETW_CHECK_EQ(branch.api()->auth_app_tag_of(), tag_of("fixed-app"));
}

TEST_CASE(
    "[Networked][Session][Hosted][SceneTree] CS8 a peer assigned from a "
    "callback that reacts to the settings being applied is refused as busy, "
    "while that callback already reads every applied setting"
) {
    Mounted branch("CS8Tree");
    Netw::configure_session(child_named(branch.tree, "Session"))
        ->app(StringName("busy-app"));
    Netw::configure_clock(child_named(branch.tree, "Clock"))
        ->ticks_per_second(24);
    configuring() = Configuring();
    configuring().branch = &branch;
    configuring().intruder = a_server();
    branch.api()->observe(
        NetwMultiplayer::CLOCK_CONFIGURED,
        callable_mp_static(&assign_while_configuring)
    );
    const Ref<MultiplayerPeer> offered = a_server();
    const FaultWatch watch;

    branch.assign(offered);

    NETW_CHECK_EQ(configuring().calls, 1);
    NETW_CHECK_EQ(configuring().tag_seen, tag_of("busy-app"));
    NETW_CHECK_EQ(configuring().tickrate_seen, 24);
    CHECK(branch.peer() == offered);
    NETW_CHECK_EQ(
        watch.log->error_count_containing("while the session applied"),
        1
    );
    configuring() = Configuring();
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session][SceneTree] CS9 declared link conditions shape the "
    "peer the same assignment installs"
) {
    Mounted branch("CS9Tree");
    Ref<netw::NetwLinkConditions> conditions;
    conditions.instantiate();

    Netw::configure_session(child_named(branch.tree, "Session"))
        ->link(conditions);
    const Ref<MultiplayerPeer> offered = a_server();

    branch.assign(offered);

    CHECK(branch.peer() == offered);
    const Ref<MultiplayerPeer> polled
        = branch.api()->session_get_inner()->get_multiplayer_peer();
    REQUIRE(polled.is_valid());
    CHECK(polled != offered);
}

#endif

} // namespace TestNetwConfigureSession
