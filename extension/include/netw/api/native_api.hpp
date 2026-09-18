#pragma once

#include "godot/variant.hpp"

#include <cstdint>

extern "C" {

void *netw_native_method_bind(
    const char *p_class,
    const char *p_method,
    uint64_t p_hash
);

void netw_native_ptrcall0(void *p_bind, void *p_instance, void *r_return);

void netw_native_ptrcall1(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    void *r_return
);

void netw_native_ptrcall2(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    void *r_return
);

void netw_native_ptrcall3(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    void *r_return
);

void netw_native_call0(void *p_bind, void *p_instance, void *r_return);

void netw_native_call1(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    void *r_return
);

void netw_native_call2(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    void *r_return
);

void netw_native_call3(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    void *r_return
);

void netw_native_call4(
    void *p_bind,
    void *p_instance,
    const void *p_a0,
    const void *p_a1,
    const void *p_a2,
    const void *p_a3,
    void *r_return
);

void *netw_native_args_new(int64_t p_count);

void netw_native_args_set(void *p_pack, int64_t p_index, const void *p_value);

void netw_native_call_pack(
    void *p_bind,
    void *p_instance,
    void *p_pack,
    int64_t p_count,
    void *r_return
);

void netw_native_args_free(void *p_pack);

void netw_native_retain(void *p_instance);

void netw_native_release(void *p_instance);
}

namespace netw {

godot::Dictionary native_api_table();

} // namespace netw
