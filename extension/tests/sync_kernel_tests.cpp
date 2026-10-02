#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/call_args.hpp"
#include "netw/sync_kernel.hpp"

namespace TestNetwSyncKernel {

using namespace godot;
namespace sync_kernel = netw::sync_kernel;

bool decoded_volatile(
    const PackedByteArray &p_bytes,
    int64_t p_columns,
    sync_kernel::DecodedRow &r_row
) {
    return sync_kernel::decode_volatile(p_bytes, p_columns, r_row);
}

bool refused_volatile(const PackedByteArray &p_bytes, int64_t p_columns) {
    sync_kernel::DecodedRow row;
    return !sync_kernel::decode_volatile(p_bytes, p_columns, row);
}

bool refused_retained(const PackedByteArray &p_bytes, int64_t p_columns) {
    sync_kernel::DecodedRow row;
    return !sync_kernel::decode_retained(p_bytes, p_columns, row);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK1 a volatile row round trips every column "
    "in order, because the values self describe and neither side carries a "
    "schema"
) {
    Array values;
    values.push_back(true);
    values.push_back(int64_t(9));
    values.push_back(String("hp"));
    values.push_back(3.5);

    const PackedByteArray bytes = sync_kernel::encode_volatile(9, values);
    sync_kernel::DecodedRow row;
    REQUIRE(decoded_volatile(bytes, 4, row));

    NETW_CHECK_EQ(row.ordinal, 9);
    NETW_CHECK_EQ(int64_t(row.mask), int64_t(0xF));
    NETW_CHECK_EQ(int64_t(row.values.size()), 4);
    CHECK(bool(row.values[0]));
    NETW_CHECK_EQ(int64_t(row.values[1]), 9);
    CHECK(String(row.values[2]) == String("hp"));
    NETW_CHECK_CLOSE(double(row.values[3]), 3.5, 0.0001);
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

    CHECK(refused_volatile(stream.to_bytes(), 1));
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK3 a row that does not fill its columns is "
    "refused, because a partly named row is not a volatile row"
) {
    Array values;
    values.push_back(int64_t(1));
    values.push_back(int64_t(2));
    const PackedByteArray bytes = sync_kernel::encode_volatile(0, values);

    CHECK(refused_volatile(bytes, 3));
    CHECK_FALSE(refused_volatile(bytes, 2));
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
    "[Networked][Sync][Hosted] SK5 a retained row round trips the columns its "
    "mask names, in ascending bit order"
) {
    Array values;
    values.push_back(int64_t(100));
    values.push_back(int64_t(300));
    const PackedByteArray bytes = sync_kernel::encode_retained(4, 5, values);
    sync_kernel::DecodedRow row;
    REQUIRE(sync_kernel::decode_retained(bytes, 4, row));

    NETW_CHECK_EQ(row.ordinal, 4);
    NETW_CHECK_EQ(int64_t(row.mask), int64_t(5));
    NETW_CHECK_EQ(int64_t(row.values.size()), 2);
    NETW_CHECK_EQ(int64_t(row.values[0]), 100);
    NETW_CHECK_EQ(int64_t(row.values[1]), 300);
}

TEST_CASE(
    "[Networked][Sync][Hosted] SK6 a retained row that disagrees with its own "
    "mask is refused, because the mask is what names the values"
) {
    Array values;
    values.push_back(int64_t(1));
    values.push_back(int64_t(2));
    const PackedByteArray bytes = sync_kernel::encode_retained(0, 7, values);

    CHECK(refused_retained(bytes, 3));
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
    CHECK(refused_volatile(volatile_bytes, 1));

    PackedByteArray retained_bytes = sync_kernel::encode_retained(3, 1, values);
    retained_bytes.push_back(0);
    CHECK(refused_retained(retained_bytes, 1));
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
    CHECK(refused_volatile(bytes, 2));
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
