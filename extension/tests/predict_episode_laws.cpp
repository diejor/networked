#include "support/netw_test.h"

#include "netw/api/predict.hpp"
#include "netw/predict/engine.hpp"
#include "netw/predict/episode.hpp"

namespace TestNetwPredictEpisodeLaws {

using namespace godot;
using namespace netw::predict;
using netw::NetwPredictionEngine;

void append_row(
    Journal &r_journal,
    int64_t p_transition,
    Domain p_domain = Domain::IN_DOMAIN,
    Attribution p_attribution = Attribution::PRE_STATE,
    bool p_divergent = false
) {
    JournalOpen row;
    row.label = p_transition;
    row.pre_fp = int32_t(p_transition + 1);
    r_journal.open(p_transition, row);
    r_journal
        .close(p_transition, int32_t(p_transition + 2), FamilyFingerprints());
    r_journal.mark_domain(p_transition, p_domain);
    r_journal.mark_attribution(p_transition, p_attribution);
    if (p_divergent) {
        r_journal.mark_divergent(p_transition);
    }
}

Episode opened_episode() {
    Journal journal;
    append_row(journal, 0);
    Episode episode;
    episode.open(journal, 0, Attribution::PRE_STATE);
    episode.record_comparison(0, 4, false, 2);
    return episode;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] the earliest contiguous row is "
    "pinned"
) {
    Journal journal;
    append_row(journal, 0);
    append_row(journal, 1, Domain::OUT_OF_DOMAIN, Attribution::PRE_STATE, true);
    append_row(journal, 2, Domain::OUT_OF_DOMAIN, Attribution::PRE_STATE, true);
    append_row(journal, 3, Domain::OUT_OF_DOMAIN, Attribution::PRE_STATE, true);
    Episode episode;
    episode.open(journal, 3, Attribution::PRE_STATE);

    CHECK(episode.generator.present);
    NETW_CHECK_EQ(episode.generator.transition, 1);
    NETW_CHECK_EQ(int(episode.generator.domain), int(Domain::OUT_OF_DOMAIN));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] later causes split from taint"
) {
    Journal journal;
    append_row(journal, 0);
    Episode episode;
    episode.open(journal, 0, Attribution::PRE_STATE);
    append_row(journal, 1, Domain::IN_DOMAIN, Attribution::PRE_STATE, true);
    append_row(journal, 2, Domain::IN_DOMAIN, Attribution::CLOSURE, true);
    episode.record_divergence(journal, 1);
    episode.record_divergence(journal, 2);

    NETW_CHECK_EQ(episode.taint.size(), 1);
    NETW_CHECK_EQ(episode.taint[0], 1);
    NETW_CHECK_EQ(episode.secondary_generators.size(), 1);
    NETW_CHECK_EQ(episode.secondary_generators[0].transition, 2);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] evidence is bounded and counted"
) {
    Episode episode = opened_episode();
    const int extra = 20;
    for (int transition = 1; transition < EPISODE_EVIDENCE_LIMIT + extra;
         ++transition) {
        episode.record_comparison(transition, 4, false, 2);
    }

    NETW_CHECK_EQ(int(episode.comparisons.size()), EPISODE_EVIDENCE_LIMIT);
    NETW_CHECK_EQ(episode.evidence_dropped, extra);
    NETW_CHECK_EQ(
        episode.comparisons[episode.comparisons.size() - 1].transition,
        EPISODE_EVIDENCE_LIMIT + extra - 1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] only distinct aligned agreements "
    "close"
) {
    Episode episode = opened_episode();
    for (int transition = 1; transition < 4; ++transition) {
        episode.record_comparison(transition, 0, true, 2);
        episode.record_comparison(transition, 0, true, 2);
    }
    NETW_CHECK_EQ(int(episode.state), int(EpisodeState::OPEN));
    episode.record_comparison(4, 0, true, 2);
    NETW_CHECK_EQ(int(episode.state), int(EpisodeState::CLOSED));
    NETW_CHECK_EQ(episode.closed_transition, 4);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] operator windows charge one "
    "structural unit"
) {
    Episode episode = opened_episode();
    episode.record_write(
        Operator::REBASE_EXACT,
        0,
        0,
        StringName("body"),
        3,
        TriggerShape::MIXED
    );
    episode.record_comparison(1, 4, false, 3);
    episode.record_comparison(2, 4, false, 3);
    episode.record_comparison(3, 4, false, 3);

    NETW_CHECK_EQ(
        int(episode.writes[0].outcome),
        int(OperatorOutcome::FAILED_TO_CONTRACT)
    );
    NETW_CHECK_EQ(episode.non_contraction_used, 1);
    NETW_CHECK_EQ(episode.nc_mixed_trigger, 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] declared exemptions spend no "
    "budget"
) {
    Episode episode = opened_episode();
    CHECK_FALSE(
        episode.count_non_contraction(TriggerShape::ALL_WITHHELD, false)
    );
    CHECK_FALSE(episode.count_non_contraction(TriggerShape::NONE, true));

    NETW_CHECK_EQ(episode.non_contraction_used, 0);
    NETW_CHECK_EQ(episode.withheld_non_contractions, 1);
    NETW_CHECK_EQ(episode.evidence_free_non_contractions, 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] finite budgets enter fallback"
) {
    Episode non_contraction = opened_episode();
    for (int at = 0; at < EPISODE_NON_CONTRACTION_BUDGET; ++at) {
        CHECK(
            non_contraction.count_non_contraction(TriggerShape::MIXED, false)
        );
    }
    CHECK(non_contraction.budget_exhausted());
    non_contraction.enter_fallback(9);
    NETW_CHECK_EQ(int(non_contraction.state), int(EpisodeState::FALLBACK));
    NETW_CHECK_EQ(non_contraction.fallback_transition, 9);

    Episode closure = opened_episode();
    for (int at = 0; at < EPISODE_FULL_CLOSURE_BUDGET; ++at) {
        closure.record_write(
            Operator::FULL_CLOSURE,
            at,
            0,
            StringName("body"),
            1,
            TriggerShape::MIXED
        );
    }
    CHECK(closure.budget_exhausted());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a nearby reopen links identities"
) {
    Journal journal;
    append_row(journal, 0);
    Episode episode;
    episode.open(journal, 0, Attribution::PRE_STATE);
    episode.record_comparison(1, 0, true, 1);
    episode.record_comparison(2, 0, true, 1);
    episode.record_comparison(3, 0, true, 1);
    const int closed = episode.id;

    append_row(journal, 5);
    episode.open(journal, 5, Attribution::PRE_STATE);
    NETW_CHECK_EQ(episode.reopened_from, closed);
    NETW_CHECK_EQ(episode.reopen_chain.size(), 2);
    NETW_CHECK_EQ(episode.reopen_chain[0], closed);
    NETW_CHECK_EQ(episode.reopen_chain[1], episode.id);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a REFUSED operator is retained "
    "with the evidence that refused it"
) {
    Episode episode = opened_episode();
    NETW_CHECK_EQ(int(episode.decisions.size()), 0);

    Dictionary why;
    why[StringName("below_teleport")] = false;
    EpisodeDecision refused;
    refused.op = Operator::TRANSPORT_DELTA;
    refused.basis = 4;
    refused.eligible = false;
    refused.applied = false;
    refused.eligibility = why;
    episode.record_decision(refused);

    // The refusal is the whole point: it never becomes a write, so a report
    // that keeps only writes cannot say an operator was considered at all.
    REQUIRE(int(episode.decisions.size()) == 1);
    CHECK(!episode.decisions[0].eligible);
    NETW_CHECK_EQ(episode.decisions[0].basis, 4);
    CHECK(
        !bool(episode.decisions[0].eligibility[StringName("below_teleport")])
    );

    EpisodeDecision allowed;
    allowed.op = Operator::DISSIPATE;
    allowed.basis = 5;
    allowed.eligible = true;
    allowed.applied = true;
    episode.record_decision(allowed);
    NETW_CHECK_EQ(int(episode.decisions.size()), 2);

    Episode closed_out = opened_episode();
    closed_out.enter_fallback(9);
    closed_out.clear_active();
    closed_out.record_decision(refused);
    NETW_CHECK_EQ(int(closed_out.decisions.size()), 0);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a conditional operator answers "
    "once per episode, and a refusal answers as finally as an application"
) {
    Episode episode = opened_episode();
    CHECK(!episode.operator_decided(Operator::TRANSPORT_DELTA));
    CHECK(!episode.operator_decided(Operator::DISSIPATE));

    EpisodeDecision refused;
    refused.op = Operator::TRANSPORT_DELTA;
    refused.basis = 4;
    episode.record_decision(refused);
    CHECK(episode.operator_decided(Operator::TRANSPORT_DELTA));
    CHECK(!episode.operator_decided(Operator::DISSIPATE));

    EpisodeDecision applied;
    applied.op = Operator::DISSIPATE;
    applied.basis = 5;
    applied.eligible = true;
    applied.applied = true;
    episode.record_decision(applied);
    CHECK(episode.operator_decided(Operator::DISSIPATE));

    // A write is not a verdict: the ladder is unlatched by what it decided,
    // never by what it wrote, or a RESEED would answer for a transport.
    Episode wrote = opened_episode();
    wrote.record_write(
        Operator::TRANSPORT_DELTA,
        0,
        0,
        StringName("body"),
        2,
        TriggerShape::MIXED
    );
    CHECK(!wrote.operator_decided(Operator::TRANSPORT_DELTA));

    Episode reopened = opened_episode();
    reopened.record_decision(refused);
    Journal journal;
    append_row(journal, 7);
    reopened.open(journal, 7, Attribution::PRE_STATE);
    CHECK(!reopened.operator_decided(Operator::TRANSPORT_DELTA));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] an operator is pending on its "
    "NEWEST write and never on one it already answered for"
) {
    Episode episode = opened_episode();
    CHECK(!episode.operator_pending(Operator::TRANSPORT_DELTA));

    const int first = episode.record_write(
        Operator::TRANSPORT_DELTA,
        0,
        0,
        StringName("body"),
        2,
        TriggerShape::MIXED
    );
    CHECK(episode.operator_pending(Operator::TRANSPORT_DELTA));
    // A different operator's write says nothing about this one.
    CHECK(!episode.operator_pending(Operator::DISSIPATE));

    // A retry stacked on top of a write still awaiting its verdict. Once the
    // RETRY is priced the operator is answered, and an older write left
    // pending behind it must not keep the ladder waiting forever.
    const int retry = episode.record_write(
        Operator::TRANSPORT_DELTA,
        1,
        0,
        StringName("body"),
        2,
        TriggerShape::MIXED
    );
    CHECK(episode.operator_pending(Operator::TRANSPORT_DELTA));
    for (uint32_t at = 0; at < episode.writes.size(); ++at) {
        if (episode.writes[at].write_id == retry) {
            episode.writes[at].outcome = OperatorOutcome::CONTRACTED;
        }
    }
    CHECK(!episode.operator_pending(Operator::TRANSPORT_DELTA));
    NETW_CHECK_EQ(
        int(episode.writes[0].outcome),
        int(OperatorOutcome::PENDING)
    );
    (void)first;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] the structural budget is spent by "
    "either ladder and only while the episode is open"
) {
    Episode spent = opened_episode();
    CHECK(!spent.budget_exhausted());
    for (int at = 0; at < EPISODE_NON_CONTRACTION_BUDGET; ++at) {
        spent.count_non_contraction(TriggerShape::MIXED, false);
    }
    CHECK(spent.budget_exhausted());

    Episode closed_out = opened_episode();
    for (int at = 0; at < EPISODE_NON_CONTRACTION_BUDGET; ++at) {
        closed_out.count_non_contraction(TriggerShape::MIXED, false);
    }
    closed_out.enter_fallback(9);
    CHECK(!closed_out.budget_exhausted());

    Episode closures = opened_episode();
    for (int at = 0; at < EPISODE_FULL_CLOSURE_BUDGET; ++at) {
        closures.record_write(
            Operator::FULL_CLOSURE,
            int64_t(at),
            0,
            StringName("body"),
            2,
            TriggerShape::MIXED
        );
    }
    CHECK(closures.budget_exhausted());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a rewire retires the agreement run "
    "it gathered under the replaced numbering"
) {
    Episode episode = opened_episode();
    episode.record_comparison(1, 0, true, 1);
    episode.record_comparison(2, 0, true, 1);
    NETW_CHECK_EQ(episode.agreement_run, 2);

    episode.retire_agreement_run();
    NETW_CHECK_EQ(episode.agreement_run, 0);
    NETW_CHECK_EQ(episode.last_comparison_transition, -1);

    // Retired rather than closed: the run restarts and the same transitions
    // are admissible again, because they are new transitions now.
    NETW_CHECK_EQ(int(episode.state), int(EpisodeState::OPEN));
    episode.record_comparison(1, 0, true, 1);
    NETW_CHECK_EQ(episode.agreement_run, 1);

    Episode latched = opened_episode();
    latched.record_comparison(1, 0, true, 1);
    latched.enter_fallback(2);
    latched.retire_agreement_run();
    NETW_CHECK_EQ(latched.last_comparison_transition, 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a realized breach re-pins the "
    "generator onto the transition that realized it"
) {
    Journal journal;
    append_row(journal, 0, Domain::OUT_OF_DOMAIN, Attribution::PRE_STATE, true);
    append_row(journal, 1, Domain::OUT_OF_DOMAIN, Attribution::PRE_STATE, true);
    append_row(journal, 2, Domain::OUT_OF_DOMAIN, Attribution::CONTACT, true);
    Episode episode;
    episode.open(journal, 1, Attribution::PRE_STATE);
    NETW_CHECK_EQ(episode.generator.transition, 0);

    episode.repin_generator(journal, 2, Attribution::CONTACT);
    NETW_CHECK_EQ(episode.generator.transition, 2);
    NETW_CHECK_EQ(int(episode.attribution), int(Attribution::CONTACT));

    // The contact is the whole cause, so the earliest contiguous divergent row
    // that open() would walk back to is exactly what must NOT be pinned.
    Episode latched;
    latched.open(journal, 1, Attribution::PRE_STATE);
    latched.enter_fallback(1);
    latched.repin_generator(journal, 2, Attribution::CONTACT);
    NETW_CHECK_EQ(latched.generator.transition, 0);
    NETW_CHECK_EQ(int(latched.attribution), int(Attribution::PRE_STATE));
}

int64_t opened_pool(NetwPredictionEngine &r_pool) {
    const int64_t slot = r_pool.open();
    r_pool.configure(
        slot,
        int(netw::Schedule::TICK),
        int(netw::Role::PREDICT),
        int(netw::CorrectionMode::SNAP),
        int(netw::RestoreMode::EXACT)
    );
    return slot;
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] the ladder's own writes are minted "
    "by the same series the pool plans into"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);
    pool->enter_quarantine(slot, 4, false, int(Attribution::CONTACT), false);

    const int first = pool->record_episode_write(
        slot,
        int(Operator::DISSIPATE),
        4,
        0,
        StringName("body"),
        3,
        int(TriggerShape::MIXED),
        false,
        false
    );
    const int second = pool->record_episode_write(
        slot,
        int(Operator::DEMOTE),
        5,
        11,
        StringName("body"),
        3,
        int(TriggerShape::NONE),
        true,
        false
    );
    NETW_CHECK_EQ(second, first + 1);

    PackedInt64Array stats = pool->episode_stats(slot);
    NETW_CHECK_EQ(
        stats[NetwPredictionEngine::STAT_EPISODE_LAST_WRITE_ID],
        second
    );
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_WRITE_COUNT], 2);

    const netw::EpisodeReport report = pool->episode(slot);
    NETW_CHECK_EQ(report.write_count(), 2);
    NETW_CHECK_EQ(report.write_operator(1), int(Operator::DEMOTE));
    NETW_CHECK_EQ(report.write_delta_fp(1), 11);
    CHECK(report.write_evidence_free(1));
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] an unsettled divergence opens the "
    "episode the settled one would have, and never a second time"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);
    CHECK(pool->open_episode(slot, 3, int(Attribution::PRE_STATE)));

    PackedInt64Array stats = pool->episode_stats(slot);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_ACTIVE], 1);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_OPENED], 3);
    const int64_t opened_id = stats[NetwPredictionEngine::STAT_EPISODE_ID];

    // Re-opening would discard the evidence the open episode has gathered,
    // which is the whole reason the shell asked before it opened.
    CHECK(!pool->open_episode(slot, 4, int(Attribution::CONTACT)));
    stats = pool->episode_stats(slot);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_ID], opened_id);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_OPENED], 3);

    pool->record_episode_escalation(slot, int(TriggerShape::MIXED));
    NETW_CHECK_EQ(
        pool->episode_stats(
            slot
        )[NetwPredictionEngine::STAT_EPISODE_NON_CONTRACTION],
        1
    );

    pool->enter_quarantine(slot, 5, false, int(Attribution::CONTACT), false);
    CHECK(pool->open_episode(slot, 6, int(Attribution::CONTACT)));
    NETW_CHECK_EQ(
        pool->episode_stats(slot)[NetwPredictionEngine::STAT_EPISODE_ID],
        opened_id + 1
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a demotion latches an episode that "
    "names the breach as its cause and itself as demoted"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);
    pool->record_breach(slot, 6);

    PackedInt64Array stats = pool->episode_stats(slot);
    NETW_CHECK_EQ(stats[NetwPredictionEngine::STAT_EPISODE_ACTIVE], 1);
    NETW_CHECK_EQ(
        stats[NetwPredictionEngine::STAT_EPISODE_ATTRIBUTION],
        int(Attribution::CONTACT)
    );

    pool->enter_quarantine(slot, 6, false, int(Attribution::CONTACT), true);
    const netw::EpisodeReport report = pool->episode(slot);
    NETW_CHECK_EQ(report.breach_transition(), 6);
    NETW_CHECK_EQ(report.fallback_transition(), 6);
    CHECK(report.demoted());

    NetwPredictionEngine held_undemoted;
    NetwPredictionEngine *const undemoted = &held_undemoted;
    const int64_t undemoted_slot = opened_pool(held_undemoted);
    undemoted->enter_quarantine(
        undemoted_slot,
        6,
        false,
        int(Attribution::CONTACT),
        false
    );
    CHECK(!undemoted->episode(undemoted_slot).demoted());
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a slot with no episode reports the "
    "absence rather than a zeroth episode"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);

    PackedInt64Array live = pool->episode_stats(slot);
    PackedInt64Array missing = pool->episode_stats(slot + 1000);
    NETW_CHECK_EQ(
        int(live.size()),
        int(NetwPredictionEngine::STAT_EPISODE_COUNT)
    );
    NETW_CHECK_EQ(int(missing.size()), int(live.size()));
    for (int at = 0; at < int(live.size()); ++at) {
        NETW_CHECK_EQ(missing[at], live[at]);
    }
    NETW_CHECK_EQ(live[NetwPredictionEngine::STAT_EPISODE_ACTIVE], 0);
    NETW_CHECK_EQ(live[NetwPredictionEngine::STAT_EPISODE_STATE], -1);
    NETW_CHECK_EQ(live[NetwPredictionEngine::STAT_EPISODE_OPENED], -1);
    NETW_CHECK_EQ(live[NetwPredictionEngine::STAT_EPISODE_LAST_COMPARISON], -1);
    NETW_CHECK_EQ(
        pool->record_episode_write(
            slot + 1000,
            int(Operator::DISSIPATE),
            4,
            0,
            StringName("body"),
            3,
            int(TriggerShape::MIXED),
            false,
            false
        ),
        0
    );
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] the revision stamp follows an "
    "episode there is, so a slot holding none stamps nothing and a reader "
    "keyed on the stamp is never told a change it cannot read"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);

    const int64_t quiet = pool->episode_revision(slot);
    pool->sync_episode(slot);
    NETW_CHECK_EQ(pool->episode_revision(slot), quiet);

    REQUIRE(pool->open_episode(slot, 3, int(Attribution::PRE_STATE)));
    const int64_t opened = pool->episode_revision(slot);
    pool->sync_episode(slot);
    NETW_CHECK_EQ(pool->episode_revision(slot), opened + 1);
}

TEST_CASE(
    "[Networked][Predict][Hosted][Episode] a close stamps whether or not the "
    "slot still holds an episode, because the retirement IS the change a "
    "reader keyed on the stamp is waiting for"
) {
    NetwPredictionEngine held_pool;
    NetwPredictionEngine *const pool = &held_pool;
    const int64_t slot = opened_pool(held_pool);

    const int64_t quiet = pool->episode_revision(slot);
    pool->close_episode(slot);
    NETW_CHECK_EQ(pool->episode_revision(slot), quiet + 1);
}

} // namespace TestNetwPredictEpisodeLaws
