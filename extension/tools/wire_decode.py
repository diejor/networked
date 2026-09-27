#!/usr/bin/env python3
"""An independent WIRE decoder, written from WIRE.md and from nothing else.

wire_decode.py --spec wire.spec.json --record CommandHeader --hex 0a1b2c
wire_decode.py --self-test                          # the decoder alone
wire_decode.py --self-test --spec reports/native/wire.spec.json
wire_decode.py --capture run.netwcap --verbose      # WIRE.md 21
"""

import argparse
import json
import sys

MAX_VARINT_BYTES = 10


class Poisoned(Exception):
    """A read the stream cannot answer. Never a wrong value, always a refusal."""


def bits_required(span):
    """Bits needed to hold `span`. Zero for a span of zero."""
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
            raise Poisoned("wanted %d bits, %d remain" % (width, self.remaining()))
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
            raise Poisoned("blob wants %d bytes, %d remain" % (length, self.remaining() // 8))
        at = self.read // 8
        self.read += length * 8
        return self.data[at : at + length]


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
    if kind == "string":
        return stream.bytes_capped(int(field["cap"])).decode("utf-8")
    raise Poisoned("unknown field kind: %s" % kind)


def decode_into(fields, stream):
    """Decode one record from a live stream, leaving whatever follows it."""
    out = {}
    for field in fields:
        out[field["name"]] = decode_field(stream, field)
    return out


def decode(fields, data, exhaustive=True):
    """Decode one record. Residue is a failure."""
    stream = ReadStream(data)
    out = decode_into(fields, stream)
    if exhaustive:
        stream.align_verify()
        if stream.remaining():
            raise Poisoned("%d bits of residue" % stream.remaining())
    return out


HAND_BUILT = {
    "name": "the v9 reference frame",
    "fields": [
        {"name": "flags", "kind": "bool1"},
        {"name": "pos_x", "kind": "int_range", "low": 0, "high": 2047},
        {"name": "pos_y", "kind": "int_range", "low": 0, "high": 2047},
    ],
    "bytes": bytes([0x01, 0x08, 0x20]),
    "values": {"flags": True, "pos_x": 1024, "pos_y": 512},
}


MASKED_ROW = {
    "name": "the v10 masked row over a three column I16 plan",
    "fields": [
        {"name": "life", "kind": "bits", "width": 4},
        {"name": "mask", "kind": "bits", "width": 3},
        {"name": "has_ack", "kind": "bool1"},
        {"name": "has_tick", "kind": "bool1"},
        {"name": "ack_delta", "kind": "svarint", "max_bytes": 5},
        {"name": "tick_delta", "kind": "svarint", "max_bytes": 3},
        {"name": "x", "kind": "bits", "width": 16},
        {"name": "y", "kind": "bits", "width": 16},
        {"name": "z", "kind": "bits", "width": 16},
    ],
    "bytes": bytes([0xF5, 0x01, 0x05, 0x02, 0x0B, 0x00, 0x16, 0x00, 0x21, 0x00]),
    "values": {
        "life": 5,
        "mask": 7,
        "has_ack": True,
        "has_tick": True,
        "ack_delta": -3,
        "tick_delta": 1,
        "x": 11,
        "y": 22,
        "z": 33,
    },
}


LADDER_ROW = {
    "name": "the v10 laddered row over a two column I16 plan",
    "fields": [
        {"name": "life", "kind": "bits", "width": 4},
        {"name": "mask", "kind": "bits", "width": 2},
        {"name": "has_ack", "kind": "bool1"},
        {"name": "has_tick", "kind": "bool1"},
        {"name": "has_base", "kind": "bool1"},
        {"name": "base_low", "kind": "bits", "width": 8},
        {"name": "sel_x", "kind": "bits", "width": 2},
        {"name": "zz_x", "kind": "bits", "width": 4},
        {"name": "sel_y", "kind": "bits", "width": 2},
        {"name": "zz_y", "kind": "bits", "width": 4},
    ],
    "bytes": bytes([0x35, 0x55, 0xB2, 0x12]),
    "values": {
        "life": 5,
        "mask": 3,
        "has_ack": False,
        "has_tick": False,
        "has_base": True,
        "base_low": 42,
        "sel_x": 1,
        "zz_x": 6,
        "sel_y": 1,
        "zz_y": 9,
    },
}


SNAPSHOT_HEAD = [
    {"name": "token", "kind": "varuint", "max_bytes": 10},
    {"name": "revision", "kind": "varuint", "max_bytes": 10},
    {"name": "distance", "kind": "bits", "width": 6},
    {"name": "has_ack", "kind": "bool1"},
    {"name": "has_tick", "kind": "bool1"},
]


SNAPSHOT_FULL = {
    "name": "WIRE.md 8 full row over a three column I16 plan",
    "fields": SNAPSHOT_HEAD
    + [
        {"name": "x", "kind": "bits", "width": 16},
        {"name": "y", "kind": "bits", "width": 16},
        {"name": "z", "kind": "bits", "width": 16},
    ],
    "bytes": bytes([0x07, 0x01, 0x00, 0x0B, 0x00, 0x16, 0x00, 0x21, 0x00]),
    "values": {
        "token": 7,
        "revision": 1,
        "distance": 0,
        "has_ack": False,
        "has_tick": False,
        "x": 11,
        "y": 22,
        "z": 33,
    },
}


SNAPSHOT_DELTA = {
    "name": "WIRE.md 8 delta at distance one, mask 0b101",
    "fields": SNAPSHOT_HEAD
    + [
        {"name": "mask", "kind": "bits", "width": 3},
        {"name": "tick_delta", "kind": "svarint", "max_bytes": 3},
        {"name": "x", "kind": "bits", "width": 16},
        {"name": "z", "kind": "bits", "width": 16},
    ],
    "bytes": bytes([0x07, 0x04, 0x81, 0x05, 0x02, 0x0B, 0x00, 0x21, 0x00]),
    "values": {
        "token": 7,
        "revision": 4,
        "distance": 1,
        "has_ack": False,
        "has_tick": True,
        "mask": 5,
        "tick_delta": 1,
        "x": 11,
        "z": 33,
    },
}


SNAPSHOT_ZERO_MASK = {
    "name": "WIRE.md 8 zero mask delta at distance two",
    "fields": SNAPSHOT_HEAD + [{"name": "mask", "kind": "bits", "width": 3}],
    "bytes": bytes([0x07, 0x03, 0x02, 0x00]),
    "values": {
        "token": 7,
        "revision": 3,
        "distance": 2,
        "has_ack": False,
        "has_tick": False,
        "mask": 0,
    },
}


SNAPSHOT_WINDOW = {
    "name": "WIRE.md 9 window of two samples over one I16 column",
    "fields": [
        {"name": "token", "kind": "varuint", "max_bytes": 10},
        {"name": "revision", "kind": "varuint", "max_bytes": 10},
        {"name": "count", "kind": "int_range", "low": 1, "high": 255},
        {"name": "has_ack", "kind": "bool1"},
        {"name": "tick_delta", "kind": "svarint", "max_bytes": 3},
        {"name": "age_older", "kind": "varuint", "max_bytes": 3},
        {"name": "older", "kind": "bits", "width": 16},
        {"name": "age_newer", "kind": "varuint", "max_bytes": 3},
        {"name": "newer", "kind": "bits", "width": 16},
    ],
    "bytes": bytes([0x07, 0x02, 0x01, 0x00, 0x00, 0x01, 0x0B, 0x00, 0x00, 0x16, 0x00]),
    "values": {
        "token": 7,
        "revision": 2,
        "count": 2,
        "has_ack": False,
        "tick_delta": 0,
        "age_older": 1,
        "older": 11,
        "age_newer": 0,
        "newer": 22,
    },
}


CONTROL_OPEN = {
    "name": "WIRE.md 10 OPEN for route 300 ordinal 2",
    "fields": [
        {"name": "tag", "kind": "bits", "width": 8},
        {"name": "request", "kind": "varuint", "max_bytes": 10},
        {"name": "route", "kind": "varuint", "max_bytes": 5},
        {"name": "ordinal", "kind": "bits", "width": 8},
        {"name": "family", "kind": "bits", "width": 8},
        {"name": "epoch", "kind": "varuint", "max_bytes": 3},
        {"name": "tenure", "kind": "varuint", "max_bytes": 5},
        {"name": "anchor", "kind": "varuint", "max_bytes": 5},
        {"name": "anchor_author", "kind": "varuint", "max_bytes": 5},
        {"name": "schema", "kind": "bits", "width": 32},
    ],
    "bytes": bytes(
        [
            0x00,
            0x01,
            0xAC,
            0x02,
            0x02,
            0x00,
            0x05,
            0x03,
            0x04,
            0x06,
            0xEF,
            0xBE,
            0xAD,
            0xDE,
        ]
    ),
    "values": {
        "tag": 0,
        "request": 1,
        "route": 300,
        "ordinal": 2,
        "family": 0,
        "epoch": 5,
        "tenure": 3,
        "anchor": 4,
        "anchor_author": 6,
        "schema": 0xDEADBEEF,
    },
}


CONTROL_READY = {
    "name": "WIRE.md 10 READY for request 300",
    "fields": [
        {"name": "tag", "kind": "bits", "width": 8},
        {"name": "request", "kind": "varuint", "max_bytes": 10},
        {"name": "token", "kind": "varuint", "max_bytes": 10},
    ],
    "bytes": bytes([0x01, 0xAC, 0x02, 0x07]),
    "values": {"tag": 1, "request": 300, "token": 7},
}


CONTROL_ACCEPT = {
    "name": "WIRE.md 10 ACCEPT of two entries",
    "fields": [
        {"name": "tag", "kind": "bits", "width": 8},
        {"name": "count", "kind": "int_range", "low": 1, "high": 32},
        {"name": "token_a", "kind": "varuint", "max_bytes": 10},
        {"name": "revision_a", "kind": "varuint", "max_bytes": 10},
        {"name": "token_b", "kind": "varuint", "max_bytes": 10},
        {"name": "revision_b", "kind": "varuint", "max_bytes": 10},
    ],
    "bytes": bytes([0x02, 0x01, 0x07, 0x03, 0x09, 0x01]),
    "values": {
        "tag": 2,
        "count": 2,
        "token_a": 7,
        "revision_a": 3,
        "token_b": 9,
        "revision_b": 1,
    },
}


V11_HAND_BUILT = [
    SNAPSHOT_FULL,
    SNAPSHOT_DELTA,
    SNAPSHOT_ZERO_MASK,
    SNAPSHOT_WINDOW,
    CONTROL_OPEN,
    CONTROL_READY,
    CONTROL_ACCEPT,
]


def unzigzag(coded):
    return (coded >> 1) ^ -(coded & 1)


AGAINST_SPEC = [
    {
        "record": "DatagramReliable",
        "bytes": bytes([0x57, 0x01]),
        "values": {"magic": 0x57, "base_tick": 1},
        "why": "WIRE.md 2, the reliable magic then tick zero as base_tick one",
    },
    {
        "record": "DatagramUnreliable",
        "bytes": bytes([0x77, 0x09, 0x00, 0x00]),
        "values": {"magic": 0x77, "seq": 9, "base_tick": 0},
        "why": "WIRE.md 2, seq 9 little endian, then an unconfigured clock as 0",
    },
    {
        "record": "DatagramAcked",
        "bytes": bytes([0x97, 0x09, 0x00, 0x0C, 0x00, 0xA5, 0xA5, 0xA5, 0xA5, 0xAD, 0x02]),
        "values": {"magic": 0x97, "seq": 9, "ack": 12, "history": 0xA5A5A5A5, "base_tick": 301},
        "why": "WIRE.md 2, the echo of seq 12 with its 32 bit history, at tick 300",
    },
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
    {
        "record": "FrameHeader",
        "bytes": bytes([0xAC, 0x02, 0x03, 0x13, 0x00]),
        "values": {"route": 300, "comp": 3, "channel": 19, "length": 0},
        "why": "WIRE.md 3, route varuint 5, comp u8, channel u8, len varuint 3",
    },
    {
        "record": "SpawnVerbHead",
        "bytes": bytes([0xAC, 0x02, 0x02]),
        "values": {"route": 300, "epoch": 2},
        "why": "WIRE.md 11, route varuint 5 then epoch varuint 3",
    },
    {
        "record": "ClockRate",
        "bytes": bytes([0x3C]),
        "values": {"tickrate": 60},
        "why": "WIRE.md 12, a sixty hertz rate in one varuint group",
    },
    {
        "record": "ClockPing",
        "bytes": bytes([0x78, 0x56, 0x34, 0x12]),
        "values": {"origin": 0x12345678},
        "why": "WIRE.md 12, the low 32 bits of the pinger's own wall clock",
    },
    {
        "record": "ClockPong",
        "bytes": bytes([0x78, 0x56, 0x34, 0x12, 0x2A, 0x00, 0x00, 0x00, 0x80]),
        "values": {"origin": 0x12345678, "tick": 42, "phase": 128},
        "why": "WIRE.md 12, the ping's origin echoed, then tick 42 at half phase",
    },
    {
        "record": "ActionRequest",
        "bytes": bytes([0x04, 0x00]) + b"fire" + bytes([0x12, 0x00, 0x00, 0x01, 0x00]) + b"k" + bytes([0x01]),
        "values": {"method": "fire", "view_tick": 9, "data": b"", "key": "k", "timing": 1},
        "why": "WIRE.md 18, a remote action request at view tick 9",
    },
    {
        "record": "SessionReason",
        "bytes": bytes([0x04, 0x00]) + b"rude",
        "values": {"reason": "rude"},
        "why": "WIRE.md 16, a reason a game wrote and a player reads",
    },
    {
        "record": "KickRequest",
        "bytes": bytes([0x12, 0x03, 0x00]) + b"afk",
        "values": {"peer": 9, "reason": "afk"},
        "why": "WIRE.md 16, peer 9 zigzagged, then the reason",
    },
    {
        "record": "SceneRequest",
        "bytes": bytes([0x09, 0x0C, 0x00]) + b"res://a.tscn" + bytes([0x02, 0x0E, 0x02]),
        "values": {
            "request_id": 9,
            "path": "res://a.tscn",
            "scope": 1,
            "source_route": 7,
            "source_epoch": 1,
        },
        "why": "WIRE.md 16, request 9 asks for a scene at scope 1 from scene 7 " "at epoch 1",
    },
    {
        "record": "SceneResult",
        "bytes": bytes([0x09, 0x02]),
        "values": {"request_id": 9, "code": 1},
        "why": "WIRE.md 16, request 9 answered with a signed engine Error",
    },
    {
        "record": "SceneReleased",
        "bytes": bytes([0x07]),
        "values": {"route": 7},
        "why": "WIRE.md 16, the route of the scene the server released",
    },
    {
        "record": "SceneViewersHead",
        "bytes": bytes([0x07, 0x01, 0x02, 0x03, 0x02]),
        "values": {"route": 7, "epoch": 1, "generation": 2, "revision": 3, "count": 2},
        "why": "WIRE.md 16, revision 3 of scene 7's roster, two viewers following",
    },
    {
        "record": "SceneViewerRow",
        "bytes": bytes([0x09]),
        "values": {"player_id": 9},
        "why": "WIRE.md 16, one viewer named by the membership the accept issued",
    },
    {
        "record": "AcceptFrame",
        "bytes": bytes([0x0E, 0x03, 0x00]) + b"ana" + bytes([0x01]),
        "values": {"peer_id": 7, "username": "ana", "player_id": 1},
        "why": "WIRE.md 16, peer 7 accepted as membership 1, carrying no "
        "join argument because the accept reaches every peer",
    },
    {
        "record": "JoinFrame",
        "bytes": (bytes([0x03, 0x00]) + b"ana" + bytes([0x00, 0x00]) + bytes(8) + bytes([0x08]) + bytes(24)),
        "values": {
            "username": "ana",
            "args": b"",
            "schema_hash": 0,
            "peer_id": 4,
            "app_tag": 0,
            "wire_identity": 0,
            "schema_identity": 0,
        },
        "why": "WIRE.md 16, a join whose three identity halves are all zero",
    },
    {
        "record": "TableFrameHead",
        "bytes": bytes([0x03, 0xEF, 0xBE, 0xAC, 0x02, 0x01, 0x02]),
        "values": {"table_id": 3, "schema_hash": 0xBEEF, "tick": 300, "flags": 1, "rows": 2},
        "why": "WIRE.md 17, a snapshot of two rows of table 3 at tick 300",
    },
    {
        "record": "AwarenessEdge",
        "bytes": bytes([0x01, 0x07, 0x04, 0x00]) + b"zone" + bytes([0x03, 0x01]),
        "values": {
            "observer_scoped": True,
            "route": 7,
            "layer_id": "zone",
            "observer": 3,
            "entered": True,
        },
        "why": "WIRE.md 15, peer 3 gains awareness of route 7 inside zone",
    },
    {
        "record": "ControlRequest",
        "bytes": bytes([0x01, 0x03, 0xAC, 0x02, 0x00, 0x00, 0x02, 0x00]),
        "values": {
            "op": 1,
            "observed_revision": 3,
            "issued_tick": 300,
            "source_route": 0,
            "successor": 0,
            "kind": 0,
            "hold": 1,
            "final_state": b"",
        },
        "why": "WIRE.md 14, op 1 asking for a yieldable hold at tick 300",
    },
    {
        "record": "ControlApply",
        "bytes": bytes([0x02, 0x03, 0x00, 0x05, 0x00]),
        "values": {
            "controller": 2,
            "revision": 3,
            "op": 0,
            "tenure_changed": True,
            "hold": 2,
            "outcome": 0,
            "final_state": b"",
        },
        "why": "WIRE.md 14, revision 3 opens peer 2's tenure with an exclusive hold",
    },
    {
        "record": "DenyKey",
        "bytes": bytes([0x04, 0x00]) + b"shot",
        "values": {"key": "shot"},
        "why": "WIRE.md 13, a ten bit length, its alignment pad, then the utf8",
    },
    {
        "record": "RouteLease",
        "bytes": bytes([0x11, 0x10]),
        "values": {"base": 17, "count": 16},
        "why": "WIRE.md 16, the sixteen routes from 17 a joiner mints from",
    },
    {
        "record": "RowControlOpen",
        "bytes": CONTROL_OPEN["bytes"],
        "values": CONTROL_OPEN["values"],
        "why": "WIRE.md 10, an OPEN at epoch 5, tenure 3, anchor 4 by peer 6",
    },
    {
        "record": "RowControlReady",
        "bytes": CONTROL_READY["bytes"],
        "values": CONTROL_READY["values"],
        "why": "WIRE.md 10, the receiver answering request 300 with token 7",
    },
    {
        "record": "RowControlReset",
        "bytes": bytes([0x03, 0xAC, 0x02, 0x07]),
        "values": {"tag": 3, "request": 300, "token": 7},
        "why": "WIRE.md 10, a RESET naming both the request and the token",
    },
    {
        "record": "RowControlClose",
        "bytes": bytes([0x04, 0x07]),
        "values": {"tag": 4, "token": 7},
        "why": "WIRE.md 10, a writer closing token 7",
    },
    {
        "record": "RowControlAcceptHead",
        "bytes": bytes([0x02, 0x01]),
        "values": {"tag": 2, "count": 2},
        "why": "WIRE.md 10, two entries written as 1 in the range 1..32",
    },
    {
        "record": "RowControlAcceptEntry",
        "bytes": bytes([0x07, 0x03]),
        "values": {"token": 7, "revision": 3},
        "why": "WIRE.md 10, token 7 accepted at revision 3",
    },
    {
        "record": "CaptureRecord",
        "bytes": bytes([0x01, 0x02, 0xAC, 0x02, 0x2B, 0x02, 0x00, 0x57, 0x01]),
        "values": {
            "dir": 1,
            "peer": 2,
            "wall_ms": 300,
            "tick": 43,
            "bytes": bytes([0x57, 0x01]),
        },
        "why": "WIRE.md 21, a datagram peer 2 read 300ms in, at session tick 42",
    },
]


# A capture names its own format, and a format owns its three datagram magics.
# Every other byte of the carrier is described by the header's own record sheet,
# so this table is the one thing a format bump moves that the sheet cannot say.
# A capture whose format is absent here is refused rather than guessed at.
# Reading a magic against the wrong table names a record of the wrong shape and
# then decodes plausible garbage out of every frame behind it.
CAPTURE_MAGICS_BY_FORMAT = {
    10: {
        0x57: "DatagramReliable",
        0x77: "DatagramUnreliable",
        0x97: "DatagramAcked",
    },
    11: {
        0x58: "DatagramReliable",
        0x78: "DatagramUnreliable",
        0x98: "DatagramAcked",
    },
}


def datagram_magics(fmt):
    """The magic table one capture format speaks, or a refusal."""
    try:
        key = int(fmt)
    except (TypeError, ValueError):
        raise Poisoned("the capture names no format version")
    if key not in CAPTURE_MAGICS_BY_FORMAT:
        raise Poisoned(
            "format %s is no capture format this decoder speaks, and it speaks %s"
            % (key, ", ".join(str(k) for k in sorted(CAPTURE_MAGICS_BY_FORMAT)))
        )
    return CAPTURE_MAGICS_BY_FORMAT[key]


def read_capture(path):
    """Split a .netwcap into its header and record list. Reject trailing data."""
    with open(path, "rb") as handle:
        raw = handle.read()
    split = raw.find(b"\n")
    if split < 0:
        raise Poisoned("the capture carries no header line")
    header = json.loads(raw[:split].decode("utf-8"))
    records = header.get("records", {})
    if "CaptureRecord" not in records:
        raise Poisoned("the capture header describes no CaptureRecord")
    stream = ReadStream(raw[split + 1 :])
    rows = []
    while stream.remaining() >= 8:
        rows.append(decode_into(records["CaptureRecord"], stream))
    if stream.remaining():
        raise Poisoned("%d bits of residue past the last record" % stream.remaining())
    return header, rows


def walk_datagram(records, channels, payload, magics):
    """Every frame in one captured datagram, or the refusal that stopped it."""
    stream = ReadStream(payload)
    magic = payload[0] if payload else -1
    if magic not in magics:
        raise Poisoned("0x%02x is no datagram magic this format speaks" % magic)
    head = decode_into(records[magics[magic]], stream)
    stream.align_verify()
    frames = []
    while stream.remaining() >= 8:
        frame = decode_into(records["FrameHeader"], stream)
        length = frame["length"]
        if stream.remaining() < length * 8:
            raise Poisoned(
                "route %d channel %d claims %d bytes, %d remain"
                % (frame["route"], frame["channel"], length, stream.remaining() // 8)
            )
        at = stream.read // 8
        stream.read += length * 8
        frame["payload"] = stream.data[at : at + length]
        frame["channel_name"] = channels.get(frame["channel"], "?")
        frames.append(frame)
    return head, frames


def capture(path, verbose):
    """Decode a capture end to end, and answer non-zero on any refusal."""
    header, rows = read_capture(path)
    records = header.get("records", {})
    channels = {int(row["id"]): row["name"] for row in header.get("channels", [])}
    magics = datagram_magics(header.get("format"))
    print(
        "CAPTURE %s format=%s identity=%s peer=%s records=%d schemas=%d"
        % (
            path,
            header.get("format"),
            header.get("identity"),
            header.get("peer"),
            len(rows),
            len(header.get("schemas", [])),
        )
    )

    errors = 0
    frames = 0
    per_channel = {0: {}, 1: {}}
    per_direction = {0: 0, 1: 0}
    datagram_bytes = {0: 0, 1: 0}
    for index, row in enumerate(rows):
        direction = row["dir"]
        per_direction[direction] = per_direction.get(direction, 0) + 1
        datagram_bytes[direction] = datagram_bytes.get(direction, 0) + len(row["bytes"])
        try:
            head, walked = walk_datagram(records, channels, row["bytes"], magics)
        except Poisoned as stopped:
            errors += 1
            print("ERROR record %d: %s" % (index, stopped))
            continue
        frames += len(walked)
        lane = per_channel.setdefault(direction, {})
        for frame in walked:
            key = "%d %s" % (frame["channel"], frame["channel_name"])
            lane[key] = lane.get(key, 0) + len(frame["payload"])
        if verbose:
            print(
                "  %s peer=%d wall=%dms tick=%s bytes=%d frames=%d"
                % (
                    "out" if row["dir"] == 0 else "in ",
                    row["peer"],
                    row["wall_ms"],
                    row["tick"] - 1 if row["tick"] else "none",
                    len(row["bytes"]),
                    len(walked),
                )
            )
            for frame in walked:
                print(
                    "      route=%d comp=%d channel=%d %s payload=%d"
                    % (
                        frame["route"],
                        frame["comp"],
                        frame["channel"],
                        frame["channel_name"],
                        len(frame["payload"]),
                    )
                )

    for direction, label in ((0, "OUT"), (1, "IN ")):
        lane = per_channel.get(direction, {})
        payload = sum(lane.values())
        print(
            "  %s datagrams=%d datagram_bytes=%d frame_payload=%d residual=%d"
            % (
                label,
                per_direction.get(direction, 0),
                datagram_bytes.get(direction, 0),
                payload,
                datagram_bytes.get(direction, 0) - payload,
            )
        )
        for key in sorted(lane, key=lambda k: -lane[k]):
            print("      %-28s %9d" % (key, lane[key]))
    print("CAPTURE %d records %d frames %d errors" % (len(rows), frames, errors))
    return 1 if errors else 0


def self_test(spec_path=None):
    """Proves the decoder red before it is trusted green."""
    wrong = 0

    def check(name, got, want):
        nonlocal wrong
        ok = got == want
        wrong += 0 if ok else 1
        print("%s %-52s %s" % ("ok  " if ok else "FAIL", name, "" if ok else "got %r want %r" % (got, want)))

    def rejects(name, thunk):
        nonlocal wrong
        try:
            thunk()
        except Poisoned:
            print("ok   %-52s" % name)
            return
        wrong += 1
        print("FAIL %-52s accepted invalid input" % name)

    got = decode(HAND_BUILT["fields"], HAND_BUILT["bytes"])
    check("the 8.3 reference frame decodes to its values", got, HAND_BUILT["values"])

    layout = 0
    for bit in (0, 1 + 10, 12 + 9):
        layout |= 1 << bit
    check(
        "the 8.3 bit layout packs to the 8.3 bytes",
        bytes([(layout >> (8 * i)) & 0xFF for i in range(3)]),
        HAND_BUILT["bytes"],
    )

    check(
        "the 9.4 masked row decodes to its values",
        decode(MASKED_ROW["fields"], MASKED_ROW["bytes"]),
        MASKED_ROW["values"],
    )
    rejects(
        "the 9.4 masked row rejects a trailing byte",
        lambda: decode(MASKED_ROW["fields"], MASKED_ROW["bytes"] + b"\xff"),
    )

    laddered = decode(LADDER_ROW["fields"], LADDER_ROW["bytes"])
    check("the 9.5 laddered row decodes to its values", laddered, LADDER_ROW["values"])
    rejects(
        "the 9.5 laddered row rejects a trailing byte",
        lambda: decode(LADDER_ROW["fields"], LADDER_ROW["bytes"] + b"\xff"),
    )
    check(
        "the 9.5 laddered row steps 1000 and 2000 to the prose's codes",
        (1000 + unzigzag(laddered["zz_x"]), 2000 + unzigzag(laddered["zz_y"])),
        (1003, 1995),
    )

    for frame in V11_HAND_BUILT:
        check(
            "%s decodes to its values" % frame["name"],
            decode(frame["fields"], frame["bytes"]),
            frame["values"],
        )
        rejects(
            "%s rejects a trailing byte" % frame["name"],
            lambda f=frame: decode(f["fields"], f["bytes"] + b"\xff"),
        )
        rejects(
            "%s rejects a truncation" % frame["name"],
            lambda f=frame: decode(f["fields"], f["bytes"][:-1]),
        )

    check(
        "the 10.2 delta names revision 4 minus distance 1 as its baseline",
        SNAPSHOT_DELTA["values"]["revision"] - SNAPSHOT_DELTA["values"]["distance"],
        3,
    )
    check(
        "the 10.2 full row writes no mask, so it is six bytes of columns",
        len(SNAPSHOT_FULL["bytes"]) - 3,
        6,
    )

    check(
        "a single-value range costs no bits",
        decode([{"name": "only", "kind": "int_range", "low": 7, "high": 7}], b""),
        {"only": 7},
    )

    check(
        "a two-group varuint reads its value",
        decode([{"name": "n", "kind": "varuint", "max_bytes": 5}], bytes([0xAC, 0x02]))["n"],
        300,
    )
    rejects(
        "a varuint with a redundant final group",
        lambda: decode([{"name": "n", "kind": "varuint", "max_bytes": 5}], bytes([0xAC, 0x82, 0x00])),
    )
    rejects(
        "a varuint past its byte limit",
        lambda: decode([{"name": "n", "kind": "varuint", "max_bytes": 1}], bytes([0x80, 0x01])),
    )

    for value, encoded in ((0, 0x00), (-1, 0x01), (1, 0x02), (-2, 0x03)):
        check(
            "svarint reads %d" % value,
            decode([{"name": "n", "kind": "svarint", "max_bytes": 5}], bytes([encoded]))["n"],
            value,
        )

    check(
        "bytes_capped spends a range on its length",
        decode([{"name": "b", "kind": "bytes_capped", "cap": 3}], bytes([0x02, 0xAA, 0xBB]))["b"],
        bytes([0xAA, 0xBB]),
    )

    rejects("a frame with residue", lambda: decode(HAND_BUILT["fields"], HAND_BUILT["bytes"] + b"\xff"))
    rejects("a frame that ends early", lambda: decode(HAND_BUILT["fields"], HAND_BUILT["bytes"][:2]))
    rejects(
        "alignment padding that is not zero",
        lambda: decode(
            [{"name": "one", "kind": "bool1"}, {"name": "b", "kind": "bytes_capped", "cap": 3}], bytes([0x83, 0xAA])
        ),
    )

    if spec_path is None:
        print(
            "EXCLUDED no --spec given, so nothing was held to the C++ "
            "descriptors -- this run proves only the decoder"
        )
    else:
        with open(spec_path) as handle:
            spec = json.load(handle)
        records = spec.get("records", {})

        # The spec names the format this build speaks, and a capture it writes
        # opens with one of that format's three magics. Every fixture below
        # decodes a record the caller already named, so none of them reads a
        # magic at all, and a format bump that moves the magics leaves them all
        # green while no live capture decodes. This is the case that reads the
        # magic the way a capture does, through the same table `capture()` uses.
        spec_format = spec.get("format")
        try:
            magics = datagram_magics(spec_format)
        except Poisoned as refused:
            wrong += 1
            magics = {}
            print("FAIL %-52s %s" % ("the spec's format has a magic table", refused))
        for magic, record in sorted(magics.items()):
            if record not in records:
                wrong += 1
                print("FAIL %-52s the spec holds no such record" % record)
                continue
            # The shortest datagram this format can carry, built from the spec's
            # own field list so the case needs no transcribed layout of its own.
            # Every field past the magic reads zero, and nothing follows the
            # carrier header, so a walk yields the header and no frame.
            width = 0
            for field in records[record]:
                width += field["width"] if field["kind"] == "bits" else 8
            body = bytes([magic]) + bytes((width - 8 + 7) // 8)
            walked = None
            try:
                walked, _frames = walk_datagram(records, {}, body, magics)
            except Poisoned as refused:
                print("FAIL %-52s %s" % (record, refused))
                wrong += 1
                continue
            check(
                "format %s walks 0x%02x as %s" % (spec_format, magic, record),
                walked.get("magic"),
                magic,
            )
        if magics:
            stranger = (max(magics) + 1) & 0xFF
            rejects(
                "format %s refuses a datagram magic it does not speak" % spec_format,
                lambda m=stranger: walk_datagram(records, {}, bytes([m]) + b"\x01", magics),
            )

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
                print("FAIL %-52s %s" % (name, refused))
                continue
            check("%s decodes as its prose reads (%s)" % (name, frame["why"]), got, frame["values"])
            rejects(
                "%s rejects a trailing byte" % name, lambda f=frame, r=records[name]: decode(r, f["bytes"] + b"\xff")
            )

    print("SELFTEST %d wrong" % wrong)
    return 1 if wrong else 0


ROW_CHANNELS = {39: "SYNC_ROW", 40: "SYNC_ROW_DELTA", 41: "SYNC_ROW_WINDOW"}
CONTROL_CHANNEL = 43
CONTROL_TAGS = {0: "OPEN", 1: "READY", 2: "ACCEPT", 3: "RESET", 4: "CLOSE"}


def read_row_head(payload):
    """A v11 state row's stream name, from WIRE.md 8.

    The token, the revision and the distance precede the mask, and none of the
    three depends on the schema, so a row names its stream in a capture whose
    header carries no compiled column list.
    """
    stream = ReadStream(payload)
    token = stream.varuint(10)
    token_bits = stream.read
    revision = stream.varuint(10)
    revision_bits = stream.read - token_bits
    distance = stream.bits(6)
    return token, revision, distance, token_bits, revision_bits


def read_control(records, payload):
    """One complete ROW_CONTROL record, from WIRE.md 10."""
    stream = ReadStream(payload)
    tag = stream.bits(8)
    if tag not in CONTROL_TAGS:
        raise Poisoned("control tag %d is above 4" % tag)
    name = CONTROL_TAGS[tag]
    if name == "ACCEPT":
        count = stream.int_range(1, 32)
        entries = []
        for _ in range(count):
            entries.append((stream.varuint(10), stream.varuint(10)))
        stream.align_verify()
        return name, {"entries": entries}
    layout = records["RowControl" + name.capitalize()]
    stream.read = 0
    body = decode_into(layout, stream)
    stream.align_verify()
    return name, body


def percentile(values, share):
    if not values:
        return 0
    ordered = sorted(values)
    at = min(len(ordered) - 1, int(share * (len(ordered) - 1) + 0.5))
    return ordered[at]


def account(path, spec_path=None):
    """The byte and latency account one peer's capture can answer alone.

    A row names its stream and a receipt names the same stream, both without a
    schema, so a capture joins the two by (token, revision) and answers what a
    receipt cost in time and what a lane owed while it waited.
    """
    header, rows = read_capture(path)
    records = header.get("records", {})
    if spec_path:
        with open(spec_path) as handle:
            records = json.load(handle).get("records", records)
    channels = {int(row["id"]): row["name"] for row in header.get("channels", [])}
    magics = datagram_magics(header.get("format"))

    payload_bytes = {0: 0, 1: 0}
    datagram_bytes = {0: 0, 1: 0}
    control_bytes = {0: 0, 1: 0}
    row_bytes = {0: 0, 1: 0}
    control_records = {}
    rows_by_distance = {"full": 0, "delta": 0}
    naming_bits = {0: 0, 1: 0}
    naming_rows = {0: 0, 1: 0}

    exposed_at = {}
    confirmed = {}
    exposed_high = {}
    receipt_latency = []
    pending_series = []
    ready_at = {}
    first_full_in = {}
    errors = 0

    for index, row in enumerate(rows):
        direction = row["dir"]
        wall = row["wall_ms"]
        datagram_bytes[direction] += len(row["bytes"])
        try:
            _head, frames = walk_datagram(records, channels, row["bytes"], magics)
        except Poisoned:
            errors += 1
            continue
        for frame in frames:
            size = len(frame["payload"])
            payload_bytes[direction] += size
            channel = frame["channel"]
            if channel in ROW_CHANNELS:
                row_bytes[direction] += size
                if channel == 41:
                    continue
                try:
                    token, revision, distance, token_bits, revision_bits = read_row_head(frame["payload"])
                except Poisoned:
                    errors += 1
                    continue
                rows_by_distance["full" if distance == 0 else "delta"] += 1
                naming_bits[direction] += token_bits + revision_bits
                naming_rows[direction] += 1
                if direction == 0:
                    exposed_at.setdefault((token, revision), wall)
                    if revision > exposed_high.get(token, 0):
                        exposed_high[token] = revision
                elif distance == 0 and token not in first_full_in:
                    first_full_in[token] = wall
            elif channel == CONTROL_CHANNEL:
                control_bytes[direction] += size
                try:
                    name, body = read_control(records, frame["payload"])
                except Poisoned:
                    errors += 1
                    continue
                control_records[name] = control_records.get(name, 0) + 1
                if name == "READY":
                    ready_at.setdefault(body["token"], wall)
                if name == "ACCEPT" and direction == 1:
                    for token, revision in body["entries"]:
                        if revision > confirmed.get(token, 0):
                            confirmed[token] = revision
                        sent = exposed_at.get((token, revision))
                        if sent is not None:
                            receipt_latency.append(wall - sent)
        owing = sum(1 for token, high in exposed_high.items() if high > confirmed.get(token, 0))
        pending_series.append(owing)

    print("ACCOUNT %s format=%s" % (path, header.get("format")))
    for direction, label in ((0, "OUT"), (1, "IN ")):
        carrier = datagram_bytes[direction] - payload_bytes[direction]
        total = datagram_bytes[direction] or 1
        print(
            "  %s datagram=%d payload=%d carrier=%d (%.1f%%) "
            "rows=%d (%.1f%%) control=%d (%.1f%%)"
            % (
                label,
                datagram_bytes[direction],
                payload_bytes[direction],
                carrier,
                100.0 * carrier / total,
                row_bytes[direction],
                100.0 * row_bytes[direction] / total,
                control_bytes[direction],
                100.0 * control_bytes[direction] / total,
            )
        )
    print("  control records %s" % control_records)
    print("  rows full=%d delta=%d" % (rows_by_distance["full"], rows_by_distance["delta"]))
    for direction, label in ((0, "OUT"), (1, "IN ")):
        if not naming_rows[direction]:
            continue
        print(
            "  %s row naming rows=%d token+revision bits/row=%.2f bytes=%d "
            "(%.1f%% of row bytes)"
            % (
                label,
                naming_rows[direction],
                naming_bits[direction] / naming_rows[direction],
                naming_bits[direction] // 8,
                100.0 * (naming_bits[direction] / 8.0) / (row_bytes[direction] or 1),
            )
        )
    print(
        "  receipt latency ms n=%d p50=%d p90=%d max=%d"
        % (
            len(receipt_latency),
            percentile(receipt_latency, 0.50),
            percentile(receipt_latency, 0.90),
            max(receipt_latency) if receipt_latency else 0,
        )
    )
    print(
        "  pending lanes max=%d p90=%d streams=%d"
        % (
            max(pending_series) if pending_series else 0,
            percentile(pending_series, 0.90),
            len(exposed_high) or len(first_full_in),
        )
    )
    usable = [
        first_full_in[token] - ready_at[token]
        for token in first_full_in
        if token in ready_at and first_full_in[token] >= ready_at[token]
    ]
    print(
        "  first usable row ms after READY n=%d p50=%d max=%d"
        % (len(usable), percentile(usable, 0.50), max(usable) if usable else 0)
    )
    if errors:
        print("  UNDECODED %d" % errors)
    return 1 if errors else 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--spec")
    parser.add_argument("--record")
    parser.add_argument("--hex")
    parser.add_argument("--self-test", action="store_true")
    parser.add_argument("--capture")
    parser.add_argument("--account")
    parser.add_argument("--verbose", action="store_true")
    args = parser.parse_args()

    if args.self_test:
        return self_test(args.spec)
    if args.account:
        try:
            return account(args.account, args.spec)
        except Poisoned as refused:
            sys.exit("ERROR %s" % refused)
    if args.capture:
        try:
            return capture(args.capture, args.verbose)
        except Poisoned as refused:
            sys.exit("ERROR %s" % refused)
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
        sys.exit("ERROR %s" % refused)
    for name, value in decoded.items():
        print("%s = %r" % (name, value))
    return 0


if __name__ == "__main__":
    sys.exit(main())
