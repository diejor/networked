#include "support/netw_test.h"

#include "godot/variant.hpp"
#include "netw/predict/compare.hpp"
#include "netw/predict/transition_book.hpp"
#include "netw/predict/wiring.hpp"

using namespace godot;

namespace TestNetwPredictContainerState {

using netw::predict::FieldDecl;
using netw::predict::StateRow;
using netw::predict::TransitionBook;
using netw::predict::Wiring;

Wiring wiring_of(Variant::Type p_type) {
    LocalVector<FieldDecl> declared;
    FieldDecl held;
    held.key = StringName("held");
    held.type = int(p_type);
    declared.push_back(held);
    return netw::predict::compile(declared);
}

TEST_CASE(
    "[Networked][Predict][Hosted] PC1 an Array the game mutates in place "
    "after a tick was recorded leaves that tick's row as it was"
) {
    const Wiring wiring = wiring_of(Variant::ARRAY);
    TransitionBook book;
    book.reset(wiring.codec);
    Array live;
    live.push_back(1);
    StateRow written;
    written.resize(1);
    written.set(0, live);
    REQUIRE(book.record_row(wiring.codec, 1, 1, true, written));

    live.push_back(2);
    StateRow read;
    REQUIRE(book.read_row(wiring.codec, 1, read));
    NETW_CHECK_EQ(Array(read.values[0]).size(), int64_t(1));

    Array(read.values[0]).push_back(3);
    StateRow again;
    REQUIRE(book.read_row(wiring.codec, 1, again));
    NETW_CHECK_EQ(Array(again.values[0]).size(), int64_t(1));
}

TEST_CASE(
    "[Networked][Predict][Hosted] PC2 a declared PackedByteArray has a zero "
    "row like every other container"
) {
    const Wiring wiring = wiring_of(Variant::PACKED_BYTE_ARRAY);
    StateRow zero;
    netw::predict::zero_state_row(wiring.codec, zero);
    REQUIRE(zero.has(0));
    NETW_CHECK_EQ(
        int(zero.values[0].get_type()),
        int(Variant::PACKED_BYTE_ARRAY)
    );
}

} // namespace TestNetwPredictContainerState
