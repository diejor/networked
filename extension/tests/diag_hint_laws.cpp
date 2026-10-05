#include "support/netw_test.h"

#if defined(DEBUG_ENABLED)

#include "godot/variant.hpp"
#include "netw/diag/hints.hpp"

namespace TestDiagHintLaws {

using godot::PackedStringArray;
using godot::String;
using netw::diag::HINT_PREDICTION;
using netw::diag::HintRequest;
using netw::diag::parse_hints;

PackedStringArray args_with(const String &p_flag) {
    PackedStringArray args;
    args.push_back("--headless");
    args.push_back(p_flag);
    return args;
}

TEST_CASE(
    "[Networked][Diag][Hosted] H1 a hint list sets the bits it names and "
    "nothing else"
) {
    NETW_CHECK_EQ(
        parse_hints("", args_with("--netw-hints=prediction")).mask,
        uint32_t(HINT_PREDICTION)
    );
    NETW_CHECK_EQ(
        parse_hints("", args_with("--netw-hints= Prediction ,")).mask,
        uint32_t(HINT_PREDICTION)
    );
    NETW_CHECK_EQ(parse_hints("", args_with("--netw-hints=")).mask, 0u);
    NETW_CHECK_EQ(parse_hints("", args_with("--netw-hints=none")).mask, 0u);
    NETW_CHECK_EQ(parse_hints("", PackedStringArray()).mask, 0u);

    const HintRequest unknown
        = parse_hints("", args_with("--netw-hints=prediction,rings"));
    NETW_CHECK_EQ(unknown.mask, uint32_t(HINT_PREDICTION));
    NETW_CHECK_EQ(unknown.unknown.size(), 1);
    CHECK(unknown.unknown[0] == String("rings"));

    const HintRequest only_unknown
        = parse_hints("", args_with("--netw-hints=rings"));
    NETW_CHECK_EQ(only_unknown.mask, 0u);
    NETW_CHECK_EQ(only_unknown.unknown.size(), 1);
}

TEST_CASE(
    "[Networked][Diag][Hosted] H1 the environment outranks the flag"
) {
    NETW_CHECK_EQ(
        parse_hints("none", args_with("--netw-hints=prediction")).mask,
        0u
    );
    NETW_CHECK_EQ(
        parse_hints("prediction", args_with("--netw-hints=none")).mask,
        uint32_t(HINT_PREDICTION)
    );
}

} // namespace TestDiagHintLaws

#endif
