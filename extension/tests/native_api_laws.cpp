#include "support/netw_test.h"

#include "godot/class_db.hpp"
#include "godot/extension.hpp"
#include "netw/api/bit_stream.hpp"
#include "netw/api/clock_config.hpp"
#include "netw/api/native_api.hpp"

namespace TestNetwNativeApi {

using namespace godot;
using netw::NetwBitStream;
using netw::NetwClockConfig;

namespace {

using MethodBindFn = void *(*)(const char *, const char *, uint64_t);
using Ptrcall0Fn = void (*)(void *, void *, void *);
using Ptrcall1Fn = void (*)(void *, void *, const void *, void *);
using Ptrcall2Fn = void (*)(void *, void *, const void *, const void *, void *);
using Ptrcall3Fn = void (*)(
    void *,
    void *,
    const void *,
    const void *,
    const void *,
    void *
);
using Call0Fn = void (*)(void *, void *, void *);
using Call1Fn = void (*)(void *, void *, const void *, void *);
using Call2Fn = void (*)(void *, void *, const void *, const void *, void *);
using ArgsNewFn = void *(*)(int64_t);
using ArgsSetFn = void (*)(void *, int64_t, const void *);
using CallPackFn = void (*)(void *, void *, void *, int64_t, void *);
using ArgsFreeFn = void (*)(void *);
using RetainFn = void (*)(void *);
using ReleaseFn = void (*)(void *);

constexpr uint64_t REFERENCE_COUNT_HASH = 3905245786;
constexpr uint64_t SET_META_HASH = 3776071444;
constexpr uint64_t GET_META_HASH = 3990617847;
constexpr uint64_t GET_CLASS_HASH = 201670096;
constexpr uint64_t BIT_LENGTH_HASH = 2455072627;
constexpr uint64_t BOOL1_HASH = 2703660260;
constexpr uint64_t BITS_HASH = 50157827;
constexpr uint64_t INT_RANGE_HASH = 4124862902;
constexpr uint64_t WRITER_HASH = 1748976972;

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

template <typename T>
T function_at(const Dictionary &p_table, const char *p_name) {
    const Dictionary functions = p_table["functions"];
    return reinterpret_cast<T>(
        static_cast<intptr_t>(int64_t(functions[p_name]))
    );
}

} // namespace

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA1 the table publishes every address"
) {
    const Dictionary table = netw::native_api_table();

    NETW_CHECK_EQ(int64_t(table["version"]), int64_t(3));
    CHECK(int64_t(table["hash"]) != 0);

    const Dictionary functions = table["functions"];
    NETW_CHECK_EQ(
        functions.size(),
        int(sizeof(EXPORTED) / sizeof(EXPORTED[0]))
    );
    for (const char *name : EXPORTED) {
        CHECK(int64_t(functions[name]) != 0);
    }
}

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA2 a bind answers for a real method"
) {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");

    void *found
        = bind_of("RefCounted", "get_reference_count", REFERENCE_COUNT_HASH);
    CHECK(found != nullptr);

    void *missing = bind_of("RefCounted", "no_such_method", 1);
    CHECK(missing == nullptr);
}

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA3 a ptrcall reaches a live object"
) {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto ptrcall0 = function_at<Ptrcall0Fn>(table, "ptrcall0");
    const auto release = function_at<ReleaseFn>(table, "release");

    Ref<NetwClockConfig> config;
    config.instantiate();
    void *raw = netw::gd::engine_object(config.ptr());

    void *bind
        = bind_of("RefCounted", "get_reference_count", REFERENCE_COUNT_HASH);
    REQUIRE(bind != nullptr);

    int64_t before = 0;
    ptrcall0(bind, raw, &before);
    NETW_CHECK_EQ(before, int64_t(config->get_reference_count()));

    config->reference();
    int64_t after = 0;
    ptrcall0(bind, raw, &after);
    NETW_CHECK_EQ(after, before + 1);

    release(raw);
    int64_t settled = 0;
    ptrcall0(bind, raw, &settled);
    NETW_CHECK_EQ(settled, before);
}

TEST_CASE("[Networked][NativeApi][Hosted] NA4 a retain balances a release") {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto ptrcall0 = function_at<Ptrcall0Fn>(table, "ptrcall0");
    const auto retain = function_at<RetainFn>(table, "retain");
    const auto release = function_at<ReleaseFn>(table, "release");

    Ref<NetwClockConfig> config;
    config.instantiate();
    void *raw = netw::gd::engine_object(config.ptr());

    void *bind
        = bind_of("RefCounted", "get_reference_count", REFERENCE_COUNT_HASH);
    REQUIRE(bind != nullptr);

    int64_t before = 0;
    ptrcall0(bind, raw, &before);

    retain(raw);
    int64_t held = 0;
    ptrcall0(bind, raw, &held);
    NETW_CHECK_EQ(held, before + 1);

    release(raw);
    int64_t settled = 0;
    ptrcall0(bind, raw, &settled);
    NETW_CHECK_EQ(settled, before);
}

TEST_CASE("[Networked][NativeApi][Hosted] NA5 a call carries a StringName") {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto call1 = function_at<Call1Fn>(table, "call1");
    const auto call2 = function_at<Call2Fn>(table, "call2");

    Ref<NetwClockConfig> config;
    config.instantiate();
    void *raw = netw::gd::engine_object(config.ptr());

    void *setter = bind_of("Object", "set_meta", SET_META_HASH);
    void *getter = bind_of("Object", "get_meta", GET_META_HASH);
    REQUIRE(setter != nullptr);
    REQUIRE(getter != nullptr);

    const Variant name = StringName("probe");
    const Variant written = 7;
    Variant discarded;
    call2(setter, raw, &name, &written, &discarded);

    Variant answered;
    call1(getter, raw, &name, &answered);
    NETW_CHECK_EQ(int64_t(answered), int64_t(7));
}

TEST_CASE("[Networked][NativeApi][Hosted] NA6 a call answers a String") {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto call0 = function_at<Call0Fn>(table, "call0");

    Ref<NetwClockConfig> config;
    config.instantiate();
    void *raw = netw::gd::engine_object(config.ptr());

    void *bind = bind_of("Object", "get_class", GET_CLASS_HASH);
    REQUIRE(bind != nullptr);

    Variant answered;
    call0(bind, raw, &answered);
    CHECK(String(answered) == String("NetwClockConfig"));
}

TEST_CASE("[Networked][NativeApi][Hosted] NA7 a thunk gathers every arity") {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto ptrcall0 = function_at<Ptrcall0Fn>(table, "ptrcall0");
    const auto ptrcall1 = function_at<Ptrcall1Fn>(table, "ptrcall1");
    const auto ptrcall2 = function_at<Ptrcall2Fn>(table, "ptrcall2");
    const auto ptrcall3 = function_at<Ptrcall3Fn>(table, "ptrcall3");

    void *length = bind_of("NetwBitStream", "bit_length", BIT_LENGTH_HASH);
    void *one = bind_of("NetwBitStream", "bool1", BOOL1_HASH);
    void *bits = bind_of("NetwBitStream", "bits", BITS_HASH);
    void *ranged = bind_of("NetwBitStream", "int_range", INT_RANGE_HASH);
    REQUIRE(length != nullptr);
    REQUIRE(one != nullptr);
    REQUIRE(bits != nullptr);
    REQUIRE(ranged != nullptr);

    const Ref<NetwBitStream> measurer = NetwBitStream::measurer();
    REQUIRE(measurer.is_valid());
    void *raw = netw::gd::engine_object(measurer.ptr());

    int64_t opened = 0;
    ptrcall0(length, raw, &opened);
    NETW_CHECK_EQ(opened, int64_t(0));

    const bool asked = true;
    bool answered = false;
    ptrcall1(one, raw, &asked, &answered);
    CHECK(answered);
    int64_t after_one = 0;
    ptrcall0(length, raw, &after_one);
    NETW_CHECK_EQ(after_one, int64_t(1));

    const int64_t value = 5;
    const int64_t count = 4;
    int64_t echoed = 0;
    ptrcall2(bits, raw, &value, &count, &echoed);
    NETW_CHECK_EQ(echoed, value);
    int64_t after_bits = 0;
    ptrcall0(length, raw, &after_bits);
    NETW_CHECK_EQ(after_bits, int64_t(5));

    const int64_t low = 0;
    const int64_t high = 15;
    const int64_t within = 7;
    int64_t clamped = 0;
    ptrcall3(ranged, raw, &within, &low, &high, &clamped);
    NETW_CHECK_EQ(clamped, within);
    int64_t after_range = 0;
    ptrcall0(length, raw, &after_range);
    CHECK(after_range > after_bits);
}

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA8 an argument pack carries a tail"
) {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto call1 = function_at<Call1Fn>(table, "call1");
    const auto args_new = function_at<ArgsNewFn>(table, "args_new");
    const auto args_set = function_at<ArgsSetFn>(table, "args_set");
    const auto call_pack = function_at<CallPackFn>(table, "call_pack");
    const auto args_free = function_at<ArgsFreeFn>(table, "args_free");

    Ref<NetwClockConfig> config;
    config.instantiate();
    void *raw = netw::gd::engine_object(config.ptr());

    void *setter = bind_of("Object", "set_meta", SET_META_HASH);
    void *getter = bind_of("Object", "get_meta", GET_META_HASH);
    REQUIRE(setter != nullptr);
    REQUIRE(getter != nullptr);

    const Variant name = StringName("packed");
    const Variant written = String("carried");

    void *pack = args_new(2);
    REQUIRE(pack != nullptr);
    args_set(pack, 0, &name);
    args_set(pack, 1, &written);
    Variant discarded;
    call_pack(setter, raw, pack, 2, &discarded);
    args_free(pack);

    Variant answered;
    call1(getter, raw, &name, &answered);
    CHECK(String(answered) == String("carried"));
}

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA9 a pack holds an index it never sized"
) {
    const Dictionary table = netw::native_api_table();
    const auto args_new = function_at<ArgsNewFn>(table, "args_new");
    const auto args_set = function_at<ArgsSetFn>(table, "args_set");
    const auto call_pack = function_at<CallPackFn>(table, "call_pack");
    const auto args_free = function_at<ArgsFreeFn>(table, "args_free");

    CHECK(args_new(-1) == nullptr);

    void *pack = args_new(1);
    REQUIRE(pack != nullptr);
    const Variant written = 3;
    args_set(pack, -1, &written);
    args_set(pack, 1, &written);
    args_set(pack, 0, nullptr);
    Variant discarded;
    call_pack(nullptr, nullptr, pack, 4, &discarded);
    args_free(pack);
    CHECK(discarded.get_type() == Variant::NIL);
}

TEST_CASE(
    "[Networked][NativeApi][Hosted] NA10 a ptrcall answers a refcounted object"
) {
    const Dictionary table = netw::native_api_table();
    const auto bind_of = function_at<MethodBindFn>(table, "method_bind");
    const auto ptrcall0 = function_at<Ptrcall0Fn>(table, "ptrcall0");
    const auto ptrcall1 = function_at<Ptrcall1Fn>(table, "ptrcall1");
    const auto release = function_at<ReleaseFn>(table, "release");

    void *make = bind_of("NetwBitStream", "writer", WRITER_HASH);
    void *count
        = bind_of("RefCounted", "get_reference_count", REFERENCE_COUNT_HASH);
    void *one = bind_of("NetwBitStream", "bool1", BOOL1_HASH);
    void *length = bind_of("NetwBitStream", "bit_length", BIT_LENGTH_HASH);
    REQUIRE(make != nullptr);
    REQUIRE(count != nullptr);

    void *answered = nullptr;
    ptrcall0(make, nullptr, &answered);
    REQUIRE(answered != nullptr);

    int64_t held = 0;
    ptrcall0(count, answered, &held);
    NETW_CHECK_EQ(held, int64_t(1));

    const bool asked = true;
    bool echoed = false;
    ptrcall1(one, answered, &asked, &echoed);
    CHECK(echoed);
    int64_t written = 0;
    ptrcall0(length, answered, &written);
    NETW_CHECK_EQ(written, int64_t(1));

    release(answered);
}

} // namespace TestNetwNativeApi
