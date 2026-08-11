# WIRE v8, the shipped carrier format

Reference for the format Networked speaks today. It is written from the
implementation (`addons/networked/replication/netw_frame_envelope.gd`,
`extension/src/codec.cpp`, `extension/src/bit_buffer.cpp`) and every claim in
section 6 was measured against a running build rather than read off the source.

v8 is a description, not a contract. Nothing in it is designed; it is what
thirty-five channels grew into, and the value of writing it down is that the
next wire can be diffed against it and can refuse the elisions section 6
names.

---

## 1. Datagram

Networked traffic rides `SceneMultiplayer.send_bytes`, whose receiver re-emits
raw packets. The first byte selects Networked framing versus pass-through, so
an application sharing `send_bytes` must not start a packet with any of the
three magics.

```text
0x4E  reliable      [ magic u8 ][ frame ][ frame ] ...
0x6E  unreliable    [ magic u8 | seq u16 ][ frame ][ frame ] ...
0x8E  unreliable    [ magic u8 | seq u16 | echo u16 ][ frame ][ frame ] ...
      acked
```

`seq` is counted per destination peer by the session's carrier and stamps
every frame in the datagram. `echo` is the freshest inbound sequence the
sender has seen from this peer, which is the receiver-to-sender datagram ack a
per-peer delta picks its baseline against. It is a different thing from the
reconciliation ack the SYNC `ACKED` bit carries, and the two are never
interchangeable.

Reliable delivery is ordered by the transport and never freshness-gated.
Unreliable frames are gated per (sender, route, channel): a frame applies only
when its datagram is fresher than the last accepted for that stream, compared
across a u16 half window. A reordered datagram therefore loses only the
streams a fresher one already superseded.

There are two magics for one transfer mode because the receiver of a raw
packet cannot observe how it was sent, and reliability is what decides whether
a racing `CALL` defers or drops.

## 2. Frame

```text
[ route varint | comp u8 | channel u8 | len varint | payload ]
```

Many frames pack into one datagram, which is what per-peer aggregation flushes
once per tick. `route` addresses the entity; route `0` is peer-scoped and
names the session rather than an entity. `comp` selects the target within the
entity:

```text
0          the entity root
1 .. 254   a node in the entity's hydrated component table
255        an unmapped node, addressed by relative path
```

`comp == 255` wraps the payload rather than extending the header:

```text
payload := [ path_len varint | path utf8 | inner payload ]
```

## 3. Channels

The channel byte names the handler the payload reaches. `R` is reliable and
`U` unreliable, which is a property of the call site rather than of the id.
`route 0` marks a peer-scoped channel; everything else is entity-routed.

```text
id   name                      del  route  direction
--   -----------------------   ---  -----  ---------------
0    RETIRED, never reclaim      -      -  -
1    RETIRED, never reclaim      -      -  -
2    ACTION                      R  route  either
3    CALL                        R  route  either
4    REPLY                       R  route  either
5    SIGNAL                      R  route  either
6    PROPERTY_SYNC               R  route  either
7    RETIRED, never reclaim      -      -  -
8    INTEREST_AWARENESS          R      0  server -> client
9    CLOCK_HANDSHAKE             R      0  client -> server
10   CLOCK_HANDSHAKE_REPLY       R      0  server -> client
11   CLOCK_PING                  U      0  client -> server, un-batched
12   CLOCK_PONG                  U      0  server -> client, un-batched
13   LAGCOMP_DENY                R      0  server -> client
14   SPAWN                       R      0  server -> client
15   DESPAWN                     R      0  server -> client
16   REPARENT                    R      0  server -> client
17   TABLE                     U/R      0  server -> client
18   RESERVED, never claim       -      -  -
19   SYNC                        U  route  either
20   SYNC_DELTA                  R  route  either
21   CONTROL_REQUEST             R  route  client -> server
22   CONTROL_APPLY               R  route  server -> client
23   SESSION_JOIN                R      0  client -> server
24   SESSION_ACCEPT              R      0  server -> client
25   SESSION_ROSTER              R      0  server -> client
26   SESSION_PAUSE               R      0  server -> client
27   SESSION_UNPAUSE             R      0  server -> client
28   SESSION_KICKED              R      0  server -> client
29   SESSION_SCENE_REQUEST       R      0  client -> server
30   SESSION_SCENE_RESULT        R      0  server -> client
31   SESSION_SHUTDOWN            R      0  server -> client
32   SESSION_SCENE_RELEASED      R      0  server -> client
33   SESSION_KICK_REQUEST        R      0  client -> server
34   SESSION_LEAVE_REQUEST       R      0  client -> server
35   PREDICT_COMMAND             U  route  owner -> server
36   PREDICT_ACK                 U  route  server -> owner
37   PREDICT_RELAY               U  route  server -> subscriber
38   PREDICT_RELAY_REQUEST       R  route  client -> server
100  .. 254  user channels       -  route  either
```

`TABLE` is the one carrier whose freshness is judged in its own decoder rather
than by the datagram book, because a route-0 frame never reaches that book.
Its data frames ride unreliable and its removals, tombstones and snapshots
ride reliable.

`CLOCK_PING` and `CLOCK_PONG` bypass aggregation, because an aggregation delay
would inflate the round-trip sample the calibration is measuring.

## 4. The SYNC payload

One flags byte gates every optional section, so a plain frame (`flags == 0`)
is byte-identical to the bare positional payload and turning a bit on extends
the frame rather than re-cutting it.

```text
bit0  STAMPED    a tick varint follows
bit1  ACKED      a reconciliation ack varint follows (requires bit0)
bit2  WINDOWED   a count varint and windowed rows follow (requires bit0)
bit3  TAPED      a trailing prediction tape block follows
bit4  MASKED     a mask varint follows (the per-peer volatile diff)
bit5-7 reserved
```

```text
[ ordinal varint | flags u8 ]
[ tick varint            ]   if STAMPED
[ (ack + 1) varint       ]   if ACKED
[ mask varint            ]   if MASKED
[ count varint,              if WINDOWED: `count` rows, newest first
  { age varint, values } ]
[ values                 ]   otherwise; the masked subset when MASKED
[ tape block             ]   if TAPED
```

`ack` is written as `ack + 1` so the no-input sentinel `-1` rides as zero.
That lift is not decoration: section 6.2 is why it has to be there.

`WINDOWED` and `MASKED` never combine, and a windowed frame carries its sample
rows in place of the top-level values.

The tape block is a contiguous entry window:

```text
[ epoch u8 | base_index varint | count u8 ]
[ tagged_delta varint ] x count
```

`tagged_delta` is `zigzag(label - previous_label) << 1`, with the low bit
carrying `fresh`. Entry `i` reconstructs to index `base_index + i`, so the
window is contiguous by construction and a gap is unrepresentable.

## 5. Values

A value is either quantized or raw, decided per field by whether the
declaration named a quantizer. The two are not distinguished on the wire; both
ends read the same declaration.

**Quantized.** The quantizer writes its own bits and nothing else is emitted.
`NetwQuantizeFixed` grids on `code = round((value - min) / step)` over
`ceil((max - min) / step) + 1` levels, in `grid_bits()` bits, clamped to the
limits. Decoding is `min + code * step`, so a decoded value is exactly on the
grid and re-encoding it is idempotent.

**Raw.** A type byte then the payload:

```text
0  BOOL      u8, 0 or 1
1  INT       u64
2  VECTOR2   f32 x, f32 y
3  FLOAT     f32
4  VECTOR3   f32 x, f32 y, f32 z
5  FALLBACK  u32 length, then `var_to_bytes`
```

**A raw float is 32 bits.** A GDScript float is a double, so a raw float field
loses precision on the wire, and the value a receiver holds is the f32
rounding of what the sender read. This is why the masked lane's diff has to
canonicalize even for fields with no quantizer.

**Varint** is little-endian 7-bit groups with the high bit as continuation,
written at most 5 bytes wide.

## 6. The elision audit

What v8 leaves implicit. These are the entries the next wire has to answer
rather than inherit. Everything here was measured on a running build.

### 6.1 There is no version on the carrier

`WIRE_VERSION = 8` exists in exactly one place: a byte inside the
`PREDICT_COMMAND` and `PREDICT_ACK` payloads, checked by prediction's own
decoder. No datagram, frame, or other channel carries a version, so two peers
built from different revisions of any other channel's payload will decode each
other's bytes as the wrong shape and be told nothing. What partially covers
this today is per-family and accidental: sync sets compare a 16-bit schema
`wire_hash` at spawn, and `app_id` gates discovery. Nothing covers the frame
grammar, the channel table, or the session payloads at all.

### 6.2 A varint cannot carry a negative number, and says nothing

```text
put_varint(-1)  -> 1 byte  -> get_safe_varint reads 127
put_varint(-5)  -> 1 byte  -> get_safe_varint reads 123
```

The encoder shifts right arithmetically, so a negative value never reaches
zero, the loop takes its first `else` branch, and one byte of the low seven
bits is all that is written. No error is raised at either end. Every field
that can be negative is therefore lifted at its call site before encoding
(`ack + 1`, `base_tick + 1`), and a caller that forgets loses the value
silently. `put_svarint`, which zigzags first, exists and is the correct
spelling, but the SYNC grammar does not use it.

### 6.3 A varint saturates at 2^35 - 1

```text
34359738367  -> 5 bytes -> reads back exactly
34359738368  -> 5 bytes -> reader raises and returns -1
```

The writer stops after 5 bytes with the continuation bit of the fifth still
set, so the encoding is not merely truncated, it is malformed: the reader
consumes a sixth byte belonging to the next field before giving up. A route,
ordinal, or length above 2^35 corrupts the rest of the frame.

### 6.4 Decode never rejects

`get_safe_varint` answers `-1` on overflow and callers mostly do not check.
`decode_sync_frame` does not check `ordinal`, `tick`, or `mask`, so a corrupt
`mask` of `-1` selects every field. `get_aligned_bytes(n)` past the end
returns what is there and no error, so an over-long length prefix truncates
silently. An empty payload decodes to a well-formed-looking frame with
`ordinal 0`, `flags 0` and no values.

There is no residue check anywhere: a frame whose payload is longer than its
grammar needs is accepted and the excess is ignored. Nothing in v8 can
distinguish "decoded correctly" from "decoded something".

### 6.5 The channel table is code, not data

The table in section 3 exists as an enum with prose docs, and its delivery,
routing and direction columns exist only as sentences. Nothing enforces that
`SPAWN` is reliable or that `CLOCK_PING` is un-batched; each is a property of
the call site that happens to send it. Four ids are burned (`0`, `1`, `7`,
`18`) and the only thing keeping them burned is a comment.

### 6.6 Framing is per-frame, and pays for it

Every frame carries route, comp, channel and length, even when a hundred
frames in one datagram share a route or a channel. The header is the same
width for a peer-scoped session notice and for a one-field position update.
Nothing groups, and nothing is fitted to a budget.

---

## 7. What this document is for

Two jobs, both of which end when the next wire ships.

**The diff base.** A cutover changes bytes on purpose, so byte parity cannot
certify it. What can is a written statement of what the old wire said, against
which the new spec is read for things that were dropped without anyone
deciding to drop them.

**The elision list.** Section 6 is the checklist. An entry there is answered
when the new wire either implements the fix or records why the elision stands.

---

## 8. WIRE v9, the native bit-stream format

WIRE v9 replaces the v8 byte-granular carrier with a schema-compiled bit-stream
format. Fields do not carry individual type tags or 32-bit float alignment on
the wire; a sealed `SchemaRecord` compiles to a fixed-width `WirePlan` on both
peers.

### 8.1 Bit-stream vocabulary and alignment

All data streams sequentially through `WriteStream` and `ReadStream` bit by
bit, using little-endian bit packing.

- **`bits(value, width)`**: Writes or reads `width` bits (1 to 64) for unsigned
  integer representations.
- **`int_range(value, min, max)`**: Encodes `value - min` in
  `ceil(log2(max - min + 1))` bits.
- **`bool1(flag)`**: 1 bit (0 or 1).
- **`bytes_capped(data, max_len)`**: Varint-length prefix followed by byte
  payload.
- **`align_verify()`**: Flushes trailing bits to the next byte boundary and
  verifies strict byte alignment.

### 8.2 DeltaMode::AUTO and ladder selection

Row compression is governed by `DeltaMode`: `FULL`, `LADDER`, or `AUTO`.

- **`FULL`**: Every column emits its raw gridded code word (`ColumnPlan::width`
  bits).
- **`LADDER`**: Delta buckets represent signed-step code changes between tick
  baselines using small bit-width offsets.
- **`AUTO`**: Selected per column based on column value physics:
  - **Integrated state** (positions, velocities): Step sizes are physically
    bounded per tick. 87%–100% of moves fit small buckets, saving 39%–71% over
    `FULL`. `AUTO` assigns signed-step delta buckets.
  - **Authored or wrapped state** (inputs, angles): Input axes jump discretely
    and angles wrap around bounds. Step sizes do not fit small buckets, paying
    an escape penalty. `AUTO` selects `FULL` to avoid escape overhead.

### 8.3 Transcribed V9 hand-built frame specification

The following reference test frame is transcribed directly from prose:

**Schema Plan**:
- Column 0: `flags` (BOOL, 1 bit)
- Column 1: `pos_x` (11 bits, fixed grid 0..2047)
- Column 2: `pos_y` (11 bits, fixed grid 0..2047)
- Total payload: 23 bits, packed into 3 bytes (`0x01, 0x08, 0x20` + align).

**Bit layout (LSB first)**:
- Bit 0: `flags = 1`
- Bits 1..11: `pos_x = 1024` (`0x400`, binary `10000000000`)
- Bits 12..22: `pos_y = 512` (`0x200`, binary `01000000000`)
- Bit 23: Alignment pad (`0`)

**Packed byte sequence**: `0x01, 0x08, 0x20`
- Byte 0: `0x01` (`00000001`) -> bit 0 (1), bits 1..7 (low 7 bits of 1024)
- Byte 1: `0x08` (`00001000`) -> bits 8..11 (high 4 bits of 1024), bits 12..15
  (low 4 bits of 512)
- Byte 2: `0x20` (`00100000`) -> bits 16..22 (high 7 bits of 512), bit 23 pad

Decoding `0x01, 0x08, 0x20` against the schema plan yields:
- Column 0 (`flags`) = `1`
- Column 1 (`pos_x`) = `1024`
- Column 2 (`pos_y`) = `512`


### 8.4 The four prediction channels

Prediction's lanes carry no version byte. Protocol identity is settled before
any of them is admitted, so a peer that disagrees about a payload's shape never
reaches one. Every variable section is bounded by the count in its typed
header, and a decoder accepts only a complete frame with no unread residue.

Both data lanes are window-redundant: every send repeats the range still in
flight, floored by what the other side has confirmed, so a lost datagram heals
on the next send rather than on a retransmit.

**`PREDICT_COMMAND` (35, U, owner -> server).** One contiguous transition run
and one schema-planned row per fresh transition.

```text
header       svarint ack_of_acks   the owner's highest seen acknowledgement
             u8      epoch
             varuint base          the run's first transition
             u8      count         transitions in the run, at most 255
per transition
             varuint tagged        zigzag(label - previous) << 1 | fresh
payloads     one WirePlan row per FRESH transition, in run order, then align
evidence     u8 count, at most the transition count, then per record:
             u8 mask, then pre, post, environment, topology and witness as
             32 bits each, three pre families, three post families, and a
             raw fingerprint only where the mask claims one
```

A fresh transition and its row are inseparable, so a frame that lost one has no
shorter reading and is refused whole. The payload plan is the input set's
`VOLATILE` columns as a sealed `SchemaRecord`, which is the same column order
the declaration states, so neither peer mints a second numbering.

**`PREDICT_ACK` (36, U, server -> owner).** Authority's oldest evidence prefix,
at most 21 records so a worst-case raw frame stays under the 1150-byte
unreliable budget. Later records ride the next send once the ack-of-acks
advances.

```text
header       u8 epoch, varuint base, u8 count
per record   u8 mask, then pre, command, environment, post, topology and
             witness as 32 bits each, three pre families, three post
             families, a raw fingerprint where the mask claims one, and u8
             flags, whose bits 2..4 carry authority's witness class
```

**`PREDICT_RELAY` (37, U, server -> subscriber).** The admitted
`PREDICT_COMMAND` bytes re-emitted unchanged. A subscriber decodes the author's
own frame, which is what makes a relayed command the author's rather than a
re-cut of it.

**`PREDICT_RELAY_REQUEST` (38, R, client -> server).** One boolean bit, aligned
to a single byte. The lane it opens is unreliable and the request is not,
because a lost subscribe presents as an entity that simply never relays.
