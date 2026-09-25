#include "support/netw_test.h"

#if defined(NETW_TIER_HOSTED)

#include <cstdint>

#include <godot_cpp/classes/os.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_byte_array.hpp>

namespace TestNetwDictionaryMoveAssignLaws {

using namespace godot;

constexpr int ASSIGNMENTS = 1000;
constexpr int PAYLOAD_BYTES = 1024;
constexpr int64_t TOLERATED_GROWTH_BYTES = 64 * 1024;

Dictionary a_row(int p_index) {
    PackedByteArray payload;
    payload.resize(PAYLOAD_BYTES);
    Dictionary out;
    out["index"] = p_index;
    out["payload"] = payload;
    return out;
}

int64_t static_memory() {
    return int64_t(OS::get_singleton()->get_static_memory_usage());
}

TEST_CASE(
    "[Networked][Memory] a Dictionary move assignment releases the value it "
    "overwrites, so a thousand rows assigned into one live Dictionary leave "
    "static memory where it started") {
    Dictionary row = a_row(-1);
    const int64_t before = static_memory();
    for (int index = 0; index < ASSIGNMENTS; ++index) {
        row = a_row(index);
    }
    row = Dictionary();
    const int64_t growth = static_memory() - before;
    NETW_FORMAT_INT(growth_text, growth);
    CAPTURE(growth_text);
    CHECK(growth < TOLERATED_GROWTH_BYTES);
}

} // namespace TestNetwDictionaryMoveAssignLaws

#endif
