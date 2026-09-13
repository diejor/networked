#include "support/netw_test.h"

#include "netw/wire/attribution.hpp"

namespace TestWireRefusalLaws {

using namespace godot;
using netw::wire::AttributionBook;
using netw::wire::Refusal;

int64_t verdict_count(
    const Dictionary &p_snapshot,
    int64_t p_peer,
    int64_t p_channel,
    const char *p_verdict
) {
    const Dictionary refusals = p_snapshot[StringName("refusals")];
    if (!refusals.has(p_peer)) {
        return 0;
    }
    const Dictionary channels = refusals[p_peer];
    if (!channels.has(p_channel)) {
        return 0;
    }
    const Dictionary verdicts = channels[p_channel];
    return verdicts.has(String(p_verdict))
        ? int64_t(verdicts[String(p_verdict)])
        : 0;
}

TEST_CASE(
    "[Networked][Wire][Hosted] F1 a refused frame is counted against the peer "
    "and the channel that sent it, and the snapshot names the verdict in "
    "words, because a count with no reason answers nothing"
) {
    AttributionBook book;
    book.note_refusal(7, 39, Refusal::UNBOUND);
    book.note_refusal(7, 39, Refusal::UNBOUND);
    book.note_refusal(7, 39, Refusal::STALE);
    book.note_refusal(9, 39, Refusal::UNBOUND);

    NETW_CHECK_EQ(book.get_refused_in(), int64_t(4));
    NETW_CHECK_EQ(
        book.peer_channel_refused(7, 39, Refusal::UNBOUND),
        int64_t(2)
    );
    NETW_CHECK_EQ(book.peer_channel_refused(7, 39, Refusal::STALE), int64_t(1));
    NETW_CHECK_EQ(
        book.peer_channel_refused(9, 39, Refusal::UNBOUND),
        int64_t(1)
    );
    NETW_CHECK_EQ(
        book.peer_channel_refused(7, 40, Refusal::UNBOUND),
        int64_t(0)
    );

    const Dictionary snapshot = book.snapshot();
    NETW_CHECK_EQ(verdict_count(snapshot, 7, 39, "unbound"), int64_t(2));
    NETW_CHECK_EQ(verdict_count(snapshot, 7, 39, "stale"), int64_t(1));
    NETW_CHECK_EQ(verdict_count(snapshot, 9, 39, "unbound"), int64_t(1));
}

TEST_CASE(
    "[Networked][Wire][Hosted] F2 every verdict the receive plane can reach "
    "has its own name, so no two reasons fold into one row"
) {
    AttributionBook book;
    const Refusal every[] = {
        Refusal::GATE,
        Refusal::STALE,
        Refusal::UNROUTED,
        Refusal::UNBOUND,
        Refusal::MALFORMED,
        Refusal::BASELINE_UNKNOWN,
    };
    for (const Refusal verdict : every) {
        book.note_refusal(1, 39, verdict);
    }

    const Dictionary snapshot = book.snapshot();
    const Dictionary refusals = snapshot[StringName("refusals")];
    const Dictionary channels = refusals[int64_t(1)];
    const Dictionary verdicts = channels[int64_t(39)];
    NETW_CHECK_EQ(verdicts.size(), 6);
    NETW_CHECK_EQ(book.get_refused_in(), int64_t(6));
    NETW_CHECK_EQ(
        verdict_count(snapshot, 1, 39, "baseline_unknown"),
        int64_t(1)
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] F3 a frame that was not refused moves no "
    "counter, because NONE is the absence of a verdict rather than one of them"
) {
    AttributionBook book;
    book.note_refusal(1, 39, Refusal::NONE);

    NETW_CHECK_EQ(book.get_refused_in(), int64_t(0));
    const Dictionary snapshot = book.snapshot();
    const Dictionary refusals = snapshot[StringName("refusals")];
    NETW_CHECK_EQ(refusals.size(), 0);
    NETW_CHECK_EQ(verdict_count(snapshot, 1, 39, "none"), int64_t(0));
}

TEST_CASE(
    "[Networked][Wire][Hosted] F4 a refusal count clears with the book, so a "
    "snapshot describes the window it was taken over"
) {
    AttributionBook book;
    book.note_refusal(1, 39, Refusal::MALFORMED);
    book.clear();

    NETW_CHECK_EQ(book.get_refused_in(), int64_t(0));
    NETW_CHECK_EQ(
        book.peer_channel_refused(1, 39, Refusal::MALFORMED),
        int64_t(0)
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] F5 an inbound frame is counted per peer and "
    "channel beside the bytes it spent, so a channel heard and never applied "
    "is a ratio rather than a silence"
) {
    AttributionBook book;
    netw::wire::Attribution frame;
    frame.peer = 1;
    frame.channel = 39;
    frame.bits = 80;
    book.observe_in(frame);
    book.observe_in(frame);
    book.note_refusal(1, 39, Refusal::UNBOUND);

    const Dictionary snapshot = book.snapshot();
    const Dictionary frames = snapshot[StringName("frames_in_by_channel")];
    const Dictionary peers = frames[int64_t(1)];
    NETW_CHECK_EQ(int64_t(peers[int64_t(39)]), int64_t(2));
    NETW_CHECK_EQ(book.peer_channel_in(1, 39), int64_t(20));
    NETW_CHECK_EQ(verdict_count(snapshot, 1, 39, "unbound"), int64_t(1));
}

} // namespace TestWireRefusalLaws
