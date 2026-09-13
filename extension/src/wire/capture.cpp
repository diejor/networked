#include "netw/wire/capture.hpp"

#include "godot/json.hpp"
#include "godot/os.hpp"
#include "godot/time.hpp"
#include "netw/colors.hpp"
#include "netw/log.hpp"
#include "netw/profile.hpp"

using namespace godot;

namespace netw::wire {

namespace {

const char *CAPTURE_ENV = "NETW_CAPTURE";

bool armed_path_claimed = false;

} // namespace

PackedByteArray capture_record_pack(const CaptureRecord &p_record) {
    CaptureRecord staged = p_record;
    WriteStream stream;
    if (!CaptureRecord::wire.run(stream, staged) || !stream.ok()) {
        return PackedByteArray();
    }
    return stream.to_bytes();
}

bool capture_record_next(ReadStream &p_stream, CaptureRecord &r_record) {
    return CaptureRecord::wire.run(p_stream, r_record) && p_stream.ok();
}

Dictionary capture_spec_records() {
    Dictionary out;
    out["CaptureRecord"] = CaptureRecord::wire.spec_dump();
    return out;
}

String CaptureWriter::claim_armed_path() {
    if (armed_path_claimed) {
        return String();
    }
    const String path = OS::get_singleton()->get_environment(CAPTURE_ENV);
    if (path.is_empty()) {
        return path;
    }
    armed_path_claimed = true;
    return path;
}

bool CaptureWriter::open(const String &p_path, Dictionary p_header) {
    if (file.is_valid() || p_path.is_empty()) {
        return false;
    }
    file = FileAccess::open(p_path, FileAccess::WRITE);
    if (file.is_null()) {
        NETW_WARN_ONCE(
            sys::WIRE,
            "a capture was armed at %s and the path refused to open",
            String(p_path).utf8().get_data()
        );
        return false;
    }
    started_msec = Time::get_singleton()->get_ticks_msec();
    p_header[StringName("started")] = started_msec;
    file->store_line(JSON::stringify(p_header));
    return true;
}

void CaptureWriter::note(
    CaptureDirection p_dir,
    int64_t p_peer,
    int64_t p_tick,
    const PackedByteArray &p_datagram
) {
    NETW_ZONE_NC("wire capture note", colors::WIRE);
    if (file.is_null() || p_datagram.is_empty()) {
        return;
    }
    if (p_datagram.size() > CAPTURE_BYTES_CAP) {
        oversized += 1;
        return;
    }
    CaptureRecord record;
    record.dir = uint64_t(p_dir);
    record.peer = uint64_t(p_peer < 0 ? 0 : p_peer);
    record.wall_ms
        = uint64_t(Time::get_singleton()->get_ticks_msec() - started_msec);
    record.tick = p_tick < 0 ? uint64_t(0) : uint64_t(p_tick) + 1;
    record.bytes = p_datagram;

    const PackedByteArray packed = capture_record_pack(record);
    if (packed.is_empty()) {
        return;
    }
    file->store_buffer(packed);
    records += 1;
}

void CaptureWriter::close() {
    if (file.is_null()) {
        return;
    }
    file->flush();
    file = Ref<FileAccess>();
}

} // namespace netw::wire
