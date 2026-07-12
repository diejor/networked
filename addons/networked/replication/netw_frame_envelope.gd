## The one wire format for Networked carrier traffic: the frame codec, the
## [enum Channel] ids multiplexed over it, and the carrier magic bytes that
## mark a datagram as Networked framing on the raw byte channel.
##
## Every payload crosses the wire in one compact envelope. The component byte
## selects the target within the entity: [code]0[/code] is the entity root,
## [code]1[/code] to [code]254[/code] a node in the entity's hydrated component
## table, [code]255[/code] a relative path string for an unmapped node. The length
## prefix lets many frames pack into a single datagram, which is what per-peer
## aggregation flushes once per tick.
## [codeblock]
## frame:      [ route varint | comp u8 | channel u8 | len varint | payload ]
## reliable:   [ magic u8 ][ frame ][ frame ] ...             ordered by the transport
## unreliable: [ magic u8 | seq u16 ][ frame ][ frame ] ...   freshest-wins per stream
## [/codeblock]
## Datagrams ride [method SceneMultiplayer.send_bytes], whose receiver re-emits
## them as raw packets. The first byte selects Networked framing versus
## pass-through for user byte traffic: a packet starting with
## [constant CARRIER_MAGIC_RELIABLE] or [constant CARRIER_MAGIC_UNRELIABLE]
## is consumed by [NetwMultiplayer], anything else is re-emitted for the
## application. The two values exist because the receiver of a raw packet
## cannot observe its transfer mode, and reliable delivery is what decides
## whether a racing [constant Channel.CALL] defers or drops.
##
## [br][br]An unreliable datagram carries one [code]u16[/code] sequence stamp
## for all its frames, counted per destination peer by
## [method NetwMultiplayer.send_packet]. A receiver applies an unreliable
## entity frame only when its stamp is fresher than the last it accepted for
## that sender, route, and channel, judged by
## [method NetwSyncPipeline.accept_unreliable], so a reordered datagram loses
## only the streams a fresher datagram already superseded.
class_name NetwFrameEnvelope
extends RefCounted

# Frame codec shared by [NetwReplicationInterface] and [NetwRpcInterface], so
# the wire format has exactly one implementation. Named
# [code]NetwFrameEnvelope[/code], not [code]NetwEnvelope[/code], because
# [code]NetwEnvelope[/code] already names the debug telemetry identity wrapper
# in [code]debug/telemetry/netw_envelope.gd[/code].

## First datagram byte marking Networked framing sent reliable. Applications
## sharing [method SceneMultiplayer.send_bytes] with the addon must not start
## their own packets with this byte or [constant CARRIER_MAGIC_UNRELIABLE].
const CARRIER_MAGIC_RELIABLE := 0x4E

## First datagram byte marking Networked framing sent unreliable. See
## [constant CARRIER_MAGIC_RELIABLE] for the reservation contract.
const CARRIER_MAGIC_UNRELIABLE := 0x6E

## First datagram byte marking an unreliable Networked datagram that also echoes
## the freshest inbound sequence back to its sender. The header carries the
## freshness [code]u16[/code] then the echo [code]u16[/code] before the frames,
## so the leading magic names the datagram shape and landed shapes never re-cut.
## The echo is the receiver-to-sender state-ack, distinct from the
## server-to-client reconciliation ack the [constant SYNC_FLAG_ACKED] bit
## carries. A sender reads it to learn the freshest datagram each peer holds, the
## baseline a per-peer delta picks its diff against.
const CARRIER_MAGIC_UNRELIABLE_ACKED := 0x8E

## The SYNC frame flags [code]u8[/code] bit layout, the extension point that
## keeps a plain (flags [code]0[/code]) frame byte-identical while stamped,
## acked, windowed, and masked variants ride the same channel. A receiver drops
## a frame carrying a bit it does not implement.
## [codeblock]
## bit0 SYNC_FLAG_STAMPED   a tick varint follows
## bit1 SYNC_FLAG_ACKED     a reconciliation ack varint follows (requires bit0)
## bit2 SYNC_FLAG_WINDOWED  a count varint and windowed rows follow (requires bit0)
## bit3 reserved            unassigned
## bit4 SYNC_FLAG_MASKED    a mask varint follows (per-peer volatile diff)
## bit5-7 reserved
## [/codeblock]
const SYNC_FLAG_STAMPED := 1 << 0
const SYNC_FLAG_ACKED := 1 << 1
const SYNC_FLAG_WINDOWED := 1 << 2
const SYNC_FLAG_MASKED := 1 << 4

## Payload families multiplexed over the one carrier pair. The channel byte in
## the frame names which handler a payload reaches on the receiver.
##
## [constant Channel.SYNC] and [constant Channel.SYNC_DELTA] are the per-tick
## sync carriers, pumped together every
## [signal NetwClockInterface.after_tick]. [constant Channel.CALL] and
## [constant Channel.REPLY] carry entity RPCs and their transaction replies.
## [constant Channel.SIGNAL] and [constant Channel.PROPERTY_SYNC] carry
## on-demand variable and signal replication. [constant Channel.ACTION]
## carries [NetwAction] traffic. [constant Channel.CLOCK_HANDSHAKE] through
## [constant Channel.CLOCK_PONG] carry the tick clock's calibration protocol,
## and [constant Channel.LAGCOMP_DENY] carries lag-compensation action
## denials, all peer-scoped on route [code]0[/code].
## [constant Channel.SPAWN] and [constant Channel.DESPAWN] carry the
## replicator's entity spawn edges. [constant Channel.CONTROL_REQUEST] and
## [constant Channel.CONTROL_APPLY] carry control transfer, dispatched the
## same route-addressed way with no RPC layer. Ids [code]100[/code] to
## [code]254[/code] are user channels over [NetwChannel].
enum Channel {
	## Routed [NetwAction] traffic. Ids [code]0[/code] and [code]1[/code] are
	## retired carriers and must not be reclaimed.
	ACTION = 2,
	## Entity remote procedure call sent from a peer, from [method Netw.rpc].
	CALL = 3,
	## Transaction reply returning the value of a [method Netw.request].
	REPLY = 4,
	## On-demand signal replication from [method Netw.emit_entity_signal].
	SIGNAL = 5,
	## On-demand property replication from [method Netw.sync_property].
	PROPERTY_SYNC = 6,
	## Server-relayed visibility transitions for unbound interest layers,
	## applied by [NetwInterestInterface]. Route [code]0[/code], peer-scoped.
	INTEREST_VISIBILITY = 7,
	## Server-relayed observer transitions for entities whose
	## [member InterestComponent.report_observers] is enabled. Route
	## [code]0[/code], peer-scoped.
	INTEREST_OBSERVER = 8,
	## Clock calibration handshake request, client to server. Reliable, route
	## [code]0[/code].
	CLOCK_HANDSHAKE = 9,
	## Clock calibration handshake reply carrying the server tickrate, server to
	## client. Reliable, route [code]0[/code].
	CLOCK_HANDSHAKE_REPLY = 10,
	## Clock latency probe carrying the client send timestamp, client to server.
	## Unreliable and sent un-batched so aggregation delay never inflates the
	## round-trip sample. Route [code]0[/code].
	CLOCK_PING = 11,
	## Clock latency reply carrying the probe timestamp and the server tick,
	## server to client. Unreliable and un-batched like [constant CLOCK_PING].
	## Route [code]0[/code].
	CLOCK_PONG = 12,
	## Lag-compensation action denial, server to the requesting peer. Reliable,
	## route [code]0[/code].
	LAGCOMP_DENY = 13,
	## Entity spawn issued by [NetwReplicationInterface] for the
	## [method Netw.replicate] and [method Netw.spawn] verbs. Carries the route,
	## identity, reconstruction recipe, and spawn state. Reliable, route
	## [code]0[/code], server to client.
	SPAWN = 14,
	## Entity despawn for a route issued through [constant SPAWN]. Reliable,
	## route [code]0[/code], server to client.
	DESPAWN = 15,
	## Route-preserving reparent of an entity issued through
	## [constant SPAWN], carrying the new parent anchor. Reliable, route
	## [code]0[/code], server to client.
	REPARENT = 16,
	## Per-tick full state of one consumed [MultiplayerSynchronizer] sync set
	## or one derived [NetwSyncSetBinding] volatile row, sent through the sync
	## pump. Unreliable, freshest-wins per stream, entity-routed. Ids
	## [code]17[/code] and [code]18[/code] are reserved and must not be claimed.
	SYNC = 19,
	## Reliable on-change delta of one sync set's watched or retained fields,
	## carrying a bitmask over the set's field order. Entity-routed.
	SYNC_DELTA = 20,
	## Control-transfer request, client to server. Reliable, entity-routed,
	## empty payload (the route and sender identify who is asking). The
	## server validates against [member NetwEntity.transfer] and
	## [signal NetwEntity.control_requested] before granting.
	CONTROL_REQUEST = 21,
	## Control-transfer application, server to the route's interest-live
	## peers (the same recipient set [constant DESPAWN] and
	## [constant REPARENT] fan to). Reliable, entity-routed, payload is the
	## new controller peer id.
	CONTROL_APPLY = 22,
}


static func pack(route: int, comp: int, channel: Channel, payload: PackedByteArray, path: String = "") -> PackedByteArray:
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, route)
	w.put_aligned_u8(comp)
	w.put_aligned_u8(channel)

	var final_payload := payload
	if comp == 255:
		var path_bytes := path.to_utf8_buffer()
		var pw := NetwBitBuffer.Writer.new()
		NetwCodec.put_varint(pw, path_bytes.size())
		pw.put_aligned_bytes(path_bytes)
		pw.put_aligned_bytes(payload)
		final_payload = pw.to_bytes()

	NetwCodec.put_varint(w, final_payload.size())
	w.put_aligned_bytes(final_payload)
	return w.to_bytes()


static func unpack_next(r: NetwBitBuffer.Reader) -> Dictionary:
	var route := NetwCodec.get_safe_varint(r)
	if route < 0:
		return { }
	var comp := r.get_aligned_u8()
	var channel := r.get_aligned_u8()
	var payload_len := NetwCodec.get_safe_varint(r)
	if payload_len < 0:
		return { }
	var payload_bytes := r.get_aligned_bytes(payload_len)

	var path := ""
	var actual_payload := payload_bytes
	if comp == 255:
		var pr := NetwBitBuffer.Reader.new(payload_bytes)
		var path_len := NetwCodec.get_safe_varint(pr)
		if path_len >= 0:
			var path_bytes := pr.get_aligned_bytes(path_len)
			path = path_bytes.get_string_from_utf8()
			actual_payload = pr.get_aligned_bytes(pr.remaining_bytes())

	return {
		"route": route,
		"comp": comp,
		"channel": channel,
		"payload": actual_payload,
		"path": path,
	}


static func unpack_all(framed_bytes: PackedByteArray) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	if framed_bytes.is_empty():
		return out
	var r := NetwBitBuffer.Reader.new(framed_bytes)
	while r.remaining_bytes() > 0:
		var frame := unpack_next(r)
		if frame.is_empty():
			break
		out.append(frame)
	return out


## Encodes one [constant Channel.SYNC] payload per the [constant SYNC_FLAG_STAMPED]
## flags grammar, the single source of truth for the frame layout every consumed,
## state, and input set shares. A plain frame ([code]flags[/code] 0) is
## byte-identical to the bare positional payload, so activating a flag bit extends
## the frame without re-cutting it.
##
## [param frame] carries the fields the [param frame]'s [code]flags[/code] select:
## [codeblock]
## ordinal     int     the set ordinal under the route
## flags       int     SYNC_FLAG_* bits
## values      Array   positional field values in wire order (the masked subset
##                     when SYNC_FLAG_MASKED is set)
## quantizers  Array   parallel bit-packers, aligned to values, null = raw
## types       Array   parallel Variant types, aligned to values
## tick        int     authoring tick, written when SYNC_FLAG_STAMPED
## ack         int     reconciliation ack, written value-plus-one when
##                     SYNC_FLAG_ACKED so the no-input sentinel -1 rides as zero
## mask        int     field bitmask, written when SYNC_FLAG_MASKED
## samples     Array   [[age, row_values], ...] newest first, written when
##                     SYNC_FLAG_WINDOWED, each row sharing quantizers and types
## [/codeblock]
## A [constant SYNC_FLAG_WINDOWED] frame carries its sample rows in place of the
## top-level [code]values[/code], and the two never combine with each other or
## with [constant SYNC_FLAG_MASKED].
static func encode_sync_frame(frame: Dictionary) -> PackedByteArray:
	var flags: int = frame.get("flags", 0)
	var quantizers: Array = frame.get("quantizers", [])
	var types: Array = frame.get("types", [])
	var w := NetwBitBuffer.Writer.new()
	NetwCodec.put_varint(w, int(frame.get("ordinal", 0)))
	w.put_aligned_u8(flags)
	if flags & SYNC_FLAG_STAMPED:
		NetwCodec.put_varint(w, int(frame.get("tick", -1)))
	if flags & SYNC_FLAG_ACKED:
		NetwCodec.put_varint(w, int(frame.get("ack", -1)) + 1)
	if flags & SYNC_FLAG_MASKED:
		NetwCodec.put_varint(w, int(frame.get("mask", 0)))
	if flags & SYNC_FLAG_WINDOWED:
		var samples: Array = frame.get("samples", [])
		NetwCodec.put_varint(w, samples.size())
		for sample: Array in samples:
			NetwCodec.put_varint(w, int(sample[0]))
			var row: Array = sample[1]
			var row_types: Array = _value_types(row, types)
			NetwScriptModel.write_values(w, row, quantizers, row_types)
	else:
		var values: Array = frame.get("values", [])
		NetwScriptModel.write_values(w, values, quantizers, _value_types(values, types))
	return w.to_bytes()


## Decodes one [constant Channel.SYNC] payload written by
## [method encode_sync_frame]. [param quantizers] and [param types] are the full
## set's parallel arrays in wire order, so a [constant SYNC_FLAG_MASKED] frame
## reads only the fields named by the mask against the matching subset. Returns
## the decoded frame:
## [codeblock]
## ordinal  int            the set ordinal
## flags    int            the raw flags byte
## tick     int            authoring tick, or -1 when not stamped
## ack      int            reconciliation ack, or -1 when not acked
## mask     int            field bitmask, or 0 when not masked
## indices  Array[int]     the masked field positions, empty when not masked
## values   Array          decoded values (the masked subset when masked)
## samples  Array          [[age, row_values], ...] when windowed, else empty
## [/codeblock]
static func decode_sync_frame(
		payload: PackedByteArray,
		quantizers: Array,
		types: Array,
) -> Dictionary:
	var r := NetwBitBuffer.Reader.new(payload)
	var out := {
		"ordinal": NetwCodec.get_safe_varint(r),
		"flags": r.get_aligned_u8(),
		"tick": -1,
		"ack": -1,
		"mask": 0,
		"indices": [] as Array[int],
		"values": [] as Array,
		"samples": [] as Array,
	}
	var flags: int = out["flags"]
	if flags & SYNC_FLAG_STAMPED:
		out["tick"] = NetwCodec.get_safe_varint(r)
	if flags & SYNC_FLAG_ACKED:
		out["ack"] = NetwCodec.get_safe_varint(r) - 1
	if flags & SYNC_FLAG_MASKED:
		out["mask"] = NetwCodec.get_safe_varint(r)
	if flags & SYNC_FLAG_WINDOWED:
		var count := NetwCodec.get_safe_varint(r)
		var samples: Array = []
		for _i in count:
			var age := NetwCodec.get_safe_varint(r)
			samples.append([age, NetwScriptModel.read_values(r, quantizers, types)])
		out["samples"] = samples
	elif flags & SYNC_FLAG_MASKED:
		var indices: Array[int] = []
		var sel_q: Array = []
		var sel_t: Array = []
		var mask: int = out["mask"]
		for i in quantizers.size():
			if mask & (1 << i):
				indices.append(i)
				sel_q.append(quantizers[i])
				sel_t.append(types[i] if i < types.size() else TYPE_NIL)
		out["indices"] = indices
		out["values"] = NetwScriptModel.read_values(r, sel_q, sel_t)
	else:
		out["values"] = NetwScriptModel.read_values(r, quantizers, types)
	return out


# Fills a per-value type array when the caller passed none, so raw values still
# self-describe on the wire the way write_values reads them back.
static func _value_types(values: Array, types: Array) -> Array:
	if not types.is_empty():
		return types
	var out: Array = []
	for v in values:
		out.append(typeof(v))
	return out
