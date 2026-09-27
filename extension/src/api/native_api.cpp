#include "netw/api/native_api.hpp"

#include "godot/extension.hpp"
#include "godot/local_vector.hpp"

using namespace godot;

namespace netw {

struct ArgumentPack {
    LocalVector<Variant> values;
    LocalVector<const void *> pointers;
};

namespace {

constexpr int64_t ABI_VERSION = 3;

constexpr const char *EXPORTED[] = {
    "method_bind",
    "ptrcall0",
    "ptrcall1",
    "ptrcall2",
    "ptrcall3",
    "call0",
    "call1",
    "call2",
    "call3",
    "call4",
    "args_new",
    "args_set",
    "call_pack",
    "args_free",
    "retain",
    "release",
};

int64_t exported_hash() {
    uint64_t hash = 14695981039346656037ULL;
    for (const char *name : EXPORTED) {
        for (const char *at = name; *at != '\0'; ++at) {
            hash ^= uint64_t(uint8_t(*at));
            hash *= 1099511628211ULL;
        }
        hash ^= uint64_t(ABI_VERSION);
        hash *= 1099511628211ULL;
    }
    return int64_t(hash & 0x7fffffffffffffffULL);
}

int64_t address_of(void *p_function) {
    return int64_t(reinterpret_cast<intptr_t>(p_function));
}

} // namespace

Dictionary native_api_table() {
    Dictionary functions;
    functions["method_bind"]
        = address_of(reinterpret_cast<void *>(&netw_native_method_bind));
    functions["ptrcall0"]
        = address_of(reinterpret_cast<void *>(&netw_native_ptrcall0));
    functions["ptrcall1"]
        = address_of(reinterpret_cast<void *>(&netw_native_ptrcall1));
    functions["ptrcall2"]
        = address_of(reinterpret_cast<void *>(&netw_native_ptrcall2));
    functions["ptrcall3"]
        = address_of(reinterpret_cast<void *>(&netw_native_ptrcall3));
    functions["call0"]
        = address_of(reinterpret_cast<void *>(&netw_native_call0));
    functions["call1"]
        = address_of(reinterpret_cast<void *>(&netw_native_call1));
    functions["call2"]
        = address_of(reinterpret_cast<void *>(&netw_native_call2));
    functions["call3"]
        = address_of(reinterpret_cast<void *>(&netw_native_call3));
    functions["call4"]
        = address_of(reinterpret_cast<void *>(&netw_native_call4));
    functions["args_new"]
        = address_of(reinterpret_cast<void *>(&netw_native_args_new));
    functions["args_set"]
        = address_of(reinterpret_cast<void *>(&netw_native_args_set));
    functions["call_pack"]
        = address_of(reinterpret_cast<void *>(&netw_native_call_pack));
    functions["args_free"]
        = address_of(reinterpret_cast<void *>(&netw_native_args_free));
    functions["retain"]
        = address_of(reinterpret_cast<void *>(&netw_native_retain));
    functions["release"]
        = address_of(reinterpret_cast<void *>(&netw_native_release));

    Dictionary table;
    table["version"] = ABI_VERSION;
    table["hash"] = exported_hash();
    table["is_double"] = sizeof(real_t) == sizeof(double);
    table["functions"] = functions;
    return table;
}

} // namespace netw

extern "C" {

void *netw_native_method_bind(
    const char *p_class,
    const char *p_method,
    uint64_t p_hash
) {
    return netw::gd::method_bind(
        StringName(p_class),
        StringName(p_method),
        p_hash
    );
}

void netw_native_ptrcall0(void *p_bind, void *p_instance, void *r_return) {
    netw::gd::method_ptrcall(p_bind, p_instance, nullptr, r_return);
}

void netw_native_ptrcall1(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    void *r_return
) {
    const void *args[] = {p_a0};
    netw::gd::method_ptrcall(p_bind, p_instance, args, r_return);
}

void netw_native_ptrcall2(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    void *r_return
) {
    const void *args[] = {p_a0, p_a1};
    netw::gd::method_ptrcall(p_bind, p_instance, args, r_return);
}

void netw_native_ptrcall3(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    void *r_return
) {
    const void *args[] = {p_a0, p_a1, p_a2};
    netw::gd::method_ptrcall(p_bind, p_instance, args, r_return);
}

void netw_native_call0(void *p_bind, void *p_instance, void *r_return) {
    netw::gd::method_call(p_bind, p_instance, nullptr, 0, r_return);
}

void netw_native_call1(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    void *r_return
) {
    const void *args[] = {p_a0};
    netw::gd::method_call(p_bind, p_instance, args, 1, r_return);
}

void netw_native_call2(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    void *r_return
) {
    const void *args[] = {p_a0, p_a1};
    netw::gd::method_call(p_bind, p_instance, args, 2, r_return);
}

void netw_native_call3(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    void *r_return
) {
    const void *args[] = {p_a0, p_a1, p_a2};
    netw::gd::method_call(p_bind, p_instance, args, 3, r_return);
}

void netw_native_call4(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    const void *p_a3,
    void *r_return
) {
    const void *args[] = {p_a0, p_a1, p_a2, p_a3};
    netw::gd::method_call(p_bind, p_instance, args, 4, r_return);
}

void *netw_native_args_new(int64_t p_count) {
    if (p_count < 0) {
        return nullptr;
    }
    netw::ArgumentPack *pack = memnew(netw::ArgumentPack);
    pack->values.resize(uint32_t(p_count));
    pack->pointers.resize(uint32_t(p_count));
    return pack;
}

void netw_native_args_set(void *p_pack, int64_t p_index, const void *p_value) {
    netw::ArgumentPack *pack = reinterpret_cast<netw::ArgumentPack *>(p_pack);
    if (pack == nullptr || p_value == nullptr) {
        return;
    }
    if (p_index < 0 || uint32_t(p_index) >= pack->values.size()) {
        return;
    }
    pack->values[uint32_t(p_index)]
        = *reinterpret_cast<const Variant *>(p_value);
}

void netw_native_call_pack(
    void *p_bind,
    void *p_instance,
    void *p_pack,
    int64_t p_count,
    void *r_return
) {
    netw::ArgumentPack *pack = reinterpret_cast<netw::ArgumentPack *>(p_pack);
    if (pack == nullptr || p_count < 0
        || uint32_t(p_count) > pack->values.size()) {
        return;
    }
    for (uint32_t index = 0; index < uint32_t(p_count); ++index) {
        pack->pointers[index] = &pack->values[index];
    }
    netw::gd::method_call(
        p_bind,
        p_instance,
        p_count == 0 ? nullptr : pack->pointers.ptr(),
        p_count,
        r_return
    );
}

void netw_native_args_free(void *p_pack) {
    netw::ArgumentPack *pack = reinterpret_cast<netw::ArgumentPack *>(p_pack);
    if (pack != nullptr) {
        memdelete(pack);
    }
}

void netw_native_retain(void *p_instance) {
    netw::gd::retain_reference(p_instance);
}

void netw_native_release(void *p_instance) {
    netw::gd::release_reference(p_instance);
}
}
