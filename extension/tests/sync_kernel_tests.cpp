#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/call_args.hpp"
#include "netw/staged_writes.hpp"
#include "netw/sync_kernel.hpp"

namespace TestNetwSyncKernel {

using namespace godot;
using netw::StagedWrites;
namespace sync_kernel = netw::sync_kernel;

Array keys_of(const std::initializer_list<const char *> &p_names) {
    Array out;
    for (const char *name : p_names) {
        out.push_back(StringName(name));
    }
    return out;
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK1 a volatile row round trips under the "
    "caller's keys, because the values self describe and neither side "
    "carries a schema"
) {
    Array values;
    values.push_back(true);
    values.push_back(int64_t(9));
    values.push_back(String("hp"));
    values.push_back(3.5);

    const PackedByteArray bytes = sync_kernel::encode_volatile(9, values);
    const StagedWrites staged = sync_kernel::decode_volatile(
        bytes,
        keys_of({"alive", "count", "label", "rate"})
    );

    REQUIRE(staged.is_valid());
    NETW_CHECK_EQ(staged.ordinal, 9);
    NETW_CHECK_EQ(int64_t(staged.values.size()), 4);
    CHECK(bool(staged.values[0]));
    NETW_CHECK_EQ(int64_t(staged.values[1]), 9);
    CHECK(String(staged.values[2]) == String("hp"));
    NETW_CHECK_CLOSE(double(staged.values[3]), 3.5, 0.0001);
    CHECK(bool(staged.row[StringName("alive")]));
    NETW_CHECK_EQ(int64_t(staged.row[StringName("count")]), 9);
    CHECK(String(staged.row[StringName("label")]) == String("hp"));
    NETW_CHECK_CLOSE(double(staged.row[StringName("rate")]), 3.5, 0.0001);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK2 a written flags byte is refused rather "
    "than read, because what follows was framed under a grammar this version "
    "does not have"
) {
    netw::wire::WriteStream stream;
    sync_kernel::VolatileHead head;
    head.flags = 1;
    Array values;
    values.push_back(int64_t(55));
    REQUIRE(sync_kernel::VolatileHead::wire.run(stream, head));
    REQUIRE(netw::call_args::values_write(stream, values, Array(), Array()));
    REQUIRE(stream.align_verify());

    const bool refused
        = !sync_kernel::decode_volatile(stream.to_bytes(), keys_of({"synced"}))
               .is_valid();
    CHECK(refused);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK3 a row that does not fill its keys is "
    "refused, because a partly named row is not a volatile row"
) {
    Array values;
    values.push_back(int64_t(1));
    values.push_back(int64_t(2));
    const PackedByteArray bytes = sync_kernel::encode_volatile(0, values);

    CHECK_FALSE(
        sync_kernel::decode_volatile(bytes, keys_of({"a", "b", "c"})).is_valid()
    );
    CHECK(sync_kernel::decode_volatile(bytes, keys_of({"a", "b"})).is_valid());
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK4 the encoded bytes are the pinned layout, "
    "field for field: an ordinal varuint, the flags byte, then the values"
) {
    Array values;
    values.push_back(int64_t(12));
    const PackedByteArray bytes = sync_kernel::encode_volatile(2, values);

    netw::wire::ReadStream reader(bytes);
    sync_kernel::VolatileHead head;
    REQUIRE(sync_kernel::VolatileHead::wire.run(reader, head));
    NETW_CHECK_EQ(int64_t(head.ordinal), 2);
    NETW_CHECK_EQ(int64_t(head.flags), int64_t(sync_kernel::RESERVED));

    Array read;
    REQUIRE(netw::call_args::values_read(reader, Array(), Array(), read));
    NETW_CHECK_EQ(int64_t(read.size()), 1);
    NETW_CHECK_EQ(int64_t(read[0]), 12);
    const bool exhausted
        = reader.align_verify() && reader.bits_remaining() == 0;
    CHECK(exhausted);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK5 a retained row round trips the keys its "
    "mask names, in ascending bit order"
) {
    Array values;
    values.push_back(int64_t(100));
    values.push_back(int64_t(300));
    const PackedByteArray bytes = sync_kernel::encode_retained(4, 5, values);
    const StagedWrites staged
        = sync_kernel::decode_retained(bytes, keys_of({"a", "b", "c", "d"}));

    REQUIRE(staged.is_valid());
    NETW_CHECK_EQ(staged.ordinal, 4);
    NETW_CHECK_EQ(int64_t(staged.keys.size()), 2);
    CHECK(StringName(staged.keys[0]) == StringName("a"));
    CHECK(StringName(staged.keys[1]) == StringName("c"));
    NETW_CHECK_EQ(int64_t(staged.row[StringName("a")]), 100);
    NETW_CHECK_EQ(int64_t(staged.row[StringName("c")]), 300);
    CHECK_FALSE(staged.row.has(StringName("b")));
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK6 a retained row that disagrees with its own "
    "mask is refused, because the mask is what names the values"
) {
    Array values;
    values.push_back(int64_t(1));
    values.push_back(int64_t(2));
    const PackedByteArray bytes = sync_kernel::encode_retained(0, 7, values);

    CHECK_FALSE(
        sync_kernel::decode_retained(bytes, keys_of({"a", "b", "c"})).is_valid()
    );
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK7 a row with a byte left over is refused, "
    "because a decoder that stops before its input is exhausted has read a "
    "row somebody else framed"
) {
    Array values;
    values.push_back(int64_t(7));

    PackedByteArray volatile_bytes = sync_kernel::encode_volatile(3, values);
    volatile_bytes.push_back(0);
    const bool volatile_refused
        = !sync_kernel::decode_volatile(volatile_bytes, keys_of({"a"}))
               .is_valid();
    CHECK(volatile_refused);

    PackedByteArray retained_bytes = sync_kernel::encode_retained(3, 1, values);
    retained_bytes.push_back(0);
    const bool retained_refused
        = !sync_kernel::decode_retained(retained_bytes, keys_of({"a"}))
               .is_valid();
    CHECK(retained_refused);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK8 a row whose last byte is missing is "
    "refused whole, because the values before the truncation name properties "
    "a game would otherwise be told changed"
) {
    Array values;
    values.push_back(int64_t(1000000));
    values.push_back(int64_t(2000000));

    PackedByteArray bytes = sync_kernel::encode_volatile(3, values);
    bytes.resize(bytes.size() - 1);
    const bool refused
        = !sync_kernel::decode_volatile(bytes, keys_of({"a", "b"})).is_valid();
    CHECK(refused);
}

TEST_CASE(
    "[Networked][Sync][Hosted] P13 replaying identical intake produces "
    "identical bytes, because the encoder reads nothing but its arguments"
) {
    Array values;
    values.push_back(int64_t(8));

    const PackedByteArray left = sync_kernel::encode_volatile(1, values);
    const PackedByteArray right = sync_kernel::encode_volatile(1, values);

    CHECK(left == right);

    Array mixed;
    mixed.push_back(true);
    mixed.push_back(String("ready"));
    mixed.push_back(-3.25);
    CHECK(
        sync_kernel::encode_volatile(9, mixed)
        == sync_kernel::encode_volatile(9, mixed)
    );

    Array retained;
    retained.push_back(int64_t(100));
    retained.push_back(int64_t(300));
    CHECK(
        sync_kernel::encode_retained(4, 5, retained)
        == sync_kernel::encode_retained(4, 5, retained)
    );
}

} // namespace TestNetwSyncKernel
