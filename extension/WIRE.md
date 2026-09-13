# WIRE, the carrier format

**Section 9 is the format Networked speaks. Sections 1 to 8 are the two
formats it replaced, kept as the diff base section 6 argues against.** A
reader who wants to know what a byte on the link means today reads section 9
and nothing else. A reader who wants to know why it is shaped that way reads
section 6 first, which names six elisions v8 made, and then section 9, which
answers each of them.

Sections 1 to 7 describe **v8**, which rode `Dictionary` payloads through a
byte codec. Section 8 describes **v9**, which put the row frames on a bit
stream and left the ends of the pipeline where v8 had them. Neither is spoken
by any build on this branch, and their magics are refused rather than read
(section 9.1). They are here because a format is only ever understood against
the one before it, and because section 6 was measured against a running v8
rather than read off its source.

v8 was a description rather than a contract: nothing in it was designed, it is
what thirty-five channels grew into. v10 is a contract, and section 9 states
it.

---

## 1. Datagram

Networked traffic rides `SceneMultiplayer.send_bytes`, whose receiver re-emits
raw packets. The first byte selects Networked framing versus pass-through, so
an application sharing `send_bytes` must not start a packet with any of the
three magics.

**These are v8's magics and v9 moved them to `0x56`, `0x76` and `0x96`.
Section 9.1 holds the ones a build on this branch writes, and all six of the
retired bytes are refused rather than read.**

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

The table below is regenerated from `src/wire/registry.cpp`, which is the one
declaration. Every axis it carries is folded into the identity hash two peers
exchange at admission, so a build that disagrees about any cell of it cannot
pair rather than mis-decoding later.

```text
id   name                    kind    rel fresh del agg direction  payload
--   ----------------------- ------- --- ----- --- --- ---------- -------
0    RESERVED, never claim   -       -   -     -   -   -          -
1    RESERVED, never claim   -       -   -     -   -   -          -
2    ACTION                  routed  R   -     fit -   either     planned
3    CALL                    routed  R   -     fit -   either     planned
4    REPLY                   routed  R   -     fit -   either     planned
5    SIGNAL                  routed  R   -     fit yes either     planned
6    PROPERTY_SYNC           routed  R   -     fit yes either     planned
7    RESERVED, never claim   -       -   -     -   -   -          -
8    INTEREST_AWARENESS      session R   -     fit -   srv -> cli planned
9    CLOCK_HANDSHAKE         session R   -     fit -   cli -> srv planned
10   CLOCK_HANDSHAKE_REPLY   session R   -     fit -   srv -> cli planned
11   CLOCK_PING              session U   -     now -   cli -> srv planned
12   CLOCK_PONG              session U   -     now -   srv -> cli planned
13   LAGCOMP_DENY            session R   -     fit -   srv -> cli planned
14   SPAWN                   session R   -     fit -   srv -> cli planned
15   DESPAWN                 session R   -     fit -   srv -> cli planned
16   REPARENT                session R   -     fit -   srv -> cli planned
17   TABLE                   keyed   UA  fresh fit yes srv -> cli delta
18   HIDE                    session R   -     fit -   srv -> cli planned
19   SYNC                    keyed   UA  fresh fit yes either     delta
20   SYNC_DELTA              routed  R   -     fit yes either     delta
21   CONTROL_REQUEST         routed  R   -     fit -   cli -> srv planned
22   CONTROL_APPLY           routed  R   -     fit -   srv -> cli planned
23   SESSION_JOIN            session R   -     fit -   cli -> srv planned
24   SESSION_ACCEPT          session R   -     fit -   srv -> cli planned
25   SESSION_ROSTER          session R   -     fit -   srv -> cli planned
26   SESSION_PAUSE           session R   -     fit -   srv -> cli planned
27   SESSION_UNPAUSE         session R   -     fit -   srv -> cli planned
28   SESSION_KICKED          session R   -     fit -   srv -> cli planned
29   SESSION_SCENE_REQUEST   session R   -     fit -   cli -> srv planned
30   SESSION_SCENE_RESULT    session R   -     fit -   srv -> cli planned
31   SESSION_SHUTDOWN        session R   -     fit -   srv -> cli planned
32   SESSION_SCENE_RELEASED  session R   -     fit -   srv -> cli planned
33   SESSION_KICK_REQUEST    session R   -     fit -   cli -> srv planned
34   SESSION_LEAVE_REQUEST   session R   -     fit -   cli -> srv planned
35   PREDICT_COMMAND         routed  U   fresh fit -   own -> srv planned
36   PREDICT_ACK             routed  U   fresh fit -   srv -> own planned
37   PREDICT_RELAY           routed  U   fresh fit -   srv -> cli planned
38   PREDICT_RELAY_REQUEST   routed  R   -     fit -   cli -> srv planned
39   SYNC_ROW                keyed   UA  fresh fit yes either     delta
40   SYNC_ROW_DELTA          routed  R   -     fit yes either     delta
41   SYNC_ROW_WINDOW         keyed   U   fresh fit yes either     planned
42   SESSION_SCENE_SEAT      session R   -     fit -   srv -> cli planned
100+ user channels           routed  -   -     -   -   either     raw
```

`kind` says how the channel is addressed: `session` is route 0, `routed` names
an entity, `keyed` names an entity and a target within it. `rel` is `R`
reliable, `U` unreliable and `UA` unreliable with an echo owed. `fresh` marks
a channel whose frames are gated per `(route, sender, channel)` against a u16
half window, so a reordered datagram loses only the streams a fresher one
superseded. `del` is `fit` when the frame joins the peer's run and `now` when
it leaves immediately. `agg` marks the channels the tick flush batches.
`payload` is the grammar contract: `raw` is bytes the core never reads,
`planned` is a fixed layout, `delta` is a row against a baseline.

A channel also carries a `payload_revision`, folded into identity when
non-zero, which a v10 format change to a hand-written PLANNED payload bumps
when the format version itself does not move. Every revision is `0` today.

**Ids 0, 1 and 7 are reserved and stay reserved.** They were claimed by
formats older than v8 and reclaiming one would make an ancient frame decode as
a modern channel rather than being refused.

`DESPAWN` and `HIDE` carry the same `varint route` payload and mean different
things. `DESPAWN` says the entity ceased to exist, is addressed to every peer
holding a copy, and tombstones the route for the session. `HIDE` says this one
peer no longer holds a copy, is addressed to that peer alone by the interest
sweep, and is reversible: the route goes absent and a later `SPAWN` binds the
same identity again.

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
`NetwQuantizeScalar` spends `bit_count` bits per axis on `2 ** bit_count - 1`
levels spanning `min_limit .. max_limit`, so `code = round(unit * top)` for
`top = 2 ** bit_count - 2` and `unit` the value clamped into `0 .. 1` across
the range. Decoding is `min + code / top * (max - min)`, so both limits and
the midpoint are exactly representable and re-encoding a decoded value is
idempotent. Its `resolution_step` is the derived `(max - min) / top`, and
authoring by step picks the smallest `bit_count` whose grid is at least that
fine.

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

**All six are answered, and each entry below names where.** The audit stays
whole rather than being edited down to what is left, because an answer is only
readable against the thing it answered.

```text
6.1 no version on the carrier    identity: format version, the whole channel
                                 table and every sealed schema, exchanged at
                                 admission and refused by name
6.2 a varint eats a negative     svarint zigzags, and 9.x names the spelling
                                 of every field rather than leaving it to a
                                 call site to lift
6.3 a varint saturates at 2^35   every varuint declares its max_bytes and a
                                 value past it produces no frame at all (9.2)
6.4 decode never rejects         a decoder poisons on over-read, refuses
                                 residue and decodes to exhaustion, and a
                                 refusal writes none of the row (9.4)
6.5 the channel table is code    section 3's table is the registry, folded
                                 into identity along all eight axes
6.6 framing is per-frame         the datagram spends the tick once and the
                                 envelope spends the addressing once, so a
                                 row frame is its mask and its columns plus
                                 two bytes (9.1, 9.4)
```

### 6.1 There is no version on the carrier

`WIRE_VERSION = 8` exists in exactly one place: a byte inside the
`PREDICT_COMMAND` and `PREDICT_ACK` payloads, checked by prediction's own
decoder. No datagram, frame, or other channel carries a version, so two peers
built from different revisions of any other channel's payload will decode each
other's bytes as the wrong shape and be told nothing. What partially covers
this today is per-family: sync sets compare a schema `wire_hash` at spawn
(§8.5), and `app_id` gates discovery. Nothing covers the frame grammar, the
channel table, or the session payloads at all.

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
bit, using little-endian bit packing: within a byte the first bit written is
bit 0, and a value wider than the space left in the current byte continues
into the next byte from its own low end.

- **`bits(value, width)`**: Writes or reads `width` bits (0 to 64) of an
  unsigned value, low bit first. A value with any bit set above `width`
  poisons the stream rather than truncating.
- **`int_range(value, min, max)`**: `bits(value - min, w)` where `w` is the
  number of bits needed to hold `max - min`, which is
  `ceil(log2(max - min + 1))` and is **0 when `min == max`**: a range of one
  value occupies no bits at all.
- **`bool1(flag)`**: 1 bit (0 or 1).
- **`varuint(value, max_bytes)`**: Byte-aligns the stream first, then base-128
  little-endian groups: each byte carries seven value bits low-first and its
  top bit set on every byte but the last. At most `max_bytes` groups, 1 to 10.
  The encoding is **canonical**: a final group of zero after a first group is
  a redundant spelling and a reader refuses it rather than accepting a second
  encoding of one number.
- **`svarint(value, max_bytes)`**: `varuint` of the zigzag fold,
  `(value << 1) ^ (value >> 63)`, so a small negative costs what a small
  positive costs.
- **`bytes_capped(data, cap)`**: `int_range(length, 0, cap)`, then
  `align_verify()`, then the raw bytes. The length prefix is a **range, not a
  varint**: the cap is known to both peers, so the prefix costs
  `ceil(log2(cap + 1))` bits rather than a whole byte.
- **`align_verify()`**: Flushes trailing bits to the next byte boundary and
  verifies strict byte alignment. `varuint`, `svarint` and the payload half of
  `bytes_capped` are byte-aligned by construction; nothing else is.

### 8.2 The delta modes, and the one this section invented

Row compression is governed by `DeltaMode`, which declares `FULL` and
`LADDER`.

- **`FULL`**: Every column emits its raw gridded code word (`ColumnPlan::width`
  bits).
- **`LADDER`**: Delta buckets represent signed-step code changes between tick
  baselines using small bit-width offsets.

**This section also described a third mode, `AUTO`, choosing between the two
per column from the physics of the values. No such mode ever existed in v9's
code**, and what v9 actually did was write `FULL` for every column, because
`LADDER` had no caller either. The reasoning `AUTO` was given was sound and
v10 built it, as a DECLARATION rather than a third wire mode: the wire still
knows two modes, and `NetwPropertySetColumn.delta_mode` chooses between them
per column before a byte is written, defaulting to a derivation of exactly the
physics `AUTO` described. §9.5 is the format and §9.3 is the selection.

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

### 8.5 The SPAWN schema descriptor sections

A SPAWN payload closes with two descriptor sections, consumed first and
derived second, each a count followed by that many ordinal-and-hash pairs.
They are what lets a receiver check the sender's schema per set, for a set
sealed after the session opened and therefore outside the identity fold the
session already agreed on.

```text
per section  varint count
per entry    varint ordinal      the set's ordinal on this route
             u32    schema_hash  aligned, little endian
```

**The hash is 32 bits.** It is the full `String::hash` of the schema's
canonical description with no mask: `NetwPropertySet::wire_hash` for a
derived set, and the joined sync and watch path list for a consumed one. A
16-bit hash collides on roughly one pair in 65,536, which for a schema
disagreement means the receiver decodes the wrong shape and is told nothing.

**A described route admits only the ordinals its description names.** A
receiver that read a SPAWN's derived section knows exactly which sets the
sender holds on that route. An ordinal absent from that section is a
disagreement about the set's existence, not an unversioned set, so its frames
are refused rather than applied. A section carrying a count of zero still
describes the route: it says the sender holds no derived set there. A route
no SPAWN ever described is unconstrained, because nothing was claimed about
it.

---

## 9. WIRE v10, one substrate

Every v10 frame is written and read through the §8.1 vocabulary and a
`describe()` layout, so one description answers for the encoder, the decoder,
the pricer and the spec artifact that `tools/wire_decode.py` reads. The
sections below land as their families cross, and a family with no section
here still speaks the grammar §1 to §8 states for it.

Two properties hold for every v10 grammar and they are what the crossing buys.

**A varuint is canonical and bounded by the `max_bytes` its field declares.**
A value too large to spell in that many groups is refused by the encoder
rather than truncated onto the wire, and a redundant final group is refused by
the decoder rather than accepted as a second spelling of one number.

**A decoder that stops before its input is exhausted has not decoded.**
Residue is a refusal, and so is a read past the end. Nothing is ever yielded
from bytes that were not all there.

**A column is an element width and a stride, and no element exceeds 64 bits.**
An unquantized composite compiles to its scalar element repeated: `VECTOR2` is
32 bits by 2, `VECTOR3` is 32 by 3, and `VECTOR4`, `COLOR` and `QUATERNION`
are 32 by 4. A declared stride multiplies that count, so an array of three
`VECTOR3` is nine elements rather than three. The bytes are the ones the
`F32`-by-3 spelling already wrote, which is why an unquantized composite is a
legal layout rather than a refusal, and the element bound is what lets the
row writer spend every column through `bits`, which reads no width above 64.

A quantized column takes its quantizer's declared width and stride instead. A
quantizer maps a value to codes and back rather than to a stream, so the row
writer is the only thing that places bits, and a layout whose parts do not
share one width declares a stride of one and packs its parts into that single
code, which is what `NetwQuantizeQuaternion` does with its two selector bits
and its three components.

An `ENTITY` column is 32 bits and carries `route + 1`, so `0` is null.

A schema declares at most 64 columns, because a row's mask is one bit per
column and the mask is a `u64`. The refusal is at seal, where an author is
still writing the declaration, rather than at send.

### 9.1 Datagram

Three shapes, one per line, selected by the first byte. Every shape carries
the tick the datagram was built at, and the acked shape carries the delivery
history beside the echo.

```text
0x57  reliable          [magic bits 8][base_tick varuint 5]
0x77  unreliable        [magic bits 8][seq bits 16][base_tick varuint 5]
0x97  unreliable acked  [magic bits 8][seq bits 16][ack bits 16]
                        [history bits 32][base_tick varuint 5]
```

Frames follow the header to the end of the datagram, each byte aligned, and
the header itself ends byte aligned so the first frame begins on a byte.

`base_tick` is the session tick plus one, and `0` while the clock is
unconfigured. A frame therefore never spends a tick of its own: every frame in
a datagram was gathered at `T = base_tick - 1`, and a frame that needs a tick
older than `T` spends an age below it. A datagram whose `base_tick` is `0`
carries no tick at all, and a frame inside it that would need one spends an
age of zero, which reads as absent rather than as tick zero.

`seq` counts per destination peer. `ack` is the freshest inbound seq this
sender has seen from this peer, and the acked shape is written whenever that
peer is owed an echo.

`history` is the delivery record behind `ack`. Bit `i` is set when inbound seq
`ack - 1 - i` was seen, so one echo reports the fate of 33 datagrams rather
than one. A bit reads `0` for a seq that was dropped and for a seq that never
existed, which are the same statement from the sender's side: the receiver has
not seen it. The window is fixed at 32 bits because a Networked peer sends at
most one datagram per tick per lane, so 32 datagrams is about a second at
30 Hz and half that at 60, which outlasts any round trip a session survives.
The four bytes buy loss measurement, and they buy a baseline promoted only
from a datagram the receiver reported delivered rather than from one it merely
did not contradict.

**The v8 and v9 magics are refused rather than read.** `0x4E`, `0x6E` and
`0x8E`, and `0x56`, `0x76` and `0x96`, each answer MALFORMED. They are not
FOREIGN: a packet whose first byte was ours one version ago is ours and
broken, and handing it to a game's own `peer_packet` handler is the failure
the three-way read exists to prevent. Any other first byte is FOREIGN and
belongs to whatever else shares the transport.

A packet too short to carry the header its magic claims is MALFORMED, and so
is a `base_tick` that is not a canonical varuint or that overruns the packet.
The reader yields no seq, no ack and no payload offset from a header it
refused.

### 9.2 Frame envelope

```text
[route varuint 5][comp bits 8][channel bits 8][len varuint 3][payload]
```

`route` addresses the entity, and route `0` is peer scoped, naming the session
rather than an entity. `comp` selects the target within the entity: `0` is the
entity root, `1` to `254` index the entity's hydrated component table, and
`255` is an unmapped node addressed by relative path. `len` counts the payload
bytes rather than the frame's.

**`comp` names the target the CHANNEL addresses, and for the three row
channels that target is a property set rather than a node.** SYNC_ROW,
SYNC_ROW_DELTA and SYNC_ROW_WINDOW spend `comp` as the ordinal of the set
within the route, because the set knows its own node and a node address would
say nothing about which of a route's sets a row belongs to. The two are
independent axes and neither substitutes for the other, so a receiver reading
one of those three channels resolves the set and never the component table.
Every other channel reads `comp` as the node address above.

The payload follows the length byte aligned and is exactly `len` bytes. The
envelope never reads it, so a channel's own grammar begins at the first
payload byte, and a datagram walk splits correctly around a payload the walker
could not itself parse.

`comp == 255` wraps the payload rather than extending the header:

```text
payload := [path string][inner]
```

where `string := bytes_capped(utf8, 1023)`. The path spends a ten bit length
and an alignment pad, so it costs two bytes for every path this carrier
admits, and a path past the cap is refused by the encoder.

`route varuint 5` bounds a route at `2^35 - 1` and `len varuint 3` bounds a
payload at `2^21 - 1` bytes, both far above what a session or a datagram can
reach. A value past either bound produces no frame at all. This is the answer
to §6.3: the five byte codec it describes wrote a fifth group with its
continuation bit still set, so a route past the bound was not merely truncated
but malformed, and the reader consumed a byte belonging to the next field and
lost every frame after it.

**A frame is refused whole.** A header that ends before `len` is read, a
length claiming more bytes than remain, a non-canonical varuint, a path that
overruns its own body, and alignment padding that is not zero each end the
walk. The whole frames read before the refusal survive it and the walk reports
that it is not whole. A partial frame is never yielded, which is §6.4's answer
for the envelope.

### 9.3 A column's delta mode, and where it is chosen

Every column carries one of two modes, and the choice is made before the
session opens rather than per frame. `FULL` writes the column's whole code.
`LADDER` writes a per-element selector and either the whole code or a signed
step from a baseline, spelled in §9.5.

**The mode is DECLARED and not adapted, because the choice is a property of
what the value does and a frame cannot see that.** A value a solver integrates
moves by a bounded amount per tick, so its code takes small steps: measured
over two racing corpora, position, linear velocity and angular velocity fit a
small bucket 87 to 100 percent of the time and a ladder is worth 39 to 71
percent against `FULL`. A value a player authors is set to the ends of its
declared range, so its step is half the code space by construction, and a
value that wraps steps the whole space; a ladder costs both of those 12.5
percent. Bucket geometry moves the total by three points across four quite
different geometries and the per-column choice has a SIGN CHANGE in it, so the
selection is the whole lever and the geometry is not.

`NetwPropertySetColumn.delta_mode` is where a game says so, and its default
derives the answer from the declaration:

```text
LADDER   a quantized column wider than 8 bits, on a set whose record is
         RECORD_STATE, whose quantizer is not NetwQuantizeAngle
FULL     everything else, which is every column on an input set, every
         bool and small integer, every angle, and every unquantized
         float, whose IEEE bit pattern has no meaningful step
```

A column narrower than five bits is `FULL` whatever the declaration says,
because no bucket is narrower than the code it would replace.

**The mode folds into the schema identity.** It changes bytes, so two builds
that disagree about one column's mode must refuse to pair rather than decode
each other's frames as if the selectors were codes.

### 9.4 The row frames

Two shapes, and neither carries a route, a component, a channel or a tick. The
envelope named the route, the channel and the target within the entity before
the payload began, and §9.1's datagram named the tick for every frame in it.
A row frame is its mask, its life and its columns.

**Masked row**, on SYNC_ROW and SYNC_ROW_DELTA:

```text
[life bits 4]              epoch mod 16 of the route's life
[mask bits n]              n = the plan's column count, at least one bit set
[has_ack bool1]
[has_tick bool1]
[has_base bool1]           iff the mask holds a column declared LADDER
[base_low bits 8]          iff has_base. the low byte of the baseline's seq
[ack_delta svarint 5]      iff has_ack. ack = T + ack_delta
[tick_delta svarint 3]     iff has_tick. row tick = T + tick_delta
per column in mask order, per element of stride:
  laddered, and has_base   [sel bits 2] then §9.5's body
  otherwise                [code bits width]
[align_verify]
```

`T` is the datagram's `base_tick - 1`. A reconcile ack is written as a signed
step from `T` rather than as a tick of its own, so a session at tick 100,000
spends the same bits as one at tick 3. A frame on a datagram with no tick
carries no ack, and `has_ack` is `0`.

**The step is signed rather than an age because the ack is not always behind
`T`.** A reconcile ack names a tick the OTHER peer authored, and a peer whose
schedule has drifted from its partner's can acknowledge a tick ahead of the
one it is sending at. Spelling that as an unsigned age has only one outcome
available to it, dropping the ack, and a dropped reconcile ack is invisible:
the sender believes it acknowledged and the receiver simply never hears.

**A row does not always belong to the tick the datagram was built at, and
`tick_delta` is what says so.** A sender that stamps a row authors it FOR a
tick, which is usually the tick after the one whose flush carries it: the
value is written, then the frame goes out on the way to the step that applies
it. The delta is signed and almost always `0` or `1`.

**Whether a row carries a tick is a property of the FRAME and not of the
declared set, which is what `has_tick` pays for.** A set declares a stamp, but
a binding may author a tick the set never declared one for, and prediction's
own state and input lanes do exactly that. Deriving presence from the schema
looked cheaper by a bit and silently dropped the tick off every such row, so
the bit is spent and the sender says what it wrote. A row with no tick reads
back as having none rather than as belonging to the datagram's.

**The field order is load-bearing rather than decorative.** A `varuint`
aligns to a byte before it writes, so a leading `ack_age` would cost the frame
a whole byte before the mask had spent a bit. With `life` and `mask` first, an
eight column row spends 13 bits, which is two bytes with the alignment pad,
and the frame plus its envelope is six bytes against the twelve v9 spent.

`life` is the fence the unreliable lane needs and the reliable one cannot
provide. A data frame gathered against life N rides an unordered lane and can
arrive after the reliable SPAWN that opened life N + 1, carrying values for a
copy of the entity that no longer exists. Four bits distinguish sixteen
consecutive lives, which is more than a lane holds in flight, and a frame
whose life is not the life the wire has most recently named for that route is
refused and counted rather than applied.

A reader refuses `mask = 0`, a mask bit the plan does not declare, a life that
is not the route's, a frame that ends inside a column, and residue. **A
refusal writes none of the row**, so a truncated frame never applies the
columns that happened to arrive before the truncation.

The plain frame is retired. A row carrying every column writes a full mask,
which costs `n` bits and lets the receiver derive wholeness from the mask
instead of from the channel it arrived on.

**Window**, on SYNC_ROW_WINDOW:

```text
[life bits 4]
[count int_range 1..255]
[has_ack bool1]
[ack_delta svarint 5]      iff has_ack
[tick_delta svarint 3]     always. frame tick F = T + tick_delta,
                           F = the newest sample's own tick
per sample, oldest first:
  [sample_age varuint 3]   F - sample.tick
  every column, per element: [code bits width]
[align_verify]
```

**What the framing costs**, measured by the `[D20]` laws in
`tests/repl_row_frame_tests.cpp` rather than quoted, at route below 128 with
no reconcile ack and no stamp:

```text
row shape                             framing  payload  total  engine
-----------------------------------   -------  -------  -----  ------
eight columns, whole mask                   6        1      6      --
one I16 beside one BOOL                     6        2      8      11
Vector3 @ 16 + Quaternion @ 10 +
  Vector3 @ 16, every column whole          6       16     22      60
  the same row, position and velocity
  laddered at a step of three codes         6        9     15      60
```

Framing is the envelope's four bytes plus the row header's two, and it is flat
across every shape above: v9 spent twelve on the same rows. The engine column
is Godot's own `MultiplayerSynchronizer` on the same declaration.

A window frame carries no mask: every sample is whole, which is what makes
the samples comparable to each other. It always writes `tick_delta`, because a
window IS a series of ticks and its newest sample is the one the ages are
measured from; the samples that follow are ages below `F`, never below the
datagram's `T`.

### 9.5 The ladder

A column declared `LADDER` by §9.3, whose element width `w` is five bits or
more, writes each element as a selector and a body:

```text
[sel bits 2]   00  FULL      [code bits w]
               01  step      [zz bits 4]
               10  step      [zz bits 8]
               11  step      [zz bits 16]

zz   = zigzag(code - base_code), so 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3
base_code = the same element of the same column in the baseline row this
            frame named through base_low
```

A bucket is legal only where its width is less than `w`, so a 16-bit column
offers buckets 4 and 8 and never 16. **The encoding is canonical: the smallest
LEGAL bucket that holds `zz`, and `FULL` when none does.** A reader refuses a
bucket that is illegal for the column, a bucket larger than the smallest that
would have held the step it carries, and a step whose sum leaves the column's
code space. All three are the same refusal: a frame that could have been
written a second way was not written by this encoder.

**The frame names the baseline it steps from, and the receiver's own held row
is NOT that baseline.** The sender steps from the row a datagram the receiver
reported DELIVERED carried, which is the only row both peers are known to
agree on. The receiver has since applied frames the sender has not heard about
yet, so its held row has moved. The receiver therefore keeps, per lane, a ring
of the last 64 rows it applied, keyed by the seq of the datagram that carried
each one, and resolves `base_low` against that ring rather than against what
it is holding now.

**`base_low` is eight bits of an absolute seq rather than an age below this
datagram's, because the sender does not know this datagram's seq when it
writes the frame.** The carrier mints a seq at flush, long after the row was
encoded and priced, and one flush can mint more than one seq to a peer when
the buffer overruns, so no peek is exact. An age written against a guessed seq
would resolve, on the receiver, to a REAL row one seq away from the intended
one, which decodes to a wrong value in silence. The low byte of the baseline's
own seq is a number the sender knows exactly. It is unambiguous because the
ring spans at most 64 consecutive seqs, a quarter of what a byte distinguishes:
the receiver reconstructs `base_seq` as the one seq at or below this
datagram's whose low byte is `base_low`, and refuses if the distance exceeds
the ring.

**A frame naming a baseline the receiver does not hold is refused WHOLE and
counted as `row_frames_dropped_baseline`.** There is no heal request and none
is needed: the sticky mask means the next frame the sender sends after an ack
gap carries every column that has moved, and the stale rule below turns that
frame into a complete one. A refusal here is a bug rather than a state to
recover from, which is why it is counted rather than repaired.

**The sender degrades to FULL rather than to a refusal.** It writes `has_base
= 0`, and every element of every laddered column writes its whole code with no
selector at all, whenever it cannot name a baseline it is sure of.

**THE MASKED LANE NAMES NO BASELINE TODAY, AND THE REASON IS MEASURED.** The
BY_SEQ naming above is specified, implemented and held by law, and the send
plane does not use it, because the premise it rests on is false at this
revision: a baseline promoted from a datagram the delivery history reports
DELIVERED is not a row the receiver is known to have APPLIED. Delivered is a
property of the datagram and applied is a property of the lane, and the
receive plane can drop a row out of a datagram it acknowledged.
`[MEASURED 2026-09-12]` over the bomber AI arm on a lossy link, with a
temporary checksum of the baseline carried beside `base_low`, 600 of 925
laddered masked-lane frames stepped from a baseline row the receiver had never
held at ANY seq in its ring. A step from a row the receiver does not have
decodes to a WRONG VALUE, which is worse than every byte the ladder saves, so
the masked lane writes `has_base = 0` until the sender's baseline is promoted
from an APPLIED row rather than a delivered datagram. Two candidate causes were
ruled out by measurement: a baseline the receiver merely no longer holds (zero
occurrences, so the ring's own refusal never fires), and the freshness gate
keying on `(sender, route, channel)` without the set ordinal (widening it to
the ordinal moved the count not at all).

The trigger that turns it on: the receive plane reports, per lane, which rows
it applied, and `BaselineBook` promotes only from those. The window and
reliable lanes are unaffected and ladder today, because neither names a seq:
their baselines are rows both peers hold by construction.

`has_base` is spent only when the mask actually holds a laddered column, so a
plan with no laddered column writes exactly the bytes §9.4 wrote before the
ladder existed, and a laddered plan sending only its unladdered columns pays
nothing either.

**What the ladder is worth, priced offline over two recorded racing corpora.**
Solver triplet only, position at 19 bits and the two velocities at 16, all
three laddered, against the same v10 frame with every column whole:

```text
corpus                       baseline 1 tick back   baseline 4 ticks back
                              whole  ladder  saved   whole  ladder  saved
2026-07-26 codec, two laps     10.8     7.0  35.6%    11.3     7.8  30.6%
2026-07-31 wall grind, gate 11  9.9     6.5  34.4%    10.4     7.6  27.2%
```

B per tick, on the rows whose CODE moved. The two baselines bracket the round
trip: an ack every tick puts the baseline one tick back and a slower link puts
it further, and the ladder is worth less the further back it is because the
value has had longer to move. The 2026-08 per-column census over the same
racing corpus predicted 39 to 44 percent on these three columns alone, and the
gap is the frame header and the quaternion, which both pay nothing. **This is
what the masked lane will be worth when it can name a baseline safely, and it
is not what the live wire spends today**, per the paragraph below.

**The reliable lane names its baseline by ORDER rather than by seq, so it
spends `has_base` and no byte.** A retained row rides an ordered, guaranteed
lane, so the row the receiver holds is the row the sender last sent it, with
no ack and no round trip in between. There is nothing to name: the baseline is
the receiver's own held row, and the bit says only whether the sender has sent
this peer anything yet. A first send to a peer writes `has_base = 0` and every
column whole, which is also what the peer that holds nothing needs. Which of
the two namings a frame uses is a property of the CHANNEL it arrived on and
never of the frame, which is why neither is written down: `SYNC_ROW` names by
seq and `SYNC_ROW_DELTA` names by order.

**A window frame's samples step against each other, and the OLDEST is whole.**
Every sample in a window is present, in a known order, in one frame, so the
sample before is a baseline both peers have by construction and no name is
spent at all. The first sample on the wire is whole and each one after steps
from the sample before it, which is the direction the samples are already
written in. Stepping from the NEWEST instead would buy the same bits and cost
a reversal of §9.4's sample order for nothing.

**A transcribed laddered frame.** Two `I16` columns, both `LADDER`, so `w` is
16 and buckets 4 and 8 are legal while 16 is not. Life 5, whole mask, no
reconcile ack, no stamp, stepping from the baseline carried at a seq whose low
byte is 42. `x` moves 1000 -> 1003, a step of `+3`, so `zz` is 6 and the
4-bit bucket is the smallest that holds it. `y` moves 2000 -> 1995, a step of
`-5`, so `zz` is 9 and the 4-bit bucket holds that too.

```text
bit  0..3    life        5        0101
bit  4..5    mask        3        11
bit  6       has_ack     0
bit  7       has_tick    0
bit  8       has_base    1
bit  9..16   base_low    42       00101010
bit 17..18   sel x       1        01        the 4 bit bucket
bit 19..22   zz x        6        0110      zigzag(+3)
bit 23..24   sel y       1        01
bit 25..28   zz y        9        1001      zigzag(-5)
bit 29..31   pad         0
```

Packed low bit first, the frame is `0x35, 0x55, 0xB2, 0x12`: four bytes where
the same two columns written whole would have spent five. The same row against
an unnamed baseline spends `[life 4][mask 2][has_ack 1][has_tick 1]
[has_base 1][x 16][y 16]`, which is 41 bits and six bytes.

### 9.6 SPAWN, DESPAWN, HIDE and REPARENT

The four existence and presence verbs open the same way and are refused by the
same rules:

```text
[route varuint 5][epoch varuint 3]
```

`epoch` is the life the sender is speaking about. A route's life counts up each
time the authority revives it after death. **A receiver refuses any epoch below
the highest one the wire has named for that route**, because such a frame was
authored before a death the receiver has already been told about, and an
unreliable frame can outlive the reliable one that buried it.

The life a receiver fences against is the one the WIRE declared, never one it
counted for itself. A copy can die locally for reasons the authority never
spoke about, visibility being the common one, and a receiver that counted those
as lives would start refusing the authority's own frames. So a receiver records
the epoch of every verb it admits and compares against that alone, which is
what makes both peers land on the same life.

DESPAWN and HIDE end there. REPARENT continues with one anchor, which is the
node the entity now hangs under.

An **anchor** names a node either against an entity or against the session
root:

```text
[entity_relative bool1]
  1   [route varuint 5][subpath string]
  0   [path string]
```

An entity-relative anchor survives its target being re-parented, because the
route is resolved first and the subpath is walked from the entity's owner. A
root-relative anchor is the fallback for a node no entity owns.

**SPAWN** continues:

```text
[entity_id string]
[peer_id varuint 5]
[controller svarint 5]
[spawn_tick varuint 5]            tick + 1, so 0 is no tick
[requester svarint 5]
[comp_table_hash bits 32]
[declares_scene bool1][scene_label string iff]
[node_name string]
[recipe int_range 0..4]           ADOPT SCENE SPAWNER FN_REGISTRY FN
[parent_is_spawn_target bool1]    set only on SPAWNER
[parent anchor]                   iff parent_is_spawn_target = 0
recipe body
  ADOPT        nothing
  SCENE        [by_uid bool1]
               [uid bits 64 iff by_uid][scene string iff not by_uid]
  SPAWNER      [spawner anchor]
               [scene_index varuint 3]   index + 1, so 0 is custom data
               [custom bytes_capped 4095 iff scene_index = 0]
  FN_REGISTRY  [id string][args bytes_capped 4095]
  FN           [host anchor][method string][args bytes_capped 4095]
[state count varuint 2]
  per entry    [by_path bool1][path string iff]
               [token bytes_capped 1023][values bytes_capped 4095]
[native count varuint 2]
  per entry    [path string][value bytes_capped 4095]
[consumed bytes_capped 4095]
[derived bytes_capped 4095]
[align_verify]
```

`controller` and `requester` are signed, which is §6.2's answer for them: a
peer id of `-1` encoded as a varint read back as `127` and nobody was told.

**The parent anchor is elided when the receiver can derive it.** On a SPAWNER
recipe the parent is almost always the spawner's own `spawn_path` target, and
that target is a node the receiver resolves locally once it has the spawner
anchor, which the recipe body carries anyway. So `parent_is_spawn_target`
replaces the whole anchor with one bit for the common case, and the general
anchor still rides whenever the parent is anything else. The bit is legal
only on SPAWNER, because no other recipe names a spawner to derive from, and
a frame setting it on another recipe is refused.

`[token]`, `[values]`, `[custom]`, `[args]`, `[value]`, `[consumed]` and
`[derived]` are opaque runs: the verb's own grammar delimits them and never
reads inside them, so a run whose contents the receiver cannot parse costs
that one field rather than the rest of the frame. The consumed and derived
runs each hold `[count varuint 2]` then that many `[ordinal varuint 2]
[schema bits 32]` pairs, which §8.5 states.

**A SPAWN is refused whole.** Every field is read through the strict stream,
so a truncation, a non-canonical varuint, a length past the end of the frame,
a recipe outside `0..4`, and residue past `align_verify` each refuse the whole
payload and count `drops_spawn_truncated`. Nothing is applied from a frame
that did not decode to exhaustion. This is the fix `§6.4` names for the verb
that needed it most: the reader this replaced substituted zero for every byte
past the end and raised no flag, so a frame with one extra field applied
in-range garbage to every field after it and only `route <= 0` was guarded.

### 9.7 The clock handshake and the ping pair

The calibration speaks four peer-scoped frames and every one of them is
fixed width, because a sample the frame itself delayed is a sample of the
wrong thing.

```text
CLOCK_HANDSHAKE        [tickrate varuint 2]
CLOCK_HANDSHAKE_REPLY  [tickrate varuint 2]
CLOCK_PING             [origin bits 32]
CLOCK_PONG             [origin bits 32][tick bits 32][phase bits 8]
```

`tickrate` is the sender's own configured rate, bounded at `2^14 - 1` by its
two groups, which is far above any rate a simulation runs at. The client
states its rate in the handshake and the server answers with its own, so a
disagreement is visible to the side that has to act on it.

`origin` is the low 32 bits of the pinging peer's wall clock in microseconds.
The server never interprets it and copies it back into the pong unchanged, so
the round trip is measured against one clock and no skew enters the sample.
The wrap is deliberate: a round trip past 71 minutes is not a sample worth
correcting a clock with.

`tick` is the low 32 bits of the server's session tick and `phase` is its
fraction of the current tick on a 0 to 255 grid, so the pair names a point in
server time to within a 255th of a tick.

**A clock frame is refused whole.** A truncation, a non-canonical `tickrate`
and residue past the declared fields each refuse the payload and leave the
local calibration exactly as it was. This matters more here than the fixed
widths suggest: the reader this replaces decoded a Variant, so a handshake
reply with a trailing byte was accepted and a reply carrying a Variant of the
wrong type was indistinguishable from one carrying no tickrate at all.

### 9.8 The lag compensation denial

```text
LAGCOMP_DENY  [key string]
```

`key` names the effect the server refused, and the receiver discards exactly
that effect's local prediction. A denial that does not decode whole discards
nothing, which is the safe direction: the effect stays until a denial the
receiver can read names it, and the reconciliation that follows corrects it
anyway. Discarding on a key read out of a truncated frame would revert an
effect the server never spoke about.

### 9.9 Control request and control apply

Control is who drives an entity, and the two frames that move it are the
smallest pair on the wire.

```text
CONTROL_REQUEST  (no payload)
CONTROL_APPLY    [controller varuint 5]
```

`CONTROL_REQUEST` carries nothing, because the frame envelope already names
the route being asked for and the datagram already names the peer asking.
A request arriving with any payload at all is refused: the sender that wrote
it is not speaking this grammar.

`CONTROL_APPLY` names the peer that now drives the route, and `0` means the
route is uncontrolled. The frame is broadcast to every peer the entity is
live for rather than to the requester alone, so a peer that watches the
entity learns the change at the same tick as the peer that asked for it.

### 9.10 Interest awareness

One frame carries a batch of awareness edges, because the sweep that raises
them raises them together and a peer entering a layer usually enters several
routes at once.

```text
INTEREST_AWARENESS
[count varuint 2]
per edge
  [observer_scoped bool1]   0 layer, 1 observer
  [route varuint 5]
  [layer_id string]
  [observer varuint 5]
  [entered bool1]           0 exit, 1 enter
[align_verify]
```

A **layer** edge says the route entered or left the named layer and concerns
every peer watching it, so its `observer` is `0`. An **observer** edge says
one named peer gained or lost awareness of the route inside that layer, so
its `observer` is that peer. The two are one frame family because the
receiver does the same thing with both: it waits for the route to be live and
then applies the edge.

**An edge names a route, a layer and exactly one observer scope.** A `route`
of `0`, an empty `layer_id`, an observer edge naming peer `0` and a layer
edge naming any peer at all are each refused, and the refusal takes the whole
frame rather than the edge. A batch is authored by one sweep, so an edge that
does not decode means the sweep and the reader disagree about the grammar,
and the edges around it are no more trustworthy than the one that failed.

### 9.11 The session verbs

Everything a session says about who is in it and what they are looking at
rides route 0 in one of nine shapes. They are one family because one plane
writes and reads all of them, and because the identity every one of them
depends on is settled by the first.

**Joining.**

```text
SESSION_JOIN
[username string]
[args bytes_capped 4095]
[schema_hash bits 64]
[peer_id svarint 5]
[app_tag bits 64]
[wire_identity bits 64]
[schema_identity bits 64]
[align_verify]

SESSION_ACCEPT   one accepted player
[peer_id svarint 5][username string][values bytes_capped 4095]

SESSION_ROSTER   the players already accepted, sent to a late joiner
[count varuint 2][SESSION_ACCEPT body] x count
```

`app_tag`, `wire_identity` and `schema_identity` are the three halves of §9's
identity gate and ride at their full 64 bits, because a masked hash collides.
`[args]` and `[values]` are opaque runs: the verb delimits them and never
reads inside, so the arguments a game declared for its own join handler cost
one field rather than the whole frame when they cannot be parsed.

The roster is one frame rather than one frame per player because it is sent
once, to one peer, at the moment that peer is admitted, and a partial roster
is worse than none: the late joiner would hold a world it believes complete.

**Control.**

```text
SESSION_PAUSE          [reason string]
SESSION_UNPAUSE        (no payload)
SESSION_KICKED         [reason string]
SESSION_SHUTDOWN       [reason string]
SESSION_LEAVE_REQUEST  [reason string]
SESSION_KICK_REQUEST   [peer svarint 5][reason string]
```

A reason is a string a game wrote and a player reads, so it is bounded by the
1023 byte string cap and nothing else. `SESSION_UNPAUSE` carries no payload
for the same reason `CONTROL_REQUEST` does not: there is nothing to say that
the channel has not already said.

**Scenes.**

```text
SESSION_SCENE_REQUEST   [request_id varuint 5][path string][scope svarint 2]
SESSION_SCENE_RESULT    [request_id varuint 5][code svarint 5]
SESSION_SCENE_RELEASED  [route varuint 5]
SESSION_SCENE_SEAT      [route varuint 5][peer svarint 5][present bool]
```

`request_id` pairs a result with the request that opened it, so a client that
asked twice settles the right promise. `code` is an engine `Error` and is
signed because the vocabulary it draws on is.

**Every session verb is refused whole**, and a refused verb changes nothing:
no participant is seated, no roster row is admitted, no promise is settled
and no scene is released. The reader these replace decoded a Variant and
checked its type and its element count, so a frame with a trailing byte was
admitted, a reason that was not a string arrived as the empty string, and a
peer id read out of a Variant of the wrong type arrived as zero.

### 9.12 The table frames

A table is a column store the server publishes and every client mirrors, and
one frame carries a contiguous slice of its rows.

```text
TABLE
[table_id varuint 2]
[schema_hash bits 16]
[tick varuint 5]
[flags bits 8]
[rows varuint 2]
[route varuint 5] x rows
per column, in declaration order, aligned at the column's first bit
  quantized    [code bits width] x rows x stride
  ENTITY       [route varuint 5] x rows x stride
  BOOL         [bit1] x rows x stride
  I8 U8        [bits 8] x rows x stride
  I16 U16      [bits 16] x rows x stride
  otherwise    the column's packed bytes, rows x stride wide
[align_verify]
```

```text
flags
bit0  SNAPSHOT   this frame replaces the table's rows rather than upserting
bit1  REMOVE     the routes are leaving, and no column data follows
```

A column starts byte aligned so its elements can be copied whole for the
types that spend no bits of their own, and a frame's columns are therefore
not packed against each other. That is the one place this format spends
bytes deliberately: a table frame is a bulk carrier and the memcpy is worth
more than the padding.

`table_id` is the sender's ordinal for the table, `0` naming the lifecycle
stream that retires routes across every table at once. `schema_hash` is the
sealed shape of the declared columns, and a frame whose hash is not the one
the receiver sealed is counted `drops_table_schema` and applied to nothing:
the columns after the header would otherwise be read at the wrong widths.

**Freshness is judged inside this decoder.** A table frame rides route 0, and
a route-0 frame never reaches the datagram's per-route freshness book, so the
decoder compares `tick` against the table's own and counts `drops_table_stale`
for a frame older than the reorder window.

**A table frame is refused whole**, and a refused frame upserts nothing,
removes nothing and moves the table's tick not at all. The reader this
replaces read its header twice, zero-filled past the end and checked each
column's remaining bytes by hand, so a frame that ran out mid-column left the
columns before it applied.

### 9.13 The RPC family, its tokens and its values

Five entity-routed channels carry a game's own calls, and they share three
grammars: a **token** naming the member, a **value** carrying one argument,
and a **slot list** carrying the argument row.

**A token is an ordinal or a name.**

```text
[by_id bool1]
  1  [id bits 8]
  0  [name string]
```

A script's members are ordered by their declaration and both peers derive the
same ordinal from the same script, so an ordinal is what the wire spends when
the member is one the script declared. A name is the fallback for a member the
ordering does not reach, and it costs the string.

**A value is quantized or raw, decided per field by the declaration.** Both
ends read the same declaration, so the wire does not say which.

```text
quantized  [code bits width] x stride         no type byte at all
raw        [type int_range 0..5]
  0 FALLBACK  [bytes bytes_capped 65535]      var_to_bytes, the last resort
  1 BOOL      [bool1]
  2 INT       [bits 64]
  3 VECTOR2   [x bits 32][y bits 32]
  4 FLOAT     [bits 32]
  5 VECTOR3   [x bits 32][y bits 32][z bits 32]
```

**A raw float is 32 bits**, so a value a game read as a double arrives as its
`f32` rounding. §5 says why this is the shape rather than an accident.

**A slot list is the argument row.**

```text
[count varuint 2]
per slot  [kind int_range 0..2]
  0 RAW        [value, raw]
  1 QUANTIZED  [value, through the declared quantizer]
  2 NODE_REF   [route varuint 5][comp bits 8]
               [path string iff comp = 255]
```

A `NODE_REF` slot addresses a node rather than carrying one. An `Object` has
no value encoding at all, and a call that passes a node passes its address:
route, component ordinal, and a relative path when the ordinal is `255`.

**The five payloads.**

```text
CALL           [flags bits 8]              bit0 bit1 target, bit2 awaits reply
               [txn varuint 5]             iff bit2
               [target bits 32]            iff target = 2
               [method token][args slots][align_verify]
REPLY          [txn varuint 5][value slots][align_verify]
SIGNAL         [signal token][args slots][align_verify]
PROPERTY_SYNC  [property token][values slots][align_verify]
ACTION         [method string][view_tick svarint 5]
               [data bytes_capped 4095][key string]
               [timing int_range 0..3][align_verify]
```

`flags` bits 0 and 1 name who the call is addressed to: `0` everyone the
entity is live for, `1` the server, `2` the peer named by `target`. Bit 2 says
the sender opened a transaction and is waiting, and `txn` is the number the
reply comes back under.

**A call frame is refused whole**, and a refused frame calls nothing. This is
the channel where that matters most, because the reader it replaces read a
count byte and then decoded that many values out of whatever followed: a
truncated argument row invoked the method with the arguments that did parse
and a zero for the rest, which a game cannot tell from a call it was sent.

### 9.14 The property sync frames and the descriptor runs

A consumed `MultiplayerSynchronizer` speaks two channels. `SYNC` carries the
whole property row on the synchronizer's replication interval, and
`SYNC_DELTA` carries the subset that changed since the peer receiving it last
acknowledged one, on the delta interval. Both address the synchronizer by its
ordinal within the route rather than by name.

```text
SYNC        [ordinal varuint 5][flags bits 8][values slots][align_verify]
SYNC_DELTA  [ordinal varuint 5][mask varuint 10][values slots][align_verify]
```

`values slots` is the §9.13 slot list, with no quantizer and no declared type
on either side, so every value spends its raw type selector. A stock
synchronizer's replication config names properties by `NodePath` and says
nothing about their types, which is why this row is a value list rather than
a packed column row: there is no declared width to pack it into.

`ordinal` is the sender's index for the synchronizer among the consumed rows
of that route, and both peers derive it from the same config, which is what
`schema_hash` in the descriptor run below fences.

`flags` gates extensions this format version does not yet spell. A receiver
refuses a frame whose `flags` it does not implement and counts
`drops_sync_unknown_flag`, rather than reading the row that follows under a
grammar it cannot know.

`mask` is one bit per watched property in declaration order, and the slot
list that follows carries exactly the set bits, in the same order. A mask
naming a different number of properties than the slot list carries is a
refusal rather than a partial apply, which is what poisons the row: the two
peers disagree about the config and every later frame would apply the wrong
value to the wrong property. A watch set is capped at 64 properties because
the mask is a `u64`.

**The descriptor run** is how a receiver learns which ordinals a route's
SPAWN declared and what schema each of them agreed to.

```text
[count varuint 2]
per row  [ordinal varuint 5][schema_hash bits 32]
```

Two of these ride inside a SPAWN as §9.6 `bytes_capped` blobs, one for the
consumed synchronizers and one for the derived property sets, and each is
refused whole and to exhaustion inside its own blob. `schema_hash` carries
all thirty-two bits of the fingerprint, so two configs agreeing in their low
sixteen stay distinguishable, and a receiver whose own fingerprint disagrees
poisons that row at its first frame instead of applying values positionally
against a config it does not share.

### 9.15 The capture file

A `.netwcap` file is every datagram a peer wrote or read, in the order it
happened, beside enough of the build's own description that the bytes decode
with no engine running. `tools/wire_decode.py --capture <path>` is that
decoder and it shares no code with the encoder that wrote the file.

```text
line 1   a JSON object, then one newline
after    capture records back to back, to the end of the file
```

The header line names the format and the build:

```text
┠╴format      the v10 format version, an integer
┠╴identity    the channel table's 63 bit fold, the number a JOIN carries
┠╴peer        the unique id this peer held when its first datagram moved
┠╴started     the process wall clock, in milliseconds, at that same moment
┠╴channels    one row per registered channel: id, name, kind, reliability,
┃             freshness, delivery, direction, payload
┠╴reserved    the channel ids this build holds back
┠╴records     every describe() layout in the build, by record name, as the
┃             field list tools/wire_decode.py reads
┖╴schemas     one row per sealed schema: name, shape_hash, and its compiled
              column widths and strides, which is what a row frame's mask
              indexes and what no describe() layout can carry
```

A capture record is byte aligned, length prefixed and independently
refusable, exactly as a frame is:

```text
[dir bits 8][peer varuint 5][wall_ms varuint 10][tick varuint 5]
[bytes bytes_capped 65535]
```

A capture is the only honest count of what crossed the link. The session's
own `sent_bytes` counts the frame payload a datagram carried and not the
datagram it carried it in, so it undercounts the wire by every carrier
header. `[MEASURED 2026-09-12]` on a 30 second healthy two-peer run the
host wrote 432,729 bytes and counted 388,876 of them.

`dir` is `0` for a datagram this peer sent and `1` for one it read. `peer` is
the peer at the other end, `0` for a broadcast. `wall_ms` is the milliseconds
since the header's `started`, so a capture reads without knowing when the
process began. `tick` carries the session tick plus one, and `0` means the
clock had not started, which is the convention §9.1's `base_tick` already
uses. `bytes` is the whole datagram, its own header included, so the reader
of a capture and the reader of a socket see identical bytes.

The cap of 65535 is the record's, not the wire's. A datagram larger than that
is dropped from the capture and counted rather than truncated into it, because
a truncated datagram decodes to a lie while a missing one is a number.

`NETW_CAPTURE=<path>` arms the writer. The file opens on the first datagram
the peer moves rather than when the session opens, because a peer's unique id
is assigned during bring-up and a header written ahead of it names zero.

**`schemas` names what was sealed when the FIRST datagram moved.** A peer
that seals on spawn rather than before bring-up writes an empty sheet, which
is what a joining client does. The sheet the row frames need is the sender's,
and the sender of a row is the peer that sealed before it opened, so the
capture that carries the rows carries the sheet that reads them.

**A capture is one peer's datagrams and the armed path is claimed once per
process.** The first session to move a datagram takes the path and every
later session in the same process captures nothing, because two sessions
writing one file interleave their records into bytes that decode to neither
of them. A process that wants two peers captured runs two processes, which
is what a peer already is.
