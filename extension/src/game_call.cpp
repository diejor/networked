#include "netw/game_call.hpp"

#include "godot/object.hpp"
#include "godot/templates.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

HashSet<uint64_t> &reported_calls() {
    static HashSet<uint64_t> keys;
    return keys;
}

bool first_report(const Callable &p_fn, const GameCall &p_site) {
    const uint64_t key
        = (uint64_t(p_fn.hash()) << 32) ^ uint64_t(String(p_site.what).hash());
    if (reported_calls().has(key)) {
        return false;
    }
    reported_calls().insert(key);
    return true;
}

} // namespace

bool game_call_accepts(const Callable &p_fn, int64_t p_count) {
    if (!p_fn.is_valid() || p_fn.is_custom()) {
        return true;
    }
    Object *object = p_fn.get_object();
    if (object == nullptr) {
        return true;
    }
    const StringName method = p_fn.get_method();
    const Array methods = gd::method_list(object);
    for (int at = 0; at < methods.size(); ++at) {
        const Dictionary info = methods[at];
        if (StringName(info["name"]) != method) {
            continue;
        }
        const int64_t total = Array(info["args"]).size();
        const int64_t optional = Array(info["default_args"]).size();
        const bool vararg
            = (int64_t(info["flags"]) & int64_t(METHOD_FLAG_VARARG)) != 0;
        return p_count >= total - optional && (vararg || p_count <= total);
    }
    return true;
}

bool game_call_declared(const Callable &p_fn, const GameCall &p_site) {
    if (game_call_accepts(p_fn, p_site.argument_count)) {
        return true;
    }
    if (first_report(p_fn, p_site)) {
        NETW_ERROR(
            p_site.system,
            "%s takes the wrong arguments, so it will not run. Declare it as "
            "%s.",
            String(p_site.what),
            String(p_site.signature)
        );
    }
    return false;
}

Variant game_call(
    const Callable &p_fn,
    const Array &p_args,
    const GameCall &p_site,
    bool *r_called
) {
    bool called = false;
    const Variant result = gd::call_checked(p_fn, p_args, called);
    if (r_called != nullptr) {
        *r_called = called;
    }
    if (!called && p_fn.is_valid() && first_report(p_fn, p_site)) {
        NETW_ERROR(
            p_site.system,
            "%s could not be called with %d arguments, so it did not run. "
            "Declare it as %s.",
            String(p_site.what),
            int64_t(p_args.size()),
            String(p_site.signature)
        );
    }
    return result;
}

} // namespace netw
