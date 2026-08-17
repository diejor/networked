#!/usr/bin/env python3
"""An independent WIRE v9 decoder, written from WIRE.md section 8 prose.

The conformance instrument's third vertex. The C++ descriptors emit a spec
artifact, `wire.spec.json`, and this reads that artifact and decodes bytes
against it without sharing a line with the code that produced them. Two
implementations agreeing is evidence; one implementation agreeing with itself
is not, which is what a round-trip test in the same process actually measures.

The rule the triangle rests on, and the only one worth restating here: a
hand-built frame is transcribed from WIRE.md's prose and never generated from
the spec artifact. A frame generated from the artifact would agree with the
artifact by construction, which is the one thing it must not do.

    wire_decode.py --spec wire.spec.json --record CommandHeader --hex 0a1b2c
    wire_decode.py --self-test                          # the decoder alone
    wire_decode.py --self-test --spec reports/native/wire.spec.json

The spec artifact is a JSON object of records, each an ordered field list, in
the shape `Description::spec_dump()` emits:

    {"records": {"AckHeader": [
        {"name": "epoch", "kind": "bits", "width": 8},
        {"name": "base",  "kind": "varuint", "max_bytes": 5},
        {"name": "count", "kind": "bits", "width": 8}]}}

Exits 1 on a decode failure, on residue left in a frame, or on a self-test
that did not come out as written.
"""
import argparse
import json
import sys

MAX_VARINT_BYTES = 10


class Poisoned(Exception):
    """A read the stream cannot answer. Never a wrong value, always a refusal."""


def bits_required(span):
    """Bits needed to hold `span`. Zero for a span of zero, which is a range of
    one value and occupies no bits."""
    width = 0
    while span > 0:
        width += 1
        span >>= 1
    return width


class ReadStream:
    """Little-endian bit reader. Within a byte the first bit read is bit 0."""

    def __init__(self, data):
        self.data = bytes(data)
        self.read = 0

    def remaining(self):
        return len(self.data) * 8 - self.read

    def bits(self, width):
        if width < 0 or width > 64:
            raise Poisoned("bit width %d is out of range" % width)
        if width == 0:
            return 0
        if self.remaining() < width:
            raise Poisoned(
                "wanted %d bits, %d remain" % (width, self.remaining())
            )
        value = 0
        taken = 0
        while taken < width:
            byte_at = (self.read + taken) >> 3
            within = (self.read + taken) & 7
            available = 8 - within
            wanted = width - taken
            taking = wanted if wanted < available else available
            chunk = (self.data[byte_at] >> within) & ((1 << taking) - 1)
            value |= chunk << taken
            taken += taking
        self.read += width
        return value

    def bool1(self):
        return self.bits(1) != 0

    def int_range(self, low, high):
        if low > high:
            raise Poisoned("range bounds %d..%d are inverted" % (low, high))
        return low + self.bits(bits_required(high - low))

    def align_verify(self):
        pad = (8 - (self.read % 8)) % 8
        if pad and self.bits(pad) != 0:
            raise Poisoned("alignment padding is not zero")

    def varuint(self, max_bytes=MAX_VARINT_BYTES):
        if max_bytes < 1 or max_bytes > MAX_VARINT_BYTES:
            raise Poisoned("varint byte limit %d is out of range" % max_bytes)
        self.align_verify()
        staged = 0
        for at in range(max_bytes):
            byte = self.bits(8)
            # The tenth group can only carry one more bit of a 64-bit value.
            if at >= 9 and (byte & 0x7E) != 0:
                raise Poisoned("varint does not fit 64 bits")
            staged |= (byte & 0x7F) << (at * 7)
            if not byte & 0x80:
                if at > 0 and (byte & 0x7F) == 0:
                    raise Poisoned("varint is not canonical")
                return staged
        raise Poisoned("varint exceeds its %d-byte limit" % max_bytes)

    def svarint(self, max_bytes=MAX_VARINT_BYTES):
        staged = self.varuint(max_bytes)
        return (staged >> 1) ^ -(staged & 1)

    def bytes_capped(self, cap):
        if cap < 0:
            raise Poisoned("blob cap %d is negative" % cap)
        length = self.int_range(0, cap)
        self.align_verify()
        if self.remaining() < length * 8:
            raise Poisoned(
                "blob wants %d bytes, %d remain"
                % (length, self.remaining() // 8)
            )
        at = self.read // 8
        self.read += length * 8
        return self.data[at:at + length]


def decode_field(stream, field):
    kind = field["kind"]
    if kind == "bits":
        return stream.bits(int(field["width"]))
    if kind == "bool1":
        return stream.bool1()
    if kind == "int_range":
        return stream.int_range(int(field["low"]), int(field["high"]))
    if kind == "varuint":
        return stream.varuint(int(field["max_bytes"]))
    if kind == "svarint":
        return stream.svarint(int(field["max_bytes"]))
    if kind == "bytes_capped":
        return stream.bytes_capped(int(field["cap"]))
    raise Poisoned("unknown field kind: %s" % kind)


def decode(fields, data, exhaustive=True):
    """Decode one record. Residue is a failure, because a frame that leaves
    bits unread is a frame whose reading nobody agreed on."""
    stream = ReadStream(data)
    out = {}
    for field in fields:
        out[field["name"]] = decode_field(stream, field)
    if exhaustive:
        stream.align_verify()
        if stream.remaining():
            raise Poisoned("%d bits of residue" % stream.remaining())
    return out


# Section 8.3 states both a bit layout and a packed byte sequence, so the two
# are held against each other here rather than either being trusted.
HAND_BUILT = {
    "name": "WIRE.md 8.3 reference frame",
    "fields": [
        {"name": "flags", "kind": "bool1"},
        {"name": "pos_x", "kind": "int_range", "low": 0, "high": 2047},
        {"name": "pos_y", "kind": "int_range", "low": 0, "high": 2047},
    ],
    "bytes": bytes([0x01, 0x08, 0x20]),
    "values": {"flags": True, "pos_x": 1024, "pos_y": 512},
}


# Transcribed from section 8.4, with the bytes worked out from section 8.1's
# primitives. Decoding these against the artifact is what asks whether the C++
# agrees with its own documentation, so generating one from the artifact
# instead would answer that question with itself.
AGAINST_SPEC = [
    {
        "record": "AckHeader",
        "bytes": bytes([0x03, 0xAC, 0x02, 0x02]),
        "values": {"epoch": 3, "base": 300, "count": 2},
        "why": "u8 epoch, varuint base, u8 count",
    },
    {
        "record": "CommandHeader",
        "bytes": bytes([0x01, 0x07, 0x05, 0x02]),
        "values": {"ack_of_acks": -1, "epoch": 7, "base": 5, "count": 2},
        "why": "svarint ack_of_acks zigzags -1 to 1, then u8, varuint, u8",
    },
]


def self_test(spec_path=None):
    """Proves the decoder red before it is trusted green.

    With `--spec`, also decodes the transcribed frames against the artifact the
    C++ descriptors emitted, which is the only part of this that can catch the
    two implementations disagreeing.
    """
    wrong = 0

    def check(name, got, want):
        nonlocal wrong
        ok = got == want
        wrong += 0 if ok else 1
        print("%s %-52s %s" % ("ok  " if ok else "FAIL", name, "" if ok else
                               "got %r want %r" % (got, want)))

    def refuses(name, thunk):
        nonlocal wrong
        try:
            thunk()
        except Poisoned:
            print("ok   %-52s" % name)
            return
        wrong += 1
        print("FAIL %-52s accepted what it must refuse" % name)

    got = decode(HAND_BUILT["fields"], HAND_BUILT["bytes"])
    check("the 8.3 reference frame decodes to its values", got,
          HAND_BUILT["values"])

    # Bit 0 set, bit 11 set (1024 is 2^10, at offset 1), bit 21 set (512 is
    # 2^9, at offset 12).
    layout = 0
    for bit in (0, 1 + 10, 12 + 9):
        layout |= 1 << bit
    check("the 8.3 bit layout packs to the 8.3 bytes",
          bytes([(layout >> (8 * i)) & 0xFF for i in range(3)]),
          HAND_BUILT["bytes"])

    check("a single-value range costs no bits",
          decode([{"name": "only", "kind": "int_range", "low": 7, "high": 7}],
                 b""),
          {"only": 7})

    check("a two-group varuint reads its value",
          decode([{"name": "n", "kind": "varuint", "max_bytes": 5}],
                 bytes([0xAC, 0x02]))["n"], 300)
    refuses("a varuint with a redundant final group",
            lambda: decode([{"name": "n", "kind": "varuint", "max_bytes": 5}],
                           bytes([0xAC, 0x82, 0x00])))
    refuses("a varuint past its byte limit",
            lambda: decode([{"name": "n", "kind": "varuint", "max_bytes": 1}],
                           bytes([0x80, 0x01])))

    for value, encoded in ((0, 0x00), (-1, 0x01), (1, 0x02), (-2, 0x03)):
        check("svarint reads %d" % value,
              decode([{"name": "n", "kind": "svarint", "max_bytes": 5}],
                     bytes([encoded]))["n"], value)

    check("bytes_capped spends a range on its length",
          decode([{"name": "b", "kind": "bytes_capped", "cap": 3}],
                 bytes([0x02, 0xAA, 0xBB]))["b"], bytes([0xAA, 0xBB]))

    refuses("a frame with residue",
            lambda: decode(HAND_BUILT["fields"],
                           HAND_BUILT["bytes"] + b"\xff"))
    refuses("a frame that ends early",
            lambda: decode(HAND_BUILT["fields"], HAND_BUILT["bytes"][:2]))
    # One bool and a 2-bit length leave five bits of pad before the payload.
    # 0x83 sets one of them, which is a frame the writer could not have
    # produced and a reader must not accept.
    refuses("alignment padding that is not zero",
            lambda: decode([{"name": "one", "kind": "bool1"},
                            {"name": "b", "kind": "bytes_capped", "cap": 3}],
                           bytes([0x83, 0xAA])))

    if spec_path is None:
        print("EXCLUDED no --spec given, so nothing was held to the C++ "
              "descriptors -- this run proves only the decoder")
    else:
        with open(spec_path) as handle:
            records = json.load(handle).get("records", {})
        for frame in AGAINST_SPEC:
            name = frame["record"]
            if name not in records:
                wrong += 1
                print("FAIL %-52s the spec holds no such record" % name)
                continue
            try:
                got = decode(records[name], frame["bytes"])
            except Poisoned as refused:
                wrong += 1
                print("FAIL %-52s REFUSED %s" % (name, refused))
                continue
            check("%s decodes as its prose reads (%s)"
                  % (name, frame["why"]), got, frame["values"])
            refuses("%s refuses a trailing byte" % name,
                    lambda f=frame, r=records[name]:
                    decode(r, f["bytes"] + b"\xff"))

    print("SELFTEST %d wrong" % wrong)
    return 1 if wrong else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec")
    parser.add_argument("--record")
    parser.add_argument("--hex")
    parser.add_argument("--self-test", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.spec)
    if not (args.spec and args.record and args.hex):
        parser.error("--spec, --record and --hex are required without --self-test")

    with open(args.spec) as handle:
        spec = json.load(handle)
    records = spec.get("records", {})
    if args.record not in records:
        sys.exit("spec holds no record named %s" % args.record)
    try:
        decoded = decode(records[args.record], bytes.fromhex(args.hex))
    except Poisoned as refused:
        sys.exit("REFUSED %s" % refused)
    for name, value in decoded.items():
        print("%s = %r" % (name, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
