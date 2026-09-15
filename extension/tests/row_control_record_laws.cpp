#include "support/netw_test.h"

#include <cstdint>

#include "godot/variant.hpp"
#include "netw/wire/control_record.hpp"

namespace TestRowControlRecord {

using netw::wire::ACCEPT_MAX_ENTRIES;
using netw::wire::ControlAcceptEntry;
using netw::wire::ControlRecord;
using netw::wire::ControlTag;
using netw::wire::read_control_record;
using netw::wire::StreamFamily;
using netw::wire::write_control_record;

godot::PackedByteArray blob(std::initializer_list<uint8_t> p_bytes) {
    godot::PackedByteArray out;
    for (uint8_t byte : p_bytes) {
        out.push_back(byte);
    }
    return out;
}

ControlRecord open_of(uint64_t p_request) {
    ControlRecord record;
    record.tag = ControlTag::OPEN;
    record.request = p_request;
    record.route = 300;
    record.ordinal = 2;
    record.family = StreamFamily::VOLATILE;
    record.epoch = 5;
    record.schema = 0xDEADBEEF;
    return record;
}

ControlRecord accept_of(uint32_t p_count) {
    ControlRecord record;
    record.tag = ControlTag::ACCEPT;
    for (uint32_t at = 0; at < p_count; ++at) {
        ControlAcceptEntry entry;
        entry.token = uint64_t(at) + 1;
        entry.revision = uint64_t(at) + 1;
        record.receipts.push_back(entry);
    }
    return record;
}

TEST_CASE(
    "[Networked][Wire][Hosted] every control record writes the bytes WIRE.md "
    "10.6 transcribes"
) {
    NETW_CHECK_EQ(
        int(write_control_record(open_of(1))
                == blob(
                    {0x00,
                     0x01,
                     0xAC,
                     0x02,
                     0x02,
                     0x00,
                     0x05,
                     0xEF,
                     0xBE,
                     0xAD,
                     0xDE}
                )),
        1
    );

    ControlRecord ready;
    ready.tag = ControlTag::READY;
    ready.request = 300;
    ready.token = 7;
    NETW_CHECK_EQ(
        int(write_control_record(ready) == blob({0x01, 0xAC, 0x02, 0x07})),
        1
    );

    ControlRecord receipts;
    receipts.tag = ControlTag::ACCEPT;
    ControlAcceptEntry first;
    first.token = 7;
    first.revision = 3;
    ControlAcceptEntry second;
    second.token = 9;
    second.revision = 1;
    receipts.receipts.push_back(first);
    receipts.receipts.push_back(second);
    NETW_CHECK_EQ(
        int(write_control_record(receipts)
            == blob({0x02, 0x01, 0x07, 0x03, 0x09, 0x01})),
        1
    );

    ControlRecord closing;
    closing.tag = ControlTag::CLOSE;
    closing.token = 7;
    NETW_CHECK_EQ(
        int(write_control_record(closing) == blob({0x04, 0x07})),
        1
    );
}

TEST_CASE(
    "[Networked][Wire][Hosted] a control record round trips through its own "
    "codec"
) {
    ControlRecord read;
    const bool opened
        = read_control_record(write_control_record(open_of(41)), read);
    CHECK(opened);
    NETW_CHECK_EQ(int(read.tag), int(ControlTag::OPEN));
    NETW_CHECK_EQ(read.request, 41);
    NETW_CHECK_EQ(read.route, 300);
    NETW_CHECK_EQ(read.ordinal, 2);
    NETW_CHECK_EQ(int(read.family), int(StreamFamily::VOLATILE));
    NETW_CHECK_EQ(read.epoch, 5);
    NETW_CHECK_EQ(int64_t(read.schema), int64_t(0xDEADBEEF));

    ControlRecord reset;
    reset.tag = ControlTag::RESET;
    reset.request = 12;
    reset.token = 9001;
    ControlRecord back;
    const bool carried
        = read_control_record(write_control_record(reset), back);
    CHECK(carried);
    NETW_CHECK_EQ(int(back.tag), int(ControlTag::RESET));
    NETW_CHECK_EQ(back.request, 12);
    NETW_CHECK_EQ(back.token, 9001);

    ControlRecord many;
    const bool batched = read_control_record(
        write_control_record(accept_of(ACCEPT_MAX_ENTRIES)),
        many
    );
    CHECK(batched);
    NETW_CHECK_EQ(many.receipts.size(), ACCEPT_MAX_ENTRIES);
    NETW_CHECK_EQ(many.receipts[31].token, 32);
    NETW_CHECK_EQ(many.receipts[31].revision, 32);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an undeclared control tag installs nothing"
) {
    ControlRecord read;
    read.token = 555;
    const bool refused = read_control_record(blob({0x05, 0x01, 0x01}), read);
    CHECK(!refused);
    NETW_CHECK_EQ(read.token, 555);

    const bool empty = read_control_record(godot::PackedByteArray(), read);
    CHECK(!empty);
    NETW_CHECK_EQ(read.token, 555);
}

TEST_CASE(
    "[Networked][Wire][Hosted] an undeclared stream family refuses its OPEN"
) {
    ControlRecord record = open_of(1);
    record.family = StreamFamily(uint8_t(3));
    ControlRecord read;
    read.request = 77;
    const bool refused
        = read_control_record(write_control_record(record), read);
    CHECK(!refused);
    NETW_CHECK_EQ(read.request, 77);

    record.family = StreamFamily::WINDOW;
    ControlRecord window;
    const bool carried
        = read_control_record(write_control_record(record), window);
    CHECK(carried);
    NETW_CHECK_EQ(int(window.family), int(StreamFamily::WINDOW));
}

TEST_CASE(
    "[Networked][Wire][Hosted] an ACCEPT count outside one through 32 cannot "
    "be written"
) {
    const bool none = write_control_record(accept_of(0)).is_empty();
    CHECK(none);
    const bool over
        = write_control_record(accept_of(ACCEPT_MAX_ENTRIES + 1)).is_empty();
    CHECK(over);
    const bool one = write_control_record(accept_of(1)).is_empty();
    CHECK(!one);
}

TEST_CASE(
    "[Networked][Wire][Hosted] a receipt naming revision zero or token zero "
    "is refused"
) {
    ControlRecord record = accept_of(1);
    record.receipts[0].revision = 0;
    ControlRecord read;
    read.token = 4;
    const bool no_revision
        = read_control_record(write_control_record(record), read);
    CHECK(!no_revision);
    NETW_CHECK_EQ(read.token, 4);

    record.receipts[0].revision = 1;
    record.receipts[0].token = 0;
    const bool no_token
        = read_control_record(write_control_record(record), read);
    CHECK(!no_token);
    NETW_CHECK_EQ(read.token, 4);
}

TEST_CASE(
    "[Networked][Wire][Hosted] residue and truncation both refuse a control "
    "record whole"
) {
    ControlRecord ready;
    ready.tag = ControlTag::READY;
    ready.request = 300;
    ready.token = 7;
    const godot::PackedByteArray whole = write_control_record(ready);

    godot::PackedByteArray longer = whole;
    longer.push_back(0xFF);
    ControlRecord read;
    read.request = 21;
    const bool residue = read_control_record(longer, read);
    CHECK(!residue);
    NETW_CHECK_EQ(read.request, 21);

    godot::PackedByteArray shorter = whole;
    shorter.resize(whole.size() - 1);
    const bool truncated = read_control_record(shorter, read);
    CHECK(!truncated);
    NETW_CHECK_EQ(read.request, 21);

    godot::PackedByteArray partial
        = write_control_record(accept_of(ACCEPT_MAX_ENTRIES));
    partial.resize(partial.size() - 1);
    const bool incomplete = read_control_record(partial, read);
    CHECK(!incomplete);
    NETW_CHECK_EQ(read.request, 21);
}

} // namespace TestRowControlRecord
