#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include "support/loopback_rig.h"
#include "support/netw_cells.h"

#include "godot/node.hpp"
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

} // namespace TestNetwDisplayAuthorshipLaws

#endif
