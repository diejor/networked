#include "netw/member_packing.hpp"

#include "netw/api/interpolate.hpp"
#include "netw/api/quantize.hpp"
#include "netw/log.hpp"

using namespace godot;

namespace netw {

namespace {

String script_file(const Ref<Script> &p_script) {
    return p_script.is_valid() ? p_script->get_path().get_file() : String();
}

int declared_type(const Array &p_types, int p_index) {
    return p_index < p_types.size() ? int(p_types[p_index]) : int(Variant::NIL);
}

String type_label(int p_type) {
    return Variant::get_type_name(Variant::Type(p_type));
}

template <typename T>
bool declared_slots_are(
    const Array &p_declared,
    const String &p_verb,
    const String &p_expected,
    const StringName &p_member
) {
    for (int index = 0; index < p_declared.size(); index++) {
        const Variant slot = p_declared[index];
        if (slot.get_type() == Variant::NIL || Ref<T>(slot).is_valid()) {
            continue;
        }
        NETW_ERROR(
            sys::SESSION,
            "%s: argument %d of '%s' is a %s rather than a %s.",
            p_verb,
            index,
            String(p_member),
            type_label(int(slot.get_type())),
            p_expected
        );
        return false;
    }
    return true;
}

} // namespace

bool declared_count_fits_arity(
    int p_declared_count,
    const Array &p_types,
    const String &p_verb,
    const StringName &p_member,
    const Ref<Script> &p_script
) {
    if (p_types.is_empty() || p_declared_count == p_types.size()) {
        return true;
    }
    NETW_ERROR(
        sys::SESSION,
        "%s: '%s' on script '%s' takes %d arguments and %d were declared. "
        "Pass one per argument, and null for an argument that travels "
        "unchanged.",
        p_verb,
        String(p_member),
        script_file(p_script),
        p_types.size(),
        p_declared_count
    );
    return false;
}

bool declared_are_quantizers(
    const Array &p_declared,
    const String &p_verb,
    const StringName &p_member
) {
    return declared_slots_are<NetwQuantize>(
        p_declared,
        p_verb,
        "NetwQuantize",
        p_member
    );
}

bool declared_are_interpolators(
    const Array &p_declared,
    const String &p_verb,
    const StringName &p_member
) {
    return declared_slots_are<NetwInterpolate>(
        p_declared,
        p_verb,
        "NetwInterpolate",
        p_member
    );
}

bool quantizers_fit_types(
    const Array &p_quantizers,
    const Array &p_types,
    const StringName &p_member,
    const Ref<Script> &p_script
) {
    for (int index = 0; index < p_quantizers.size(); index++) {
        const Ref<NetwQuantize> packer = p_quantizers[index];
        if (packer.is_null()) {
            continue;
        }
        const int type = declared_type(p_types, index);
        if (type == int(Variant::NIL)
            || packer->supports_type(static_cast<Variant::Type>(type))) {
            continue;
        }
        NETW_WARN(
            sys::SESSION,
            "Quantizer of type '%s' does not support the declared type '%s' "
            "for '%s' on script '%s'.",
            packer->get_class(),
            type_label(type),
            String(p_member),
            script_file(p_script)
        );
        return false;
    }
    return true;
}

bool interpolators_fit_types(
    const Array &p_interpolators,
    const Array &p_types,
    const StringName &p_member,
    const Ref<Script> &p_script
) {
    for (int index = 0; index < p_interpolators.size(); index++) {
        const Ref<NetwInterpolate> smoother = p_interpolators[index];
        if (smoother.is_null()) {
            continue;
        }
        const int type = declared_type(p_types, index);
        if (type == int(Variant::NIL) || smoother->supports_type(type)) {
            continue;
        }
        NETW_WARN(
            sys::SESSION,
            "Interpolator does not support the declared type '%s' for '%s' "
            "on script '%s'.",
            type_label(type),
            String(p_member),
            script_file(p_script)
        );
        return false;
    }
    return true;
}

} // namespace netw
