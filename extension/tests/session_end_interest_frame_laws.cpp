#include "support/frame_drive.h"
#include "support/netw_test.h"

#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"

using namespace godot;

namespace NetwTests {

namespace {

using netw::NetwMultiplayer;

struct EndedEvidence {
    bool driven = false;
    int layers_opened = 0;
    int layers_after_end = 0;
};

EndedEvidence &ended_evidence() {
    static EndedEvidence evidence;
    return evidence;
}

class EndedInterestScenario final : public netw_test::FrameScenario {
    int step = 0;
    Ref<NetwMultiplayer> session;

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        EndedEvidence &evidence = ended_evidence();
        if (step == 0) {
            session.instantiate();
            session->interest_layer_create(StringName("sight"));
            session->interest_layer_create(StringName("sound"));
            evidence.layers_opened = int(session->interest_layers().size());
            session->session_announce_ended();
            ++step;
            return true;
        }
        evidence.layers_after_end = int(session->interest_layers().size());
        evidence.driven = true;
        session->embed_dispose();
        session.unref();
        return false;
    }
};

NETW_FRAME_SCENARIO(EndedInterestScenario, ended_interest_scenario);

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] L7 a session that ends forgets every layer "
    "it opened by the next frame, so a re-host on the same session starts "
    "from an empty interest plane"
) {
    const EndedEvidence &evidence = ended_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_EQ(evidence.layers_opened, 2);
    NETW_CHECK_EQ(evidence.layers_after_end, 0);
}

} // namespace NetwTests
