#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <memory>

#include "godot/node.hpp"
#include "godot/scene_tree.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/api/nodes/multiplayer_viewport.hpp"

using namespace godot;

namespace NetwTests {

namespace {

using netw::MultiplayerViewport;
using netw::NetwMultiplayer;
using netw_test::CallLog;

constexpr int POLL_FRAMES = 30;

struct TreePollEvidence {
    bool driven = false;
    int polls = 0;
};

TreePollEvidence &tree_poll_evidence() {
    static TreePollEvidence evidence;
    return evidence;
}

class TreePollScenario final : public netw_test::FrameScenario {
    int step = 0;
    MultiplayerViewport *tree = nullptr;
    std::unique_ptr<CallLog> polls;

    void open() {
        polls = std::make_unique<CallLog>();
        tree = memnew(MultiplayerViewport);
        tree->set_name("MT11Tree");
        netw::gd::scene_root()->add_child(tree);
        tree->get_api()->observe(
            NetwMultiplayer::SESSION_POLL_STARTED,
            polls->callable("poll")
        );
    }

    void close() {
        TreePollEvidence &evidence = tree_poll_evidence();
        evidence.polls = polls->count("poll");
        evidence.driven = true;
        netw::gd::scene_root()->remove_child(tree);
        memdelete(tree);
        tree = nullptr;
        polls.reset();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (step == 0) {
            open();
        }
        if (step < POLL_FRAMES) {
            ++step;
            return true;
        }
        close();
        return false;
    }
};

NETW_FRAME_SCENARIO(TreePollScenario, tree_poll_scenario);

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] MT11 a tree's session is polled once per "
    "frame by the engine, so nothing in the tree polls it a second time"
) {
    const TreePollEvidence &evidence = tree_poll_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_GE(evidence.polls, POLL_FRAMES - 2);
    NETW_CHECK_LE(evidence.polls, POLL_FRAMES + 2);
}

} // namespace NetwTests
