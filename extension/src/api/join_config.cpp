#include "netw/api/join_config.hpp"

#include "godot/class_db.hpp"
#include "godot/vararg.hpp"
#include "netw/api/quantize.hpp"
#include "netw/log.hpp"
#include "netw/member_packing.hpp"
#include "netw/script/model.hpp"

using namespace godot;

namespace netw {

namespace {

Callable &arg_types_reader() {
    static Callable reader;
    return reader;
}

Array wire_arg_types(const Ref<Script> &p_script, const StringName &p_method) {
    const Array declared
        = netw::script::model::get_method_arg_types(p_script, p_method);
    return declared.is_empty() ? declared : declared.slice(1);
}

} // namespace

void NetwJoinConfig::set_arg_types_reader(const Callable &p_reader) {
    arg_types_reader() = p_reader;
}

Ref<NetwJoinConfig> NetwJoinConfig::quantize(const Array &p_quantizers) {
    if (p_quantizers.is_empty()) {
        NETW_ERROR(
            sys::SESSION,
            "NetwJoinConfig.quantize: '%s' was given no quantizer.",
            String(context_name)
        );
        return Ref<NetwJoinConfig>(this);
    }
    if (!declared_are_quantizers(
            p_quantizers,
            "NetwJoinConfig.quantize",
            context_name
        )) {
        return Ref<NetwJoinConfig>(this);
    }
    const Callable &reader = arg_types_reader();
    Array types;
    if (!context_name.is_empty()) {
        types = reader.is_valid()
            ? Array(reader.call(context_script, context_name))
            : wire_arg_types(context_script, context_name);
    }
    if (!declared_count_fits_arity(
            p_quantizers.size(),
            types,
            "NetwJoinConfig.quantize",
            context_name,
            context_script
        )) {
        return Ref<NetwJoinConfig>(this);
    }
    quantizers = p_quantizers.duplicate();
    quantizers_fit_types(quantizers, types, context_name, context_script);
    return Ref<NetwJoinConfig>(this);
}

void NetwJoinConfig::_bind_methods() {
    gd::bind_vararg_method(D_METHOD("quantize"), &NetwJoinConfig::quantize);
}

} // namespace netw
