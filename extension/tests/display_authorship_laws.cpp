#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/mesh_stand.h"
#include "support/netw_cells.h"

#include "godot/node.hpp"
#include "godot/physics_body.hpp"
#include "godot/scene_tree.hpp"
#include "godot/spatial_node.hpp"
#include "godot/templates.hpp"
#include "netw/api/context.hpp"
#include "netw/api/display_handle.hpp"
#include "netw/api/entity.hpp"
#include "netw/api/interpolate.hpp"
#include "netw/api/member_config.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/property_config.hpp"
#include "netw/api/property_set.hpp"
#include "netw/api/sync_pipeline.hpp"
#include "netw/display/book.hpp"
#include "netw/display/decl.hpp"
#include "netw/property_set_builder.hpp"

namespace TestNetwDisplayAuthorshipLaws {

using namespace godot;
using netw::Netw;
using netw::NetwDisplayHandle;
using netw::NetwEntity;
using netw::NetwInterpolate;
using netw::NetwMultiplayer;
using netw::NetwPropertyConfig;
using netw::NetwPropertySet;
using netw_test::MeshStand;
namespace property_set_builder = netw::property_set_builder;
using netw_test::law_broken;
using netw_test::law_held;
using netw_test::LawRowFor;
using netw_test::LawVerdict;

enum Plant {
    PLANT_NONE,
    PLANT_THE_STREAM_IS_PRIVATE,
    PLANT_THE_SERVER_CONTROLS_IT,
};

struct AuthorshipScenario {
    String label;
    int64_t record = NetwPropertySet::RECORD_STATE;
    bool controller_policed = false;
};

AuthorshipScenario a_state_set_the_server_authors() {
    AuthorshipScenario scenario;
    scenario.label = "a-state-set-the-server-authors";
    return scenario;
}

AuthorshipScenario a_broadcast_set_the_controller_authors() {
    AuthorshipScenario scenario;
    scenario.label = "a-broadcast-set-the-controller-authors";
    scenario.record = NetwPropertySet::RECORD_BROADCAST;
    scenario.controller_policed = true;
    return scenario;
}

struct Evidence {
    int64_t server_role = -1;
    int64_t client_role = -1;
    int64_t server_channels = 0;
    int64_t client_channels = 0;
    bool server_authors = false;
    bool client_authors = false;
    bool server_is_authority = false;
    bool client_is_authority = false;
};

const int64_t SUBJECT_ROUTE = 21;

class AuthorshipRun {
    AuthorshipScenario declared;
    Plant planted = PLANT_NONE;
    Evidence read;

    bool plant_is(Plant p_plant) const {
        return planted == p_plant;
    }

    Ref<NetwPropertySet> declare_stream(Node *p_node) const {
        Ref<NetwPropertyConfig> config
            = Netw::configure_property(p_node, StringName("position"), true);
        Ref<NetwInterpolate> spec;
        spec.instantiate();
        config->interpolate(
            netw::gd::array_of(
                spec->lerp()->smooth(0.0)->to(StringName("position"))
            )
        );
        if (declared.record == NetwPropertySet::RECORD_BROADCAST) {
            config->broadcast();
        } else {
            config->state();
        }
        if (declared.controller_policed) {
            config->controller();
        }
        if (plant_is(PLANT_THE_STREAM_IS_PRIVATE)) {
            config->audience(true);
        }

        Dictionary configs;
        configs[StringName("position")] = config;
        const Ref<NetwPropertySet> set
            = property_set_builder::from_property_configs(
                configs,
                declared.record
            );
        REQUIRE(set.is_valid());
        set->set_sealed(true);
        return set;
    }

    void stand_up(
        NetwMultiplayer *p_core,
        Node *p_branch,
        int64_t p_controller,
        int64_t &r_role,
        int64_t &r_channels,
        bool &r_authors,
        bool &r_is_authority
    ) {
        Node2D *body = memnew(Node2D);
        body->set_name("DerivedPlayer");
        p_branch->add_child(body);

        const Ref<NetwEntity> entity = NetwEntity::ensure(body);
        const Ref<NetwPropertySet> set = declare_stream(body);
        NETW_CHECK_EQ(
            int(p_core->sync_pipeline()->register_property_set(body, set)),
            int(OK)
        );
        entity->set_controller(p_controller);
        p_core->liveness_bind_route(SUBJECT_ROUTE, entity.ptr());
        p_core->display_mark_dirty(
            entity->get_rid_handle(),
            netw::display::DIRT_ROLE
        );
        p_core->session_flush_deferred();

        r_role = int64_t(p_core->display_get_track_stat(
            entity->get_rid_handle(),
            StringName(),
            StringName("role")
        ));
        r_channels = int64_t(p_core->display_get_track_stat(
            entity->get_rid_handle(),
            StringName(),
            StringName("channels")
        ));
        r_authors
            = p_core->display_default_authors_streams(entity->get_rid_handle());
        r_is_authority = body->is_multiplayer_authority();
    }

public:
    explicit AuthorshipRun(
        const AuthorshipScenario &p_scenario,
        Plant p_plant = PLANT_NONE
    )
        : declared(p_scenario), planted(p_plant) {
        netw_test::LoopbackRig rig(1);
        rig.mount();
        NetwMultiplayer *server
            = Object::cast_to<NetwMultiplayer>(rig.server());
        NetwMultiplayer *client
            = Object::cast_to<NetwMultiplayer>(rig.client(0));
        REQUIRE(server != nullptr);
        REQUIRE(client != nullptr);
        const int64_t controller = plant_is(PLANT_THE_SERVER_CONTROLS_IT)
            ? int64_t(1)
            : int64_t(rig.peer_id(0));

        stand_up(
            server,
            rig.branch(-1),
            controller,
            read.server_role,
            read.server_channels,
            read.server_authors,
            read.server_is_authority
        );
        stand_up(
            client,
            rig.branch(0),
            controller,
            read.client_role,
            read.client_channels,
            read.client_authors,
            read.client_is_authority
        );
    }

    AuthorshipRun(const AuthorshipRun &) = delete;
    AuthorshipRun &operator=(const AuthorshipRun &) = delete;

    const AuthorshipScenario &scenario() const {
        return declared;
    }

    const Evidence &evidence() const {
        return read;
    }
};

typedef LawRowFor<AuthorshipRun> AuthorshipLaw;

LawVerdict law_tracked(const AuthorshipRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.server_channels <= 0 || read.client_channels <= 0) {
        return law_broken(
            "the server tracks %d channels and the client %d, so a role "
            "difference would be a difference of declaration",
            int(read.server_channels),
            int(read.client_channels)
        );
    }
    return law_held();
}

LawVerdict law_authorship(const AuthorshipRun &p_run) {
    const Evidence &read = p_run.evidence();
    const bool server_owed
        = p_run.scenario().record == NetwPropertySet::RECORD_STATE;
    if (read.server_authors != server_owed) {
        return law_broken(
            "the server reads authorship %d for a %s stream",
            int(read.server_authors),
            server_owed ? "state" : "controller-policed"
        );
    }
    if (read.client_authors == server_owed) {
        return law_broken(
            "the client reads authorship %d beside the server's %d",
            int(read.client_authors),
            int(read.server_authors)
        );
    }
    return law_held();
}

LawVerdict law_apart(const AuthorshipRun &p_run) {
    const Evidence &read = p_run.evidence();
    if (read.server_role == read.client_role) {
        return law_broken(
            "both peers display role %d for one stream",
            int(read.server_role)
        );
    }
    const int64_t authoring
        = read.server_authors ? read.server_role : read.client_role;
    const int64_t receiving
        = read.server_authors ? read.client_role : read.server_role;
    const bool receiver_is_authority = read.server_authors
        ? read.client_is_authority
        : read.server_is_authority;
    if (authoring == NetwMultiplayer::DISPLAY_ROLE_REMOTE) {
        return law_broken(
            "the peer authoring the stream displays it as remote"
        );
    }
    const int64_t owed = receiver_is_authority
        ? NetwMultiplayer::DISPLAY_ROLE_DISABLED
        : NetwMultiplayer::DISPLAY_ROLE_REMOTE;
    if (receiving != owed) {
        return law_broken(
            "the peer receiving the stream displays role %d, owed %d",
            int(receiving),
            int(owed)
        );
    }
    return law_held();
}

const AuthorshipLaw L_TRACKED = {
    "tracked",
    "the displayed value exists on both peers, so a role difference is a "
    "difference of authorship rather than of declaration",
    &law_tracked,
};

const AuthorshipLaw L_AUTHORSHIP = {
    "authorship",
    "the authorship question is asked OF THE SET, so exactly the peer the "
    "set's write policy admits reads as authoring it",
    &law_authorship,
};

const AuthorshipLaw L_APART = {
    "apart",
    "the authoring peer displays its own simulation, and the peer receiving "
    "the stream displays what arrived unless it holds the node's authority, "
    "in which case nothing would write the display and it goes dark",
    &law_apart,
};

const AuthorshipLaw LAWS[] = {L_TRACKED, L_AUTHORSHIP, L_APART};

TEST_CASE("[Networked][Display][SceneTree] the display authorship laws hold") {
    const AuthorshipScenario CORPUS[] = {
        a_state_set_the_server_authors(),
        a_broadcast_set_the_controller_authors(),
    };
    for (const AuthorshipScenario &scenario : CORPUS) {
        const AuthorshipRun run(scenario);
        for (const AuthorshipLaw &law : LAWS) {
            NETW_CELL(law, scenario);
            NETW_LAW_HOLDS(law, run);
        }
    }
}

TEST_CASE(
    "[Networked][Display][SceneTree] a stream no observer may read reds "
    "authorship"
) {
    const AuthorshipScenario scenario = a_state_set_the_server_authors();
    const AuthorshipRun run(scenario, PLANT_THE_STREAM_IS_PRIVATE);
    NETW_CELL(L_AUTHORSHIP, scenario);
    NETW_LAW_BREAKS(L_AUTHORSHIP, run);
}

TEST_CASE(
    "[Networked][Display][SceneTree] a controller-policed stream the "
    "server controls reds authorship"
) {
    const AuthorshipScenario scenario
        = a_broadcast_set_the_controller_authors();
    const AuthorshipRun run(scenario, PLANT_THE_SERVER_CONTROLS_IT);
    NETW_CELL(L_AUTHORSHIP, scenario);
    NETW_LAW_BREAKS(L_AUTHORSHIP, run);
}

constexpr int COORDINATOR = 7;
constexpr int TRANSPORT_SERVER = 1;

Node *mount_under_root(const char *p_name) {
    Node *mount = memnew(Node);
    mount->set_name(StringName(p_name));
    netw::gd::scene_root()->add_child(mount);
    return mount;
}

void drop(Node *p_mount) {
    netw::gd::scene_root()->remove_child(p_mount);
    memdelete(p_mount);
}

RigidBody2D *a_state_body(
    NetwMultiplayer *p_core,
    Node *p_branch,
    double p_x
) {
    RigidBody2D *body = memnew(RigidBody2D);
    body->set_name("StateBody");
    body->set_freeze_enabled(false);
    body->set_freeze_mode(RigidBody2D::FREEZE_MODE_STATIC);
    body->set_position(Vector2(real_t(p_x), 0.0));
    body->set_multiplayer_authority(COORDINATOR);
    const Ref<NetwEntity> entity = NetwEntity::ensure(body);
    p_branch->add_child(body);

    Ref<NetwPropertyConfig> config
        = Netw::configure_property(body, StringName("position"), true);
    Ref<NetwInterpolate> spec;
    spec.instantiate();
    config->interpolate(
        netw::gd::array_of(
            spec->lerp()->smooth(0.0)->to(StringName("position"))
        )
    );
    config->state();
    Dictionary configs;
    configs[StringName("position")] = config;
    const Ref<NetwPropertySet> set
        = property_set_builder::from_property_configs(
            configs,
            NetwPropertySet::RECORD_STATE
        );
    REQUIRE(set.is_valid());
    set->set_sealed(true);
    NETW_CHECK_EQ(
        int(p_core->sync_pipeline()->register_property_set(body, set)),
        int(OK)
    );
    p_core->liveness_bind_route(SUBJECT_ROUTE, entity.ptr());
    p_core->display_mark_dirty(
        entity->get_rid_handle(),
        netw::display::DIRT_ROLE
    );
    p_core->session_flush_deferred();
    return body;
}

int64_t display_role_of(NetwMultiplayer *p_core, Node *p_body) {
    return int64_t(p_core->display_get_track_stat(
        NetwEntity::of(p_body)->get_rid_handle(),
        StringName(),
        StringName("role")
    ));
}

TEST_CASE(
    "[Networked][Display][SceneTree] DA1 the peer holding session authority "
    "writes a state stream and the peer receiving it draws it, so a "
    "coordinator of 7 keeps its own solver running while transport peer 1 "
    "freezes its body kinematically for the display to drive"
) {
    MeshStand stand;
    stand.seat_coordinator(COORDINATOR);
    stand.seat_member(TRANSPORT_SERVER);
    stand.wire(COORDINATOR, TRANSPORT_SERVER);
    stand.pump(4);

    NetwMultiplayer *host = stand.session_of(COORDINATOR);
    NetwMultiplayer *guest = stand.session_of(TRANSPORT_SERVER);
    REQUIRE(host != nullptr);
    REQUIRE(guest != nullptr);
    NETW_CHECK_EQ(int(host->is_host()), 1);
    NETW_CHECK_EQ(int(guest->is_host()), 0);
    NETW_CHECK_EQ(int(guest->is_server()), 1);

    Node *held = mount_under_root("DA1Host");
    Node *seen = mount_under_root("DA1Member");
    stand.mount(COORDINATOR, held);
    stand.mount(TRANSPORT_SERVER, seen);

    RigidBody2D *authored = a_state_body(host, held, 12.0);
    RigidBody2D *received = a_state_body(guest, seen, 12.0);

    NETW_CHECK_EQ(
        int(host->display_default_authors_streams(
            NetwEntity::of(authored)->get_rid_handle()
        )),
        1
    );
    NETW_CHECK_EQ(
        int(guest->display_default_authors_streams(
            NetwEntity::of(received)->get_rid_handle()
        )),
        0
    );

    NETW_CHECK_EQ(
        int(display_role_of(host, authored)),
        int(NetwMultiplayer::DISPLAY_ROLE_AUTHORITY)
    );
    NETW_CHECK_EQ(
        int(display_role_of(guest, received)),
        int(NetwMultiplayer::DISPLAY_ROLE_REMOTE)
    );

    NETW_CHECK_EQ(int(authored->is_freeze_enabled()), 0);
    NETW_CHECK_EQ(int(received->is_freeze_enabled()), 1);
    NETW_CHECK_EQ(
        int(received->get_freeze_mode()),
        int(RigidBody2D::FREEZE_MODE_KINEMATIC)
    );

    const Dictionary standing = NetwEntity::of(authored)
                                    ->get_state_binding()
                                    ->snapshot_payload();
    NETW_CHECK_CLOSE(
        double(Vector2(standing[StringName("position")]).x),
        12.0,
        0.001
    );

    drop(seen);
    drop(held);
}

} // namespace TestNetwDisplayAuthorshipLaws

#endif
