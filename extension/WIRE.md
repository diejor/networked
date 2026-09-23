# WIRE v11, the carrier format

The byte layout of every datagram, frame and payload Networked writes.
`FORMAT_VERSION` is 11. `src/wire/registry.cpp` is the one declaration of the
channel table and `tools/wire_decode.py` reads a capture against this page.

---

## 1. Primitives

Every field streams through `WriteStream` and `ReadStream` bit by bit, little
endian. Within a byte the first bit written is bit 0, and a value wider than
the space left continues into the next byte from its own low end.

```text
bits(v, w)          w bits, 0..64, low bit first. a bit set above w poisons
                    the stream
int_range(v, a, b)  bits(v - a, ceil(log2(b - a + 1))). a range of one value
                    spends no bits
bool1(f)            1 bit
varuint(v, n)       byte aligns, then base-128 groups, seven value bits low
                    first, top bit set on every group but the last, at most n
                    groups
svarint(v, n)       varuint of zigzag(v), (v << 1) ^ (v >> 63)
bytes_capped(d, c)  int_range(len, 0, c), align_verify, then the raw bytes
align_verify()      flushes to the next byte boundary and verifies alignment
string              bytes_capped(utf8, 1023)
```

A varuint is canonical. A redundant final group is refused, and a value too
large for the declared groups produces no frame at all.

A decoder that stops before its input is exhausted has not decoded. Residue is
a refusal and so is a read past the end.

A quantized value spends its quantizer's bits and no type tag, because both
ends read the same declaration. `NetwQuantizeScalar` spends `bit_count` bits
per axis over `top = 2 ** bit_count - 2` levels, so `code = round(unit * top)`
and `value = min + code / top * (max - min)`. Both limits and the midpoint are
exactly representable and re-encoding a decoded value is idempotent.

---

## 2. Datagram

Three shapes, selected by the first byte.

```text
0x58  reliable          [magic bits 8][base_tick varuint 5]
0x78  unreliable        [magic bits 8][seq bits 16][base_tick varuint 5]
0x98  unreliable acked  [magic bits 8][seq bits 16][ack bits 16]
                        [history bits 32][base_tick varuint 5]
```

Frames follow the header to the end of the datagram, each byte aligned, and
the header ends byte aligned.

```text
base_tick  the session tick plus one, 0 while the clock is unconfigured. every
           frame in the datagram was gathered at T = base_tick - 1 and spends
           an age below it
seq        counted per destination peer
ack        the freshest inbound seq this sender has seen from this peer
history    bit i is set when inbound seq ack - 1 - i was seen, so one echo
           reports 33 datagrams. an unseen bit and a dropped bit read the same
```

The retired magics `0x4E`, `0x6E`, `0x8E`, `0x56`, `0x76`, `0x96`, `0x57`,
`0x77` and `0x97` answer MALFORMED. Any other first byte is FOREIGN and
belongs to whatever else shares the transport.

A packet too short for the header its magic claims is MALFORMED, and so is a
`base_tick` that is not a canonical varuint or that overruns the packet. A
refused header yields no seq, no ack and no payload offset.

---

## 3. Frame envelope

```text
[route varuint 5][comp bits 8][channel bits 8][len varuint 3][payload]
```

```text
route  the entity. 0 is peer scoped and names the session
comp   0 the entity root, 1..254 the hydrated component table, 255 an
       unmapped node addressed by relative path
len    payload bytes, not the frame's
```

`comp == 255` wraps the payload as `[path string][inner]`.

On SYNC_ROW, SYNC_ROW_DELTA and SYNC_ROW_WINDOW, `comp` is the ordinal of the
property set within the route rather than a node address. Every other channel
reads it as the node address above.

The payload is exactly `len` bytes and the envelope never reads inside it, so
a datagram walk splits correctly around a payload it cannot parse.

A frame is refused whole. A header ending before `len`, a length past the
remaining bytes, a non-canonical varuint, a path overrunning its body and
non-zero alignment padding each end the walk. Whole frames read before the
refusal survive it.

---

## 4. Channels

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
42   SESSION_SCENE_VIEWERS   session R   -     fit -   srv -> cli planned
43   ROW_CONTROL             session R   -     fit -   either     planned
100+ user channels           routed  -   -     -   -   either     raw
```

```text
kind     session is route 0, routed names an entity, keyed names an entity
         and a target within it
rel      R reliable, U unreliable, UA unreliable with an echo owed
fresh    gated per (route, sender, channel) against a u16 half window
del      fit joins the peer's run, now leaves immediately
agg      the tick flush batches this channel
payload  raw is bytes the core never reads, planned is a fixed layout, delta
         is a row against a baseline
```

Every axis of the table folds into the identity hash two peers exchange at
admission, so a build disagreeing about any cell cannot pair.

A channel also carries a `payload_revision`, folded into identity when
non-zero. SYNC_ROW, SYNC_ROW_DELTA and SYNC_ROW_WINDOW declare 1 for the v11
row contract, SESSION_ACCEPT and SESSION_ROSTER declare 1 for the membership
row of section 16, SPAWN declares 1 for the decision the header of section 11
carries, CONTROL_REQUEST and CONTROL_APPLY declare 1 for the layouts of
section 14, ROW_CONTROL declares 1 for the tenure its OPEN of section 10
carries, and every other revision is 0.

Ids 0, 1 and 7 were claimed by formats older than v8 and stay reserved, so an
ancient frame is refused instead of decoding as a modern channel.

`DESPAWN` and `HIDE` carry the same `varint route` and mean different things.
DESPAWN says the entity ceased to exist, reaches every peer holding a copy and
tombstones the route. HIDE says this one peer no longer holds a copy, reaches
that peer alone, and is reversible by a later SPAWN onto the same identity.

TABLE judges its own freshness in its decoder, because a route-0 frame never
reaches the datagram's per-route book.

---

## 5. Columns and delta mode

A column is an element width and a stride, and no element exceeds 64 bits. An
unquantized composite compiles to its scalar element repeated, so `VECTOR2` is
32 bits by 2, `VECTOR3` is 32 by 3, and `VECTOR4`, `COLOR` and `QUATERNION`
are 32 by 4. A declared stride multiplies that count. A quantized column takes
its quantizer's width and stride instead, and a layout whose parts do not
share one width declares a stride of one and packs its parts into that single
code, which is what `NetwQuantizeQuaternion` does.

An `ENTITY` column is 32 bits and carries `route + 1`, so `0` is null.

A schema declares at most 64 columns, because a row's mask is a `u64`. The
refusal is at seal.

Every column declares one of two delta modes before the session opens.

```text
FULL    the column's whole code
LADDER  a per-element selector and either the whole code or a signed step
```

`NetwPropertySetColumn.delta_mode` is where a game says so, and its default
derives from the declaration.

```text
LADDER   a quantized column wider than 8 bits, on a set whose record is
         RECORD_STATE, whose quantizer is not NetwQuantizeAngle
FULL     everything else, so every column on an input set, every bool and
         small integer, every angle, and every unquantized float
```

A column narrower than five bits is FULL whatever the declaration says. The
mode folds into the schema identity, because it changes bytes.

---

## 6. The ladder

A column declared LADDER whose element width `w` is five bits or more writes
each element as a selector and a body.

```text
[sel bits 2]   00  FULL      [code bits w]
               01  step      [zz bits 4]
               10  step      [zz bits 8]
               11  step      [zz bits 16]

zz = zigzag(code - base_code), so 0 -> 0, -1 -> 1, 1 -> 2, -2 -> 3
```

`base_code` is the same element of the same column in the row the frame's
`distance` names (section 8).

A bucket is legal only where its width is less than `w`, so a 16-bit column
offers buckets 4 and 8 and never 16. The encoding is canonical, the smallest
legal bucket that holds `zz` and FULL when none does. A reader refuses an
illegal bucket, a bucket larger than the smallest that would have held its
step, and a step whose sum leaves the column's code space.

A window frame's samples step against each other. The oldest sample on the
wire is whole and each one after steps from the sample before it, so no
baseline is named at all.

---

## 7. Streams

A stream is one directed `(peer, route, set ordinal, lane family)` under one
schema, one entity lifetime, one writer and one connection incarnation.

```text
token      uint64, minted by the receiver, unique for the process lifetime
revision   uint64, counted by the sender inside one token, first value 1
family     0 volatile, 1 retained, 2 window
```

Revision 0 is reserved and means none. A revision never wraps inside a stream,
and exhaustion closes the stream. A revision reserved for a candidate that
fitting then declines is burned, so a gap forces an absolute row and nothing
else.

A token is looked up under the connection that sent it. Two peers may hold
streams for the same route in opposite directions, and the lookup direction is
what tells them apart.

---

## 8. The state row

On SYNC_ROW and SYNC_ROW_DELTA.

```text
[token varuint 10]
[revision varuint 10]
[distance bits 6]          0 full, 1..32 delta, 33..63 refused
[has_ack bool1]
[has_tick bool1]
[mask bits n]              iff distance is nonzero. n = the plan's columns
[ack_delta svarint 5]      iff has_ack. ack = T + ack_delta
[tick_delta svarint 3]     iff has_tick. row tick = T + tick_delta
per column in mask order, per element of stride:
  laddered, and distance nonzero   [sel bits 2] then section 6's body
  otherwise                        [code bits width]
[align_verify]
```

`T` is the datagram's `base_tick - 1`.

`distance` names `revision - distance` in this exact stream. A distance of 0 is
a full row, which carries every column and writes no mask. There is no partial
full row.

The reconcile ack is a signed step from `T`, because a peer whose schedule has
drifted can acknowledge a tick ahead of the one it is sending at. A frame on a
datagram with no tick carries no ack.

`has_tick` is spent because a binding may author a tick the set never declared
one for, which prediction's state and input lanes do. A row with no tick reads
back as having none.

A zero mask is legal. It names a new revision whose complete image equals its
baseline, which is what lets a lane that returned to an already confirmed
value emit a row instead of falling silent.

Decode copies the baseline and replaces every masked column. It never reads
the row the receiver currently holds, so an accepted row is the exact complete
row the sender staged.

A reader refuses a mask bit the plan does not declare, a distance above 32, a
distance the revision cannot subtract, a frame ending inside a column, and
residue. A refusal writes none of the row.

---

## 9. The window row

On SYNC_ROW_WINDOW.

```text
[token varuint 10]
[revision varuint 10]
[count int_range 1..255]
[has_ack bool1]
[ack_delta svarint 5]      iff has_ack
[tick_delta svarint 3]     always. frame tick F = T + tick_delta
per sample, oldest first:
  [sample_age varuint 3]   F - sample.tick
  every column, per element: [code bits width]
[align_verify]
```

Every sample is whole and the ages are measured below `F`. The revision orders
a window against the other windows of its own stream and buys nothing else.

---

## 10. ROW_CONTROL, channel 43

One complete record per frame, tagged by its first byte.

```text
[tag bits 8]
0 OPEN    [request varuint 10][route varuint 5][ordinal bits 8]
          [family bits 8][epoch varuint 3][tenure varuint 5][schema bits 32]
1 READY   [request varuint 10][token varuint 10]
2 ACCEPT  [count int_range 1..32]
          then count entries of [token varuint 10][revision varuint 10]
3 RESET   [request varuint 10][token varuint 10]
4 CLOSE   [token varuint 10]
[align_verify]
```

`route`, `ordinal` and `epoch` are spelled as section 3 and section 11 spell
them, so `ordinal` is the envelope's `comp` width. `schema` is the compiled
shape hash folded to 32 bits.

OPEN travels from the writer and carries the request id it minted. READY and
RESET travel back from the receiver naming that same request id, so a
cancellation arriving before READY strands nobody. CLOSE travels from the
writer and names only the token.

`tenure` is the control revision at which the entity's current controller
began, as section 14 carries it. It binds a lane for an input or broadcast
record to that interval. The receiver seats such an OPEN only when its own
tenure is equal and the sender authors the record under that decision. An OPEN
for a newer tenure parks until the decision arrives, and a park older than the
stream repair interval is answered with RESET and token 0 so the writer opens
again. An OPEN for an older tenure is counted and dropped. Installing a
decision that moves the tenure closes every lane of an older tenure for that
route, and the writer that lost authorship closes its lanes and sends CLOSE.
A state lane writes the tenure it has and the receiver ignores it.

A tag above 4, a family above 2, a count outside 1..32, an entry that ends
early and any residue refuse the record. A malformed record installs no stream
and promotes no receipt. An unknown token is counted and ignored, never
answered.

ACCEPT means the complete row committed to the configured consumer under this
token at this revision, and it is emitted after the commit. Receipts coalesce
to the latest accepted revision per stream and flush at most every 50ms per
peer. A record holds at most 32 entries and at most 256 encoded payload bytes,
splitting at whichever limit it reaches first. At most 256 control payload
bytes per peer per physics tick are reserved inside the governor's budget, and
unused reservation returns to state traffic.

Reliable head-of-line delay can age a confirmed revision past the 32 the
distance field allows, and the sender then writes a full row.

---

## 11. SPAWN, DESPAWN, HIDE and REPARENT

The four existence verbs open the same way.

```text
[route varuint 5][epoch varuint 3]
```

`epoch` is the life the sender is speaking about, counting up each time the
authority revives the route. A receiver refuses any epoch below the highest
one the wire has named for that route, and fences against the life the wire
declared rather than one it counted for itself.

DESPAWN and HIDE end there. REPARENT continues with one anchor.

```text
anchor
[entity_relative bool1]
  1   [route varuint 5][subpath string]
  0   [path string]
```

An entity-relative anchor survives its target being re-parented. A
root-relative anchor is the fallback for a node no entity owns.

```text
SPAWN
[entity_id string]
[peer_id varuint 5]
[controller svarint 5]
[control_revision varuint 5]
[control_tenure varuint 5]
[spawn_tick varuint 5]            tick + 1, so 0 is no tick
[requester svarint 5]
[comp_table_hash bits 32]
[control_hold bits 2]             NONE YIELDABLE EXCLUSIVE
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

`controller` and `requester` are signed, because a peer id of `-1` is a value
they carry.

`control_revision`, `control_tenure` and `control_hold` are the live decision
at the moment the frame is encoded, so a peer that joins late starts on the
decision every holder already has. A CONTROL_APPLY at or below that revision
is discarded when it reaches the joiner.

`parent_is_spawn_target` replaces the parent anchor with one bit when the
parent is the spawner's own `spawn_path` target, which the receiver resolves
from the spawner anchor the recipe body already carries. The bit is legal only
on SPAWNER.

`[token]`, `[values]`, `[custom]`, `[args]`, `[value]`, `[consumed]` and
`[derived]` are opaque runs. The verb delimits them and never reads inside, so
an unparseable run costs one field. The consumed and derived runs carry the
descriptor rows of section 19.

A SPAWN is refused whole and counts `drops_spawn_truncated`. Nothing is
applied from a frame that did not decode to exhaustion.

---

## 12. The clock handshake and the ping pair

Four peer-scoped frames, every one fixed width.

```text
CLOCK_HANDSHAKE        [tickrate varuint 2]
CLOCK_HANDSHAKE_REPLY  [tickrate varuint 2]
CLOCK_PING             [origin bits 32]
CLOCK_PONG             [origin bits 32][tick bits 32][phase bits 8]
```

```text
tickrate  the sender's own configured rate, bounded at 2^14 - 1
origin    the low 32 bits of the pinging peer's wall clock in microseconds.
          the server copies it back unchanged, so the round trip is measured
          against one clock. the wrap is at 71 minutes
tick      the low 32 bits of the server's session tick
phase     its fraction of the current tick on a 0..255 grid
```

A clock frame is refused whole and leaves the local calibration as it was.

---

## 13. The lag compensation denial

```text
LAGCOMP_DENY  [key string]
```

`key` names the effect the server refused, and the receiver discards exactly
that effect's local prediction. A denial that does not decode whole discards
nothing.

---

## 14. Control request and control apply

```text
CONTROL_REQUEST
[op varuint 5]                    minted by the issuer, per entity
[observed_revision varuint 5]     the decision the issuer held
[issued_tick varuint 5]
[source_route varuint 5]          0, or the entity a contact claim came from
[successor varuint 5]             a release's successor, 0 the session
[kind bits 1]                     0 request, 1 release
[hold bits 2]                     NONE YIELDABLE EXCLUSIVE
[final_state bytes_capped 1024]   a release's last state, empty otherwise
[align_verify]

CONTROL_APPLY
[controller varuint 5]            0 is the session
[revision varuint 5]              +1 per decision the coordinator makes
[op varuint 5]                    the issuer's op, 0 on every other copy
[tenure_changed bool1]            the tenure starts at this revision
[hold bits 2]
[outcome bits 2]                  0 granted, 1 unauthorized, 2 unavailable
[final_state bytes_capped 1024]   the new tenure's first sample, or empty
[align_verify]
```

The envelope already names the route and the datagram already names the peer
asking. A peer's requests travel one reliable ordered channel, so the
coordinator decides them in the order they were issued, across entities too.

`CONTROL_APPLY` is admitted only from the session authority and is broadcast
to every peer the entity is live for. The issuer's copy also carries its `op`
and the `outcome`. A refusal reaches only the issuer and restates the current
decision at its current revision. A receiver installs a decision only when its
revision is newer than the one it holds, and settles the op either way. An
apply for a route the receiver has not spawned waits for the spawn.

A `final_state` is one absolute row of every `.broadcast()` record the entity
carries, in the entity's record order. It is left empty when it would pass
1024 bytes.

```text
[tick+1 varuint]                  the releaser's tick, 0 when it had none
[records varuint]
per record
  [row bytes_capped 1024]         the call argument encoding of every column,
                                  in column order, under its quantizer
```

---

## 15. Interest awareness

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

A layer edge concerns every peer watching the route and its `observer` is 0.
An observer edge names the one peer that gained or lost awareness.

A route of 0, an empty `layer_id`, an observer edge naming peer 0 and a layer
edge naming any peer are each refused, and the refusal takes the whole frame
rather than the edge.

---

## 16. The session verbs

Everything a session says about who is in it and what they are looking at
rides route 0.

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

SESSION_ACCEPT   one accepted membership
[peer_id svarint 5][username string][membership varuint 5]

SESSION_ROSTER   the memberships already accepted, sent to a late joiner
[count varuint 2][SESSION_ACCEPT body] x count
```

`app_tag`, `wire_identity` and `schema_identity` ride at their full 64 bits,
because a masked hash collides. `[args]` is an opaque run.

Only the server that decoded `[args]` sees those values. The accept is
broadcast and the roster is handed to a late joiner, so the accept names the
membership and nothing else the request carried.

`membership` is the incarnation the server issued for this acceptance, rising
once per acceptance within a session. It is what names a participant, because
a transport peer id is reused and a scene row naming an old membership cannot
be applied to the new player. Nothing persists it and nothing outside the
session reads it.

The roster is one frame, because a partial roster would leave the late joiner
holding a world it believes complete.

```text
SESSION_PAUSE          [reason string]
SESSION_UNPAUSE        (no payload)
SESSION_KICKED         [reason string]
SESSION_SHUTDOWN       [reason string]
SESSION_LEAVE_REQUEST  [reason string]
SESSION_KICK_REQUEST   [peer svarint 5][reason string]
```

A reason is a string a game wrote and a player reads, bounded by the 1023 byte
string cap.

```text
SESSION_SCENE_REQUEST   [request_id varuint 5][path string][scope svarint 2]
SESSION_SCENE_RESULT    [request_id varuint 5][code svarint 5]
SESSION_SCENE_RELEASED  [route varuint 5]
SESSION_SCENE_VIEWERS   the whole viewer roster of one scene
[route varuint 5][epoch varuint 5][generation varuint 5][revision varuint 5]
[count varuint 2][membership varuint 5] x count
```

`request_id` pairs a result with the request that opened it. `code` is an
engine `Error` and is signed.

`SESSION_SCENE_VIEWERS` is a complete roster and never a change, so a receiver
replaces that scene's roster with exactly what the frame carried. A peer may
view several scenes at once, and a row naming one scene says nothing about
another. Three numbers decide whether a snapshot may be applied.

```text
generation   rises when this session comes online, so a snapshot minted
             before a rehost names memberships that no longer exist
epoch        the route's lifecycle epoch, so a scene retired and reopened at
             the same route does not inherit the old viewers
revision     rises once per publication of one scene, so a snapshot no newer
             than the one already applied is dropped
```

A snapshot for a scene the receiver does not hold yet is parked by route,
keeping one pending snapshot and replacing it when a newer revision arrives.
It is applied when that scene goes live and discarded when it retires.

A membership the receiver does not know is skipped, because the accept that
names it is reliable and arrives. `count` is capped at 1023, and a server
whose scene exceeds that publishes no roster for it and reports the fault.

Every session verb is refused whole. A refused verb seats no participant,
admits no roster row, settles no promise and releases no scene.

---

## 17. The table frames

A table is a column store the server publishes and every client mirrors.

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

A column starts byte aligned so its elements can be copied whole, which is the
one place this format spends bytes deliberately.

`table_id` is the sender's ordinal, `0` naming the lifecycle stream that
retires routes across every table at once. `schema_hash` is the sealed shape
of the declared columns, and a frame whose hash is not the one the receiver
sealed counts `drops_table_schema` and applies to nothing.

Freshness is judged in this decoder against the table's own tick, counting
`drops_table_stale` for a frame older than the reorder window.

A table frame is refused whole. A refused frame upserts nothing, removes
nothing and moves the table's tick not at all.

---

## 18. The RPC family, its tokens and its values

Five entity-routed channels share three grammars.

```text
token
[by_id bool1]
  1  [id bits 8]
  0  [name string]
```

A script's members are ordered by their declaration and both peers derive the
same ordinal. A name is the fallback for a member the ordering does not reach.

```text
value
quantized  [code bits width] x stride         no type byte at all
raw        [type int_range 0..5]
  0 FALLBACK  [bytes bytes_capped 65535]      var_to_bytes
  1 BOOL      [bool1]
  2 INT       [bits 64]
  3 VECTOR2   [x bits 32][y bits 32]
  4 FLOAT     [bits 32]
  5 VECTOR3   [x bits 32][y bits 32][z bits 32]
```

A raw float is 32 bits, so a value a game read as a double arrives as its
`f32` rounding.

```text
slot list
[count varuint 2]
per slot  [kind int_range 0..2]
  0 RAW        [value, raw]
  1 QUANTIZED  [value, through the declared quantizer]
  2 NODE_REF   [route varuint 5][comp bits 8]
               [path string iff comp = 255]
```

An `Object` has no value encoding, so a call that passes a node passes its
address.

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

`flags` bits 0 and 1 name who the call is addressed to, `0` everyone the
entity is live for, `1` the server and `2` the peer named by `target`. Bit 2
says the sender opened a transaction, and `txn` is the number the reply comes
back under.

A call frame is refused whole, and a refused frame calls nothing.

---

## 19. Property sync and the descriptor runs

A consumed `MultiplayerSynchronizer` speaks two channels, both addressing the
synchronizer by its ordinal within the route.

```text
SYNC        [ordinal varuint 5][flags bits 8][values slots][align_verify]
SYNC_DELTA  [ordinal varuint 5][mask varuint 10][values slots][align_verify]
```

`values slots` is the section 18 slot list with no quantizer and no declared
type, so every value spends its raw type selector. A stock synchronizer names
properties by `NodePath` and says nothing about their types, so there is no
declared width to pack into.

`flags` gates extensions this format version does not spell. A receiver
refuses a frame whose `flags` it does not implement and counts
`drops_sync_unknown_flag`.

`mask` is one bit per watched property in declaration order, and the slot list
carries exactly the set bits in the same order. A mask naming a different
number of properties than the slot list carries is a refusal. A watch set is
capped at 64 properties because the mask is a `u64`.

```text
descriptor run
[count varuint 2]
per row  [ordinal varuint 5][schema_hash bits 32]
```

Two of these ride inside a SPAWN as the `consumed` and `derived` blobs of
section 11, each refused whole and to exhaustion inside its own blob.
`schema_hash` is the full `String::hash` of the schema's canonical
description, `NetwPropertySet::wire_hash` for a derived set and the joined
sync and watch path list for a consumed one. All thirty-two bits ride, so two
configs agreeing in their low sixteen stay distinguishable.

A described route admits only the ordinals its description names. A count of
zero still describes the route, saying the sender holds no such set there. A
route no SPAWN described is unconstrained.

---

## 20. The prediction channels

Prediction's lanes carry no version byte, because identity is settled before
any of them is admitted. Every variable section is bounded by the count in its
typed header. Both data lanes are window-redundant, so every send repeats the
range still in flight, floored by what the other side has confirmed.

```text
PREDICT_COMMAND (35, U, owner -> server)
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

A fresh transition and its row are inseparable, so a frame that lost one is
refused whole. The payload plan is the input set's `VOLATILE` columns as a
sealed `SchemaRecord`.

```text
PREDICT_ACK (36, U, server -> owner)
header       u8 epoch, varuint base, u8 count
per record   u8 mask, then pre, command, environment, post, topology and
             witness as 32 bits each, three pre families, three post
             families, a raw fingerprint where the mask claims one, and u8
             flags, whose bits 2..4 carry authority's witness class
```

At most 21 records ride, so a worst-case raw frame stays under the 1150 byte
unreliable budget. Later records ride the next send once `ack_of_acks`
advances.

`PREDICT_RELAY` (37, U, server -> subscriber) is the admitted PREDICT_COMMAND
bytes re-emitted unchanged, so a subscriber decodes the author's own frame.

`PREDICT_RELAY_REQUEST` (38, R, client -> server) is one boolean bit aligned
to a single byte.

---

## 21. The capture file

A `.netwcap` file is every datagram a peer wrote or read, in order, beside
enough of the build's own description that the bytes decode with no engine
running. `tools/wire_decode.py --capture <path>` is that decoder and it shares
no code with the encoder.

```text
line 1   a JSON object, then one newline
after    capture records back to back, to the end of the file
```

```text
header
┠╴format      the format version, an integer, 11 on this branch
┠╴identity    the channel table's 63 bit fold, the number a JOIN carries
┠╴peer        the unique id this peer held when its first datagram moved
┠╴started     the process wall clock, in milliseconds, at that same moment
┠╴channels    one row per registered channel: id, name, kind, reliability,
┃             freshness, delivery, direction, payload
┠╴reserved    the channel ids this build holds back
┠╴records     every describe() layout in the build, by record name
┖╴schemas     one row per sealed schema: name, shape_hash, and its compiled
              column widths and strides
```

```text
record
[dir bits 8][peer varuint 5][wall_ms varuint 10][tick varuint 5]
[bytes bytes_capped 65535]
```

```text
dir      0 a datagram this peer sent, 1 one it read
peer     the peer at the other end, 0 for a broadcast
wall_ms  milliseconds since the header's started
tick     the session tick plus one, 0 while the clock had not started
bytes    the whole datagram, its own header included
```

A datagram larger than the 65535 byte record cap is dropped from the capture
and counted rather than truncated into it.

`NETW_CAPTURE=<path>` arms the writer. The file opens on the first datagram
the peer moves, because a peer's unique id is assigned during bring-up.

`schemas` names what was sealed when the first datagram moved, so a peer that
seals on spawn writes an empty sheet. A capture with no sheet still decodes
its datagrams, its frame envelopes and every channel's byte account. What it
cannot do is decompose a row into columns, because a mask indexes a compiled
column list no `describe()` layout carries.

`format` decides how the first byte of each datagram is read and nothing else
in the file depends on it, so `tools/wire_decode.py` reads a v10 capture and a
v11 capture without being told which it holds.

The armed path is claimed once per process by the first session to move a
datagram. A process that wants two peers captured runs two processes.

---

## 22. Transcribed frames

Over a three column plan of `I16`, `I16`, `I16`, token 7.

Full row at revision 1, columns 11, 22 and 33.

```text
07        token 7
01        revision 1
00        distance 0, has_ack 0, has_tick 0
0B 00     column 0 at 11
16 00     column 1 at 22
21 00     column 2 at 33
```

Delta at revision 4, distance 1, mask `0b101`, tick delta 1.

```text
07        token 7
04        revision 4
81        distance 1 in bits 0..5, has_ack 0, has_tick 1 in bit 7
05        mask 0b101 in bits 0..2, then the pad the svarint aligns over
02        tick_delta 1, zigzagged
0B 00     column 0 at 11
21 00     column 2 at 33
```

Zero mask delta at revision 3, distance 2. Four bytes, no columns.

```text
07 03 02 00
```

A window at revision 2 over a single `I16` column, two samples, the older one
tick behind the newer.

```text
07        token 7
02        revision 2
01        count 2, written as 1 in the 8 bits the range 1..255 spends
00        has_ack 0, then the pad the svarint aligns over
00        tick_delta 0
01 0B 00  sample_age 1, column at 11
00 16 00  sample_age 0, column at 22
```

READY for request 300 and token 7.

```text
01 AC 02 07
```

OPEN for request 1, route 300, ordinal 2, family 0, epoch 5, tenure 3 and
schema hash `0xDEADBEEF`.

```text
00 01 AC 02 02 00 05 03 EF BE AD DE
```

An ACCEPT of two entries, the head then the pairs.

```text
02 01     tag 2, count 2 written as 1 in the 5 bits the range 1..32 spends
07 03     token 7 accepted at revision 3
09 01     token 9 accepted at revision 1
```

`tools/wire_decode.py --self-test` holds every byte string above against a
field list transcribed from this page and from nothing else, and `--spec`
holds the same strings against the build's own `describe()` dump.

---

## 23. What the ladder is worth

`[MEASURED 2026-09-15]` over 64 generated schedules of 40 events each, two
peers driving one lane through the real stream books and row codec, seed
`0x5CED017`. The pose is nine `I16` columns, all LADDER, moved by a source a
solver integrates. 78 percent of steps land within 7 codes, 18 percent within
120 and 4 percent are a jump.

```text
arm        rows  stepped   stepped B   forced B   saved
healthy    2624     2560       36,506     56,320   35.2%
loss       1469      791       14,333     17,417   17.7%
reorder    1449      847       16,052     19,481   17.6%
```

A step is measured from the confirmed snapshot rather than from the row
before, so the saving tracks receipt latency.

A source that jumps over its whole range still writes each element whole
behind a two-bit selector, so its worst case is 2 bits an element.

The narrower the row, the smaller the share. The same schedules over a row of
two `I16` columns save 11.9 percent where the nine-column pose saves 35.2,
because the token and the revision are a fixed cost the ladder cannot reach. A
two-column row on an impaired link is byte-neutral.

`extension/tests/wire_ladder_schedule_laws.cpp` holds every number above.
