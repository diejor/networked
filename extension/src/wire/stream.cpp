#include "netw/wire/stream.hpp"

#include <cstring>

#include "netw/log.hpp"

using namespace godot;

namespace netw::wire {

namespace {

uint64_t low_mask(int count) {
    return count >= 64 ? ~uint64_t(0) : ((uint64_t(1) << count) - 1);
}

bool width_is_legal(int count) {
    return count >= 0 && count <= 64;
}

int pad_to_byte(int64_t bit_length) {
    return int((8 - (bit_length % 8)) % 8);
}

bool byte_count_is_legal(int count) {
    return count > 0 && count <= 10;
}

bool varint_byte_fits_uint64(uint64_t byte, int at) {
    return at < 9 || (byte & 0x7e) == 0;
}

uint64_t zigzag_encode(int64_t value) {
    return (uint64_t(value) << 1) ^ uint64_t(value >> 63);
}

int64_t zigzag_decode(uint64_t value) {
    return int64_t((value >> 1) ^ uint64_t(-int64_t(value & 1)));
}

} // namespace

bool WriteStream::bits(uint64_t &value, int count) {
    if (!healthy) {
        return false;
    }
    if (!width_is_legal(count)) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Bit width is out of range.");
    }
    if (count == 0) {
        return true;
    }
    if ((value & ~low_mask(count)) != 0) {
        healthy = false;
        NETW_ERR_V(
            false,
            sys::WIRE,
            "Value does not fit its declared bit width."
        );
    }
    int written = 0;
    while (written < count) {
        const int within = int(bits_written & 7);
        if (within == 0) {
            output.push_back(0);
        }
        const int available = 8 - within;
        const int wanted = count - written;
        const int taking = wanted < available ? wanted : available;
        const uint64_t chunk = (value >> written) & low_mask(taking);
        output[output.size() - 1] |= uint8_t(chunk << within);
        bits_written += taking;
        written += taking;
    }
    return true;
}

bool WriteStream::int_range(int64_t &value, int64_t low, int64_t high) {
    if (!healthy) {
        return false;
    }
    if (low > high) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Integer range bounds are inverted.");
    }
    if (value < low || value > high) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Value is outside its declared range.");
    }
    uint64_t raw = uint64_t(value - low);
    return bits(raw, bits_required(uint64_t(high - low)));
}

bool WriteStream::varuint(uint64_t &value, int max_bytes) {
    if (!healthy) {
        return false;
    }
    if (!byte_count_is_legal(max_bytes)) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Varint byte limit is out of range.");
    }
    if (!align_verify()) {
        return false;
    }
    uint64_t remaining = value;
    for (int at = 0; at < max_bytes; ++at) {
        uint64_t byte = remaining & 0x7f;
        remaining >>= 7;
        if (remaining != 0) {
            byte |= 0x80;
        }
        if (!bits(byte, 8)) {
            return false;
        }
        if (remaining == 0) {
            return true;
        }
    }
    healthy = false;
    NETW_ERR_V(false, sys::WIRE, "Value exceeds its varint byte limit.");
}

bool WriteStream::svarint(int64_t &value, int max_bytes) {
    uint64_t staged = zigzag_encode(value);
    return varuint(staged, max_bytes);
}

bool WriteStream::bool1(bool &value) {
    uint64_t raw = value ? 1 : 0;
    return bits(raw, 1);
}

bool WriteStream::bytes_capped(PackedByteArray &value, int cap) {
    if (!healthy) {
        return false;
    }
    if (cap < 0 || value.size() > cap) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Blob exceeds its declared cap.");
    }
    int64_t length = value.size();
    if (!int_range(length, 0, cap) || !align_verify()) {
        return false;
    }
    const uint8_t *source = value.ptr();
    for (int64_t index = 0; index < length; ++index) {
        output.push_back(source[index]);
    }
    bits_written += length * 8;
    return true;
}

bool WriteStream::raw_bytes(PackedByteArray &value, int64_t count) {
    if (!healthy) {
        return false;
    }
    if (count < 0 || int64_t(value.size()) != count) {
        healthy = false;
        NETW_ERR_V(
            false,
            sys::WIRE,
            "Opaque run does not match the length already written."
        );
    }
    if (!align_verify()) {
        return false;
    }
    const uint8_t *source = value.ptr();
    for (int64_t index = 0; index < count; ++index) {
        output.push_back(source[index]);
    }
    bits_written += count * 8;
    return true;
}

bool WriteStream::align_verify() {
    if (!healthy) {
        return false;
    }
    uint64_t pad = 0;
    return bits(pad, pad_to_byte(bit_length()));
}

int64_t WriteStream::bit_length() const {
    return bits_written;
}

PackedByteArray WriteStream::to_bytes() const {
    PackedByteArray out;
    out.resize(int64_t(output.size()));
    if (!output.is_empty()) {
        memcpy(out.ptrw(), output.ptr(), output.size());
    }
    return out;
}

ReadStream::ReadStream(const PackedByteArray &bytes) {
    seat(bytes);
}

void ReadStream::seat(const PackedByteArray &bytes) {
    source = bytes;
    input = source.ptr();
    input_size = source.size();
    bits_read = 0;
    healthy = true;
}

bool ReadStream::take(int count, uint64_t &out) {
    if (!healthy) {
        return false;
    }
    if (!width_is_legal(count)) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Bit width is out of range.");
    }
    if (count == 0) {
        out = 0;
        return true;
    }
    if (int64_t(count) > bits_remaining()) {
        healthy = false;
        return false;
    }
    uint64_t result = 0;
    int filled = 0;
    while (filled < count) {
        const int64_t bit = bits_read + filled;
        const int within = int(bit & 7);
        const int available = 8 - within;
        const int wanted = count - filled;
        const int taking = wanted < available ? wanted : available;
        const uint64_t chunk
            = (uint64_t(input[bit >> 3]) >> within) & low_mask(taking);
        result |= chunk << filled;
        filled += taking;
    }
    bits_read += count;
    out = result;
    return true;
}

bool ReadStream::bits(uint64_t &value, int count) {
    uint64_t staged = 0;
    if (!take(count, staged)) {
        return false;
    }
    value = staged;
    return true;
}

bool ReadStream::int_range(int64_t &value, int64_t low, int64_t high) {
    if (!healthy) {
        return false;
    }
    if (low > high) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Integer range bounds are inverted.");
    }
    uint64_t raw = 0;
    if (!take(bits_required(uint64_t(high - low)), raw)) {
        return false;
    }
    const int64_t staged = low + int64_t(raw);
    if (staged > high) {
        healthy = false;
        return false;
    }
    value = staged;
    return true;
}

bool ReadStream::varuint(uint64_t &value, int max_bytes) {
    if (!healthy) {
        return false;
    }
    if (!byte_count_is_legal(max_bytes)) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Varint byte limit is out of range.");
    }
    if (!align_verify()) {
        return false;
    }
    uint64_t staged = 0;
    for (int at = 0; at < max_bytes; ++at) {
        uint64_t byte = 0;
        if (!take(8, byte)) {
            return false;
        }
        if (!varint_byte_fits_uint64(byte, at)) {
            healthy = false;
            return false;
        }
        staged |= (byte & 0x7f) << (at * 7);
        if ((byte & 0x80) == 0) {
            if (at > 0 && (byte & 0x7f) == 0) {
                healthy = false;
                return false;
            }
            value = staged;
            return true;
        }
    }
    healthy = false;
    return false;
}

bool ReadStream::svarint(int64_t &value, int max_bytes) {
    uint64_t staged = 0;
    if (!varuint(staged, max_bytes)) {
        return false;
    }
    value = zigzag_decode(staged);
    return true;
}

bool ReadStream::bool1(bool &value) {
    uint64_t raw = 0;
    if (!take(1, raw)) {
        return false;
    }
    value = raw != 0;
    return true;
}

bool ReadStream::bytes_capped(PackedByteArray &value, int cap) {
    if (!healthy) {
        return false;
    }
    if (cap < 0) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Blob cap is negative.");
    }
    int64_t length = 0;
    if (!int_range(length, 0, cap) || !align_verify()) {
        return false;
    }
    if (length * 8 > bits_remaining()) {
        healthy = false;
        return false;
    }
    PackedByteArray staged;
    staged.resize(length);
    if (length > 0) {
        memcpy(staged.ptrw(), input + (bits_read / 8), size_t(length));
    }
    bits_read += length * 8;
    value = staged;
    return true;
}

bool ReadStream::raw_bytes(PackedByteArray &value, int64_t count) {
    if (!healthy) {
        return false;
    }
    if (count < 0) {
        healthy = false;
        NETW_ERR_V(false, sys::WIRE, "Opaque run length is negative.");
    }
    if (!align_verify()) {
        return false;
    }
    if (count * 8 > bits_remaining()) {
        healthy = false;
        return false;
    }
    PackedByteArray staged;
    staged.resize(count);
    if (count > 0) {
        memcpy(staged.ptrw(), input + (bits_read / 8), size_t(count));
    }
    bits_read += count * 8;
    value = staged;
    return true;
}

bool ReadStream::align_verify() {
    if (!healthy) {
        return false;
    }
    uint64_t pad = 0;
    if (!take(pad_to_byte(bits_read), pad)) {
        return false;
    }
    if (pad != 0) {
        healthy = false;
        return false;
    }
    return true;
}

bool MeasureStream::bits(uint64_t &value, int count) {
    (void)value;
    if (!width_is_legal(count)) {
        NETW_ERR_V(false, sys::WIRE, "Bit width is out of range.");
    }
    bits_described += count;
    return true;
}

bool MeasureStream::int_range(int64_t &value, int64_t low, int64_t high) {
    (void)value;
    if (low > high) {
        NETW_ERR_V(false, sys::WIRE, "Integer range bounds are inverted.");
    }
    bits_described += bits_required(uint64_t(high - low));
    return true;
}

bool MeasureStream::varuint(uint64_t &value, int max_bytes) {
    if (!byte_count_is_legal(max_bytes)) {
        NETW_ERR_V(false, sys::WIRE, "Varint byte limit is out of range.");
    }
    bits_described += pad_to_byte(bits_described);
    uint64_t remaining = value;
    for (int at = 0; at < max_bytes; ++at) {
        bits_described += 8;
        remaining >>= 7;
        if (remaining == 0) {
            return true;
        }
    }
    NETW_ERR_V(false, sys::WIRE, "Value exceeds its varint byte limit.");
}

bool MeasureStream::svarint(int64_t &value, int max_bytes) {
    uint64_t staged = zigzag_encode(value);
    return varuint(staged, max_bytes);
}

bool MeasureStream::bool1(bool &value) {
    (void)value;
    bits_described += 1;
    return true;
}

bool MeasureStream::bytes_capped(PackedByteArray &value, int cap) {
    if (cap < 0) {
        NETW_ERR_V(false, sys::WIRE, "Blob cap is negative.");
    }
    bits_described += bits_required(uint64_t(cap));
    bits_described += pad_to_byte(bits_described);
    bits_described += int64_t(value.size()) * 8;
    return true;
}

bool MeasureStream::raw_bytes(PackedByteArray &value, int64_t count) {
    (void)value;
    if (count < 0) {
        NETW_ERR_V(false, sys::WIRE, "Opaque run length is negative.");
    }
    bits_described += pad_to_byte(bits_described);
    bits_described += count * 8;
    return true;
}

bool MeasureStream::align_verify() {
    bits_described += pad_to_byte(bits_described);
    return true;
}

} // namespace netw::wire
