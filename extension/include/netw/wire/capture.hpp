#pragma once

#include <cstdint>

#include "godot/file_access.hpp"
#include "godot/variant.hpp"
#include "netw/wire/describe.hpp"

namespace netw::wire {

constexpr int CAPTURE_BYTES_CAP = 65535;

enum class CaptureDirection : uint8_t {
    OUT = 0,
    IN = 1,
};

struct CaptureRecord {
    uint64_t dir = 0;
    uint64_t peer = 0;
    uint64_t wall_ms = 0;
    uint64_t tick = 0;
    godot::PackedByteArray bytes;

    static constexpr auto wire = describe(
        field<&CaptureRecord::dir>("dir", bits(8)),
        field<&CaptureRecord::peer>("peer", varuint(5)),
        field<&CaptureRecord::wall_ms>("wall_ms", varuint(10)),
        field<&CaptureRecord::tick>("tick", varuint(5)),
        field<&CaptureRecord::bytes>("bytes", bytes_capped(CAPTURE_BYTES_CAP))
    );
};

godot::PackedByteArray capture_record_pack(const CaptureRecord &p_record);

bool capture_record_next(ReadStream &p_stream, CaptureRecord &r_record);

godot::Dictionary capture_spec_records();

class CaptureWriter {
    godot::Ref<godot::FileAccess> file;
    int64_t started_msec = 0;
    int64_t records = 0;
    int64_t oversized = 0;

public:
    static godot::String claim_armed_path();

    bool open(const godot::String &p_path, godot::Dictionary p_header);
    void note(
        CaptureDirection p_dir,
        int64_t p_peer,
        int64_t p_tick,
        const godot::PackedByteArray &p_datagram
    );
    void close();

    bool is_open() const {
        return file.is_valid();
    }

    int64_t record_count() const {
        return records;
    }

    int64_t oversized_count() const {
        return oversized;
    }
};

} // namespace netw::wire
