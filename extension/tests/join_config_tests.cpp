#include "support/netw_test.h"

#include "godot/callable.hpp"
#include "netw/api/join_config.hpp"
#include "netw/api/quantize.hpp"
#include "support/netw_call_log.h"

namespace TestNetwJoinConfig {

using namespace godot;
using netw::NetwJoinConfig;
using netw::NetwQuantize;
using netw::NetwQuantizeScalar;
using netw_test::CallLog;

Ref<NetwJoinConfig> make_config() {
    Ref<NetwJoinConfig> config;
    config.instantiate();
    return config;
}

Ref<NetwQuantize> make_quantizer() {
    Ref<NetwQuantizeScalar> packer;
    packer.instantiate();
    return packer;
}

Variant at(const Array &p_list, int p_index) {
    return p_index < p_list.size() ? p_list[p_index] : Variant();
}

Array one(const Variant &p_value) {
    Array list;
    list.push_back(p_value);
    return list;
}

Ref<NetwQuantize> only_quantizer(const Array &p_list) {
    return p_list.size() == 1 ? Ref<NetwQuantize>(p_list[0])
                              : Ref<NetwQuantize>();
}

TEST_CASE(
    "[Networked][Session][Hosted] JC1 a config that declared nothing carries "
    "an empty quantizer list, so a handler that packed nothing sends every "
    "wire argument self-describing rather than reading an absent declaration"
) {
    const Ref<NetwJoinConfig> config = make_config();

    CHECK(config->get_quantizers().is_empty());
    CHECK(config->get_context_name().is_empty());
    CHECK(config->get_context_script().is_null());
}

TEST_CASE(
    "[Networked][Session][Hosted] JC2 the declaration is one quantizer per "
    "wire argument, so a handler taking two of them refuses a list of one "
    "rather than packing the first and leaving the second to guess"
) {
    const Ref<NetwJoinConfig> config = make_config();
    config->set_context_name(StringName("spawn_at"));
    const CallLog reader;
    Array types;
    types.push_back(int(Variant::VECTOR3));
    types.push_back(int(Variant::VECTOR3));
    NetwJoinConfig::set_arg_types_reader(reader.answering("types", types));
    const Ref<NetwQuantize> packer = make_quantizer();

    Array declared;
    declared.push_back(packer);
    declared.push_back(Variant());
    config->quantize(declared);
    NETW_CHECK_EQ(config->get_quantizers().size(), 2);

    config->quantize(one(packer));

    NETW_CHECK_EQ(config->get_quantizers().size(), 2);
    NetwJoinConfig::set_arg_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] JC3 an argument that is not a NetwQuantize "
    "leaves the declared list standing, so a typo in one chained call cannot "
    "silently unpack a schema already declared"
) {
    const Ref<NetwJoinConfig> config = make_config();
    const Ref<NetwQuantize> packer = make_quantizer();
    config->quantize(one(packer));

    config->quantize(one(Variant(7)));

    NETW_CHECK_EQ(config->get_quantizers().size(), 1);
    CHECK(bool(only_quantizer(config->get_quantizers()) == packer));
}

TEST_CASE(
    "[Networked][Session][Hosted] JC4 quantize answers the very config it was "
    "called on, which is what lets one _init register the handler and declare "
    "its packing in a single chain"
) {
    const Ref<NetwJoinConfig> config = make_config();

    CHECK(bool(config->quantize(Array()) == config));
}

TEST_CASE(
    "[Networked][Session][Hosted] JC5 the installed types reader is asked "
    "about the handler and a quantizer that cannot pack the type it reports "
    "is reported without dropping the declaration, because the GDScript "
    "spelling it replaces was an assert a release build strips"
) {
    const Ref<NetwJoinConfig> config = make_config();
    config->set_context_name(StringName("spawn_at"));
    const CallLog reader;
    Array types;
    types.push_back(int(Variant::STRING));
    NetwJoinConfig::set_arg_types_reader(reader.answering("types", types));
    const Ref<NetwQuantize> packer = make_quantizer();

    config->quantize(one(packer));

    NETW_CHECK_EQ(reader.count("types"), 1);
    const Array asked = reader.args("types");
    NETW_CHECK_EQ(asked.size(), 2);
    CHECK(bool(StringName(at(asked, 1)) == StringName("spawn_at")));
    NETW_CHECK_EQ(config->get_quantizers().size(), 1);

    NetwJoinConfig::set_arg_types_reader(Callable());
}

TEST_CASE(
    "[Networked][Session][Hosted] JC6 a refused argument never reaches the "
    "types reader, because there is no new declaration to judge and the "
    "standing one was already judged when it was declared"
) {
    const Ref<NetwJoinConfig> config = make_config();
    config->set_context_name(StringName("spawn_at"));
    const CallLog reader;
    NetwJoinConfig::set_arg_types_reader(reader.answering("types", Array()));

    config->quantize(one(Variant(7)));

    NETW_CHECK_EQ(reader.count("types"), 0);

    NetwJoinConfig::set_arg_types_reader(Callable());
}

} // namespace TestNetwJoinConfig
