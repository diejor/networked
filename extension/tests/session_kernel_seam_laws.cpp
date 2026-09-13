#include "support/netw_test.h"

#include "godot/node.hpp"
#include "godot/script.hpp"
#include "netw/api/netw_multiplayer.hpp"
#include "netw/prediction_core.hpp"
#include "support/declared_seams.h"
#include "support/minted_script.h"
#include "support/netw_call_log.h"

namespace TestNetwSessionKernelSeam {

using namespace godot;
using netw::NetwMultiplayer;
using netw_test::CallLog;

Ref<NetwMultiplayer> make_session() {
    Ref<NetwMultiplayer> session;
    session.instantiate();
    return session;
}

bool differ(int64_t p_a, int64_t p_b) {
    return p_a != p_b;
}

int refused() {
    return int(ERR_INVALID_DATA);
}

Variant armed_node(Node *p_node) {
    return Variant(p_node);
}

TEST_CASE(
    "[Networked][Session][Hosted] L1 an unoverridden kernel seam answers "
    "exactly what the stock prediction core answers, so the twin is the law"
) {
    Ref<NetwMultiplayer> session = make_session();

    SUBCASE("the drive seam folds the way the core folds") {
        const Ref<netw::NetwPredictFold> seamed
            = session->predict_drive(9, 4, 7);
        const Ref<netw::NetwPredictFold> stock
            = netw::prediction_core::predict_fold(9, 4, 7);
        CHECK(seamed.is_valid());
        CHECK(stock.is_valid());
        NETW_CHECK_EQ(seamed->label(), stock->label());
        NETW_CHECK_EQ(seamed->kind(), stock->kind());
        NETW_CHECK_EQ(seamed->fresh() ? 1 : 0, stock->fresh() ? 1 : 0);
    }

    SUBCASE("the consume seam answers the core's action") {
        NETW_CHECK_EQ(
            session->predict_consume(5, 2),
            netw::prediction_core::consume_action(5, 2)
        );
        NETW_CHECK_EQ(
            session->predict_consume(0, 0),
            netw::prediction_core::consume_action(0, 0)
        );
    }

    SUBCASE("the evaluate seam judges the way the core judges") {
        const Ref<netw::NetwPredictJudgement> seamed
            = session->predict_evaluate(
                netw::NetwPredictJournal::IN_DOMAIN,
                netw::NetwPredict::EXACT_VERDICT_UNJUDGED,
                Dictionary(),
                Dictionary(),
                Dictionary(),
                Dictionary()
            );
        const Ref<netw::NetwPredictJudgement> stock
            = netw::prediction_core::evaluate(
                netw::NetwPredictJournal::IN_DOMAIN,
                netw::NetwPredict::EXACT_VERDICT_UNJUDGED,
                Dictionary(),
                Dictionary(),
                Dictionary(),
                Dictionary()
            );
        REQUIRE(seamed.is_valid());
        REQUIRE(stock.is_valid());
        const bool divergence_agrees
            = seamed->divergence() == stock->divergence();
        CHECK(divergence_agrees);
        NETW_CHECK_EQ(seamed->corrected() ? 1 : 0, stock->corrected() ? 1 : 0);
    }

    SUBCASE("the recover seam answers a recovery rather than nothing") {
        const Ref<netw::NetwPredictRecovery> seamed = session->predict_recover(
            Dictionary(),
            netw::NetwPredict::RECOVERY_POLICY_REBASE_REPLAY,
            netw::NetwPredict::CORRECTION_MODE_AUTO,
            netw::NetwPredict::RESTORE_MODE_EXACT,
            Dictionary(),
            Dictionary(),
            Dictionary(),
            Dictionary(),
            Dictionary(),
            0.0
        );
        const Ref<netw::NetwPredictRecovery> stock
            = netw::prediction_core::recover(
                Dictionary(),
                netw::NetwPredict::RECOVERY_POLICY_REBASE_REPLAY,
                netw::NetwPredict::CORRECTION_MODE_AUTO,
                netw::NetwPredict::RESTORE_MODE_EXACT,
                Dictionary(),
                Dictionary(),
                Dictionary(),
                Dictionary(),
                Dictionary(),
                0.0
            );
        CHECK(seamed.is_valid());
        CHECK(stock.is_valid());
        NETW_CHECK_EQ(seamed->teleport() ? 1 : 0, stock->teleport() ? 1 : 0);
        NETW_CHECK_EQ(seamed->skip() ? 1 : 0, stock->skip() ? 1 : 0);
        NETW_CHECK_EQ(seamed->restore().size(), stock->restore().size());
        NETW_CHECK_EQ(seamed->write().size(), stock->write().size());
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L2 a consume depth inside the buffer is a "
    "different action from one past it, so the seam is worth overriding"
) {
    Ref<NetwMultiplayer> session = make_session();

    CHECK(
        differ(session->predict_consume(0, 8), session->predict_consume(64, 8))
    );
}

TEST_CASE(
    "[Networked][Session][Hosted] L3 the encode stage is the only thing that "
    "arms the encode door, and it disarms before it answers its caller"
) {
    Ref<NetwMultiplayer> session = make_session();
    PackedByteArray stock;
    stock.push_back(1);
    stock.push_back(2);
    stock.push_back(3);

    SUBCASE("the door answers nothing outside a stage") {
        NETW_CHECK_EQ(session->sync_encode(2, 4).size(), 0);
    }

    SUBCASE("the stage answers the stock it armed") {
        const PackedByteArray bytes
            = session->run_sync_encode_stage(2, stock, 4);
        NETW_CHECK_EQ(bytes.size(), 3);
        NETW_CHECK_EQ(int(bytes[0]), 1);
        NETW_CHECK_EQ(int(bytes[2]), 3);
    }

    SUBCASE("the stage leaves the door disarmed behind it") {
        session->run_sync_encode_stage(2, stock, 4);
        NETW_CHECK_EQ(session->sync_encode(2, 4).size(), 0);
    }

    SUBCASE("clearing the session disarms a door the stage armed") {
        session->run_sync_encode_stage(2, stock, 4);
        session->clear_flat_family_state();
        NETW_CHECK_EQ(session->sync_encode(2, 4).size(), 0);
    }
}

TEST_CASE(
    "[Networked][Session][Hosted] L4 the decode door refuses outside a stage "
    "and answers the stage's own decoder inside one"
) {
    Ref<NetwMultiplayer> session = make_session();

    SUBCASE("no decoder installed is ERR_UNCONFIGURED") {
        NETW_CHECK_EQ(
            int(session->sync_decode(RID(), 0, 0, -1, PackedByteArray())),
            int(ERR_UNCONFIGURED)
        );
    }

    SUBCASE("the stage routes the verdict its decoder answered") {
        const Error verdict = session->run_sync_decode_stage(
            RID(),
            0,
            0,
            -1,
            PackedByteArray(),
            callable_mp_static(&refused)
        );
        NETW_CHECK_EQ(int(verdict), int(ERR_INVALID_DATA));
    }

    SUBCASE("the stage leaves no decoder behind it") {
        session->run_sync_decode_stage(
            RID(),
            0,
            0,
            -1,
            PackedByteArray(),
            callable_mp_static(&refused)
        );
        NETW_CHECK_EQ(
            int(session->sync_decode(RID(), 0, 0, -1, PackedByteArray())),
            int(ERR_UNCONFIGURED)
        );
    }
}

#if defined(NETW_TIER_HOSTED)

TEST_CASE(
    "[Networked][Session] L5 a script whose base is the native class "
    "redeclares every seam it names and no other, so an override attached to "
    "the session itself is still the one consulted"
) {
    Ref<NetwMultiplayer> session = make_session();
    const StringName base(NetwMultiplayer::get_class_static());
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    const Ref<Script> inheriting
        = netw_test::script_from("res://tests/support/chains/seam_leaf.gd");
    REQUIRE(replacing.is_valid());
    REQUIRE(inheriting.is_valid());

    SUBCASE("the seam it declares is the one it replaced") {
        CHECK(
            session->script_overrides_seam(replacing, base, "_predict_consume")
        );
    }

    SUBCASE("a seam it never named is not replaced") {
        CHECK_FALSE(
            session->script_overrides_seam(replacing, base, "_predict_drive")
        );
    }

    SUBCASE("a leaf that declares nothing still carries its base's seam") {
        CHECK(
            session->script_overrides_seam(inheriting, base, "_predict_consume")
        );
    }

    SUBCASE("a session carrying no script replaces nothing") {
        CHECK_FALSE(session->script_overrides_seam(
            Ref<Script>(),
            base,
            "_predict_consume"
        ));
    }
}

TEST_CASE(
    "[Networked][Session] L6 a native call to a replaced seam reaches "
    "the script attached to the session, so a game's override is what answers"
) {
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    REQUIRE(replacing.is_valid());
    Ref<NetwMultiplayer> session = replacing->call("new");
    REQUIRE(session.is_valid());

    SUBCASE("the override runs and answers what the stock body answers") {
        NETW_CHECK_EQ(int(session->get("consumes")), 0);
        NETW_CHECK_EQ(
            session->predict_consume(5, 2),
            netw::prediction_core::consume_action(5, 2)
        );
        NETW_CHECK_EQ(int(session->get("consumes")), 1);
    }

    SUBCASE("a seam the script never named still answers the stock body") {
        const Ref<netw::NetwPredictFold> folded
            = session->predict_drive(9, 4, 7);
        REQUIRE(folded.is_valid());
        NETW_CHECK_EQ(
            folded->label(),
            netw::prediction_core::predict_fold(9, 4, 7)->label()
        );
        NETW_CHECK_EQ(int(session->get("consumes")), 0);
    }
}

TEST_CASE(
    "[Networked][Session] L8 a session answers for its own script, so "
    "asking it which seams it replaces needs no base name from the caller"
) {
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    REQUIRE(replacing.is_valid());
    Ref<NetwMultiplayer> session = replacing->call("new");
    REQUIRE(session.is_valid());

    SUBCASE("the seam its own script declares is the one it replaced") {
        CHECK(session->overrides_seam("_predict_consume"));
    }

    SUBCASE("a seam its script never named is not replaced") {
        CHECK_FALSE(session->overrides_seam("_predict_drive"));
    }

    SUBCASE("a stock session carrying no script replaces nothing") {
        Ref<NetwMultiplayer> stock = make_session();
        CHECK_FALSE(stock->overrides_seam("_predict_consume"));
    }

    SUBCASE(
        "a leaf that declares nothing answers for the seam its base "
        "replaced"
    ) {
        const Ref<Script> inheriting
            = netw_test::script_from("res://tests/support/chains/seam_leaf.gd");
        REQUIRE(inheriting.is_valid());
        Ref<NetwMultiplayer> leaf = inheriting->call("new");
        REQUIRE(leaf.is_valid());
        CHECK(leaf->overrides_seam("_predict_consume"));
        CHECK_FALSE(leaf->overrides_seam("_predict_drive"));
    }
}

TEST_CASE(
    "[Networked][Session] L9 an extension script is one whose chain "
    "reaches the native session, and nothing else is"
) {
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    const Ref<Script> inheriting
        = netw_test::script_from("res://tests/support/chains/seam_leaf.gd");
    const Ref<Script> unrelated
        = netw_test::script_from(netw_test::gdsrc::NATIVE_CLASS_HANDLE);
    REQUIRE(replacing.is_valid());
    REQUIRE(inheriting.is_valid());
    REQUIRE(unrelated.is_valid());

    CHECK(NetwMultiplayer::is_extension_script(replacing));
    CHECK(NetwMultiplayer::is_extension_script(inheriting));
    CHECK_FALSE(NetwMultiplayer::is_extension_script(unrelated));
    CHECK_FALSE(NetwMultiplayer::is_extension_script(Ref<Script>()));
}

TEST_CASE(
    "[Networked][Session] L10 display and property calls consult the "
    "virtuals on a native session before their stock bodies"
) {
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    REQUIRE(replacing.is_valid());
    Ref<NetwMultiplayer> session = replacing->call("new");
    REQUIRE(session.is_valid());

    NETW_CHECK_EQ(
        int(session->display_lane(RID(), StringName("x"), Variant())),
        int(ERR_SKIP)
    );
    const Array gathered = session->run_gather_set(RID(), 7, Callable());
    NETW_CHECK_EQ(gathered.size(), 1);
    REQUIRE(bool(gathered.size() == 1));
    NETW_CHECK_EQ(int(gathered[0]), 7);
    NETW_CHECK_EQ(
        int(session->run_apply_set(RID(), 7, Array(), Callable())),
        int(ERR_SKIP)
    );
    NETW_CHECK_EQ(int(session->get("display_writes")), 1);
    NETW_CHECK_EQ(int(session->get("gathers")), 1);
    NETW_CHECK_EQ(int(session->get("applies")), 1);
}

TEST_CASE(
    "[Networked][Session] L12 the sync bookkeeping and the spawn "
    "stages consult the virtuals on a native session before their stock "
    "bodies, so a game that publishes its own session keeps every override "
    "the shell used to carry"
) {
    const Ref<Script> replacing
        = netw_test::script_from("res://tests/support/chains/seam_base.gd");
    REQUIRE(replacing.is_valid());
    Ref<NetwMultiplayer> session = replacing->call("new");
    REQUIRE(session.is_valid());

    session->note_ack(2, 9);
    session->note_sent(2, 9);
    NETW_CHECK_EQ(int(session->spawn_declare(RID(), Variant())), int(ERR_SKIP));
    session->spawn_undeclare(RID());
    CHECK(session->spawn_construct(RID()) == nullptr);

    NETW_CHECK_EQ(int(session->get("acks")), 1);
    NETW_CHECK_EQ(int(session->get("sends")), 1);
    NETW_CHECK_EQ(int(session->get("declares")), 1);
    NETW_CHECK_EQ(int(session->get("undeclares")), 1);
    NETW_CHECK_EQ(int(session->get("constructs")), 1);
}

#endif

TEST_CASE(
    "[Networked][Session][Hosted] L13 a session naming none of those stages "
    "answers its stock bodies, and the spawn constructor the pipeline armed "
    "is what the stock construction returns"
) {
    Ref<NetwMultiplayer> session = make_session();
    Node *made = memnew(Node);
    session->spawn_construct_arm(callable_mp_static(&armed_node).bind(made));

    NETW_CHECK_EQ(
        int(session->spawn_declare(RID(), Variant())),
        int(ERR_DOES_NOT_EXIST)
    );
    CHECK(session->spawn_construct(RID()) == made);

    session->spawn_construct_arm(Callable());
    CHECK(session->spawn_construct(RID()) == nullptr);
    memdelete(made);
}

TEST_CASE(
    "[Networked][Session][Hosted] L13 with no override and no installed seam "
    "the stock bodies are the staged callables themselves, so a caller that "
    "hands its own gatherer and applier round-trips the values through them"
) {
    Ref<NetwMultiplayer> session = make_session();
    CallLog log;

    Array offered;
    offered.push_back(Vector2(3.0, 4.0));

    const Array gathered
        = session->run_gather_set(RID(), 0, log.answering("gather", offered));
    NETW_CHECK_EQ(log.count("gather"), 1);
    NETW_CHECK_EQ(gathered.size(), 1);
    REQUIRE(bool(gathered.size() == 1));
    CHECK(Vector2(gathered[0]) == Vector2(3.0, 4.0));

    NETW_CHECK_EQ(
        int(session->run_apply_set(RID(), 0, gathered, log.callable("apply"))),
        int(OK)
    );
    NETW_CHECK_EQ(log.count("apply"), 1);
    const Array handed = log.args("apply");
    REQUIRE(bool(handed.size() == 1));
    CHECK(Array(handed[0]) == gathered);

    SUBCASE(
        "a caller that stages nothing gathers nothing and applies "
        "nowhere, rather than reaching for a gatherer it never got"
    ) {
        NETW_CHECK_EQ(session->run_gather_set(RID(), 0, Callable()).size(), 0);
        NETW_CHECK_EQ(
            int(session->run_apply_set(RID(), 0, Array(), Callable())),
            int(ERR_UNCONFIGURED)
        );
    }
}

} // namespace TestNetwSessionKernelSeam
