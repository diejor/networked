#include "support/frame_drive.h"
#include "support/netw_call_log.h"
#include "support/netw_test.h"

#include <memory>

#include "godot/scene_tree.hpp"
#include "netw/api/group_promise.hpp"
#include "netw/api/promise.hpp"

using namespace godot;

namespace NetwTests {

namespace {

using netw::NetwGroupPromise;
using netw::NetwPromise;
using netw_test::CallLog;

struct DeferredEvidence {
    bool driven = false;
    int resolved_before = 0;
    int resolved_after = 0;
    Variant resolved_answer;
    int rejected_before = 0;
    int rejected_after = 0;
    Variant rejected_answer;
    int batch_before = 0;
    int batch_after = 0;
    int pending_before = 0;
    int pending_after = 0;
};

DeferredEvidence &deferred_evidence() {
    static DeferredEvidence evidence;
    return evidence;
}

Ref<NetwGroupPromise> settled_batch() {
    PackedInt32Array peers;
    peers.push_back(1);
    const Ref<NetwGroupPromise> batch = NetwGroupPromise::create(peers);
    batch->resolve_peer(1, Variant(7));
    return batch;
}

class DeferredWaitScenario final : public netw_test::FrameScenario {
    int step = 0;
    std::unique_ptr<CallLog> answers;
    Ref<NetwPromise> resolved;
    Ref<NetwPromise> rejected;
    Ref<NetwPromise> pending;
    Ref<NetwGroupPromise> batch;

    void open() {
        answers = std::make_unique<CallLog>();

        resolved = NetwPromise::resolved(Variant(7));
        rejected = NetwPromise::rejected(ERR_TIMEOUT, String("gone"));
        batch = settled_batch();
        pending.instantiate();

        resolved->wait().connect(answers->callable("resolved"));
        rejected->wait().connect(answers->callable("rejected"));
        batch->wait().connect(answers->callable("batch"));
        pending->wait().connect(answers->callable("pending"));

        DeferredEvidence &evidence = deferred_evidence();
        evidence.resolved_before = answers->count("resolved");
        evidence.rejected_before = answers->count("rejected");
        evidence.batch_before = answers->count("batch");
        evidence.pending_before = answers->count("pending");
    }

    void close() {
        DeferredEvidence &evidence = deferred_evidence();
        evidence.resolved_after = answers->count("resolved");
        evidence.rejected_after = answers->count("rejected");
        evidence.batch_after = answers->count("batch");
        evidence.pending_after = answers->count("pending");
        if (evidence.resolved_after > 0) {
            evidence.resolved_answer = answers->args("resolved")[0];
        }
        if (evidence.rejected_after > 0) {
            evidence.rejected_answer = answers->args("rejected")[0];
        }
        evidence.driven = true;

        resolved.unref();
        rejected.unref();
        pending.unref();
        batch.unref();
        answers.reset();
    }

public:
    bool advance() override {
        if (netw::gd::scene_root() == nullptr) {
            return false;
        }
        if (step == 0) {
            open();
            ++step;
            return true;
        }
        close();
        return false;
    }
};

NETW_FRAME_SCENARIO(DeferredWaitScenario, deferred_wait_scenario);

} // namespace

TEST_CASE(
    "[Networked][Session][Frame] W8 a promise that settled before anyone "
    "waited still answers its waiter, one frame later, because the wait "
    "defers the emission it would otherwise have missed"
) {
    const DeferredEvidence &evidence = deferred_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_EQ(evidence.resolved_before, 0);
    NETW_CHECK_EQ(evidence.resolved_after, 1);
    CHECK(evidence.resolved_answer == Variant(7));
}

TEST_CASE(
    "[Networked][Session][Frame] W9 a promise that failed before anyone "
    "waited answers its code on the same deferred edge, so the failing path "
    "reaches a late waiter rather than hanging it"
) {
    const DeferredEvidence &evidence = deferred_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_EQ(evidence.rejected_before, 0);
    NETW_CHECK_EQ(evidence.rejected_after, 1);
    CHECK(evidence.rejected_answer == Variant(int(ERR_TIMEOUT)));
}

TEST_CASE(
    "[Networked][Session][Frame] W10 a batch that settled before anyone "
    "waited answers on the same deferred edge a single promise does, so the "
    "two wait the same way"
) {
    const DeferredEvidence &evidence = deferred_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_EQ(evidence.batch_before, 0);
    NETW_CHECK_EQ(evidence.batch_after, 1);
}

TEST_CASE(
    "[Networked][Session][Frame] W11 a promise still pending answers nobody "
    "when the frame turns, so the deferred emission belongs to the settle "
    "rather than to the wait"
) {
    const DeferredEvidence &evidence = deferred_evidence();
    REQUIRE(evidence.driven);

    NETW_CHECK_EQ(evidence.pending_before, 0);
    NETW_CHECK_EQ(evidence.pending_after, 0);
}

} // namespace NetwTests
