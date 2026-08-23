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
## for all its frames, counted per destination peer by the session's carrier.
## A receiver applies an unreliable
## entity frame only when its stamp is fresher than the last it accepted for
## that sender, route, and channel, judged by
## [method NetwSyncPipeline.accept_unreliable], so a reordered datagram loses
## only the streams a fresher datagram already superseded.
class_name NetwFrameEnvelope
extends RefCounted

const RpcCore := preload("res://addons/networked/replication/rpc_core.gd")

# Frame codec shared by [ReplicationCore] and [RpcCore], so
# the wire format has exactly one implementation. Named
# [code]NetwFrameEnvelope[/code], not [code]NetwEnvelope[/code], because
# [code]NetwEnvelope[/code] already names the debug telemetry identity wrapper
# in [code]debug/telemetry/netw_envelope.gd[/code].

## First datagram byte marking Networked V9 framing sent reliable.
const CARRIER_MAGIC_RELIABLE := NetwCarrierFrame.MAGIC_RELIABLE

## First datagram byte marking Networked V9 framing sent unreliable.
const CARRIER_MAGIC_UNRELIABLE := NetwCarrierFrame.MAGIC_UNRELIABLE

## First datagram byte marking an unreliable Networked V9 datagram that echoes sequence.
const CARRIER_MAGIC_UNRELIABLE_ACKED := NetwCarrierFrame.MAGIC_UNRELIABLE_ACKED

## Payload families multiplexed over the one carrier pair. The channel byte in
## the frame names which handler a payload reaches on the receiver.
##
## [constant Channel.SYNC] and [constant Channel.SYNC_DELTA] are the per-tick
## sync carriers, pumped together every
## [signal NetwMultiplayer.after_tick]. [constant Channel.CALL] and
## [constant Channel.REPLY] carry entity RPCs and their transaction replies.
## [constant Channel.SIGNAL] and [constant Channel.PROPERTY_SYNC] carry
## on-demand variable and signal replication. [constant Channel.ACTION]
## carries [NetwAction] traffic. [constant Channel.CLOCK_HANDSHAKE] through
## [constant Channel.CLOCK_PONG] carry the tick clock's calibration protocol,
## and [constant Channel.LAGCOMP_DENY] carries lag-compensation action
## denials, all peer-scoped on route [code]0[/code].
## [constant Channel.TABLE] carries replicated table rows and the route
## lifecycle stream, also peer-scoped on route [code]0[/code].
## [constant Channel.SPAWN] and [constant Channel.DESPAWN] carry the
## replicator's entity spawn edges. [constant Channel.CONTROL_REQUEST] and
## [constant Channel.CONTROL_APPLY] carry control transfer, dispatched the
## same route-addressed way with no RPC layer.
## [constant Channel.PREDICT_COMMAND] and [constant Channel.PREDICT_ACK] carry
## prediction's owner and authority lanes, keyed by transition rather than by
## clock label. Ids [code]100[/code] to [code]254[/code] are user channels over
## [NetwChannel].
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
	## Id [code]7[/code] is a retired interest carrier and must not be reclaimed.
	## Server-relayed route-addressed interest layer attribution and observer
	## awareness. Reliable, route [code]0[/code], peer-scoped.
	INTEREST_AWARENESS = 8,
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
	## Entity spawn issued by [ReplicationCore] for the
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
	## Rows of one replicated table, or the route lifecycle stream when the
	## payload's table id is [code]0[/code]. Route [code]0[/code], server to
	## client, and the only carrier whose freshness is judged in the decoder
	## rather than by the datagram book, because a route-0 frame never reaches
	## that book. Data frames ride unreliable by default, while removals,
	## tombstones, and snapshots ride reliable. See [TableCore] for the payload
	## grammar.
	TABLE = 17,
	## Per-tick full state of one consumed [MultiplayerSynchronizer] sync set
	## or one derived [NetwPropertySetBinding] volatile row, sent through the sync
	## pump. Unreliable, freshest-wins per stream, entity-routed. Id
	## [code]18[/code] is reserved and must not be claimed.
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
	## A joining peer's serialized [JoinPayload], client to server. Reliable,
	## route [code]0[/code]. The session-scoped join handshake rides the carrier
	## rather than a node [code]@rpc[/code], so a session with no
	## [MultiplayerTree] node still joins. Answered with [constant SESSION_ACCEPT]
	## and [constant SESSION_ROSTER].
	SESSION_JOIN = 23,
	## One accepted [ResolvedJoin], server to every peer. Reliable, route
	## [code]0[/code]. Enriches the recipient's roster row and fires
	## [signal NetwMultiplayer.participant_joined].
	SESSION_ACCEPT = 24,
	## The full accepted roster ([code]var_to_bytes[/code] of an
	## [code]Array[PackedByteArray][/code] of [ResolvedJoin]s), server to a newly
	## accepted peer. Reliable, route [code]0[/code]. Backfills every participant
	## admitted before the recipient connected.
	SESSION_ROSTER = 25,
	## Server pause notification. Reliable, route [code]0[/code].
	SESSION_PAUSE = 26,
	## Server unpause notification. Reliable, route [code]0[/code].
	SESSION_UNPAUSE = 27,
	## Server kick notification sent before disconnecting the target peer.
	## Reliable, route [code]0[/code].
	SESSION_KICKED = 28,
	## A player's scene change request, client to server. Reliable, route
	## [code]0[/code]. The session-scoped scene handshake rides the carrier
	## rather than a node [code]@rpc[/code], so a session whose
	## [MultiplayerSceneManager] has not spawned still asks. Answered with
	## [constant SESSION_SCENE_RESULT].
	SESSION_SCENE_REQUEST = 29,
	## The server-authored outcome of one scene request, server to the
	## requesting peer. Reliable, route [code]0[/code]. Carries the request id
	## and the [enum Error].
	SESSION_SCENE_RESULT = 30,
	## Server graceful-shutdown notice, server to every peer. Reliable, route
	## [code]0[/code]. The notice rides the carrier rather than a node
	## [code]@rpc[/code], so a root-installed session with no [MultiplayerTree]
	## still warns its clients before it tears down. Fires
	## [signal NetwMultiplayer.server_disconnecting].
	SESSION_SHUTDOWN = 31,
	## Server notice that a participant's scene membership was released, server to
	## the released peer. Reliable, route [code]0[/code]. Carries the released
	## the scene's framework-derived layer id. The recipient clears its local
	## participant's [member NetwParticipant.current_scene] when it still matches.
	SESSION_SCENE_RELEASED = 32,
	## A player's request to kick a peer, client to server. Reliable, route
	## [code]0[/code]. The request rides the carrier rather than a node
	## [code]@rpc[/code], so a session with no [MultiplayerTree] still asks. The
	## server fires [signal NetwMultiplayer.kick_requested] and decides.
	SESSION_KICK_REQUEST = 33,
	## A player's request for permission to leave, client to server. Reliable,
	## route [code]0[/code]. The server fires
	## [signal NetwMultiplayer.disconnect_requested] and decides.
	SESSION_LEAVE_REQUEST = 34,
	## One owner's prediction commands with the transitions they drove, owner to
	## server. Unreliable, entity-routed, window-redundant.
	##
	## A transition and the command that drove it ride the same frame because a
	## transition whose command arrived separately can be applied without it,
	## which authority can only paper over by substituting a command the owner
	## never authored. The decoder drops a frame carrying a fresh transition
	## without its payload, so that pairing is a parsing fact rather than a
	## check.
	PREDICT_COMMAND = 35,
	## Authority's per-transition acknowledgements, server to the owning peer.
	## Unreliable, entity-routed, re-sent until the owner confirms.
	##
	## Acknowledgement is separated from state because a held authority frame
	## still owes a remote observer its state while owing the owner no
	## acknowledgement at all. Each record carries what authority actually ran,
	## so an owner learns that its command was substituted instead of inferring
	## it from a state it cannot explain.
	PREDICT_ACK = 36,
	## One owner's prediction commands relayed to a subscribed observer, server
	## to client. Unreliable, entity-routed, window-redundant.
	##
	## The frame is the authored [constant PREDICT_COMMAND] re-emitted byte for
	## byte, because a subscriber that decoded a re-cut frame would be reading a
	## command the author never wrote. The entity route already names the
	## subject, so the relay needs no origin field and the layout is the
	## author's.
	PREDICT_RELAY = 37,
	## A relay subscription request, client to server. Reliable, entity-routed,
	## payload is one byte that is non-zero to subscribe and zero to drop.
	##
	## Reliable although the lane it opens is not, because a lost subscribe
	## presents as an entity that never relays, which a subscriber cannot tell
	## from one nobody is authoring for. The server answers it against
	## [method NetwMultiplayer.interest_admits], so a peer can only ever
	## subscribe to what it may already see.
	PREDICT_RELAY_REQUEST = 38,
	## One declared property set's volatile row, diffed per recipient against
	## what that peer is known to hold and carried as a
	## [NetwReplicationSend] row frame. The frame is self-addressing: it
	## carries its own route, its row address within that route, its tick and
	## its reconciliation ack, so the envelope's component byte is the address
	## the receiver resolves the binding by.
	SYNC_ROW = 39,
	## One declared property set's retained row, carried as a
	## [NetwReplicationSend] row frame masked to the columns that changed since
	## the recipient last held it. Reliable and entity-routed, self-addressing
	## exactly as [constant SYNC_ROW] is.
	##
	## The lane it rides is ordered and guaranteed, so the send is its own
	## proof: the sender advances what it believes the peer holds when the
	## frame leaves, and no acknowledgement settles it afterward.
	SYNC_ROW_DELTA = 40,
	## One declared property set's windowed input row, carried as a
	## [NetwReplicationSend] row frame that repeats every recent tick still in
	## flight rather than the newest one alone. Unreliable and entity-routed,
	## self-addressing exactly as [constant SYNC_ROW] is.
	##
	## An input tick is not superseded by a fresher one: the simulation still
	## owes it a step. Repeating the range heals a loss inside the next frame,
	## where a retransmit would arrive a round trip after the step it was for.
	SYNC_ROW_WINDOW = 41,
}


static func pack(route: int, comp: int, channel: Channel, payload: PackedByteArray, path: String = "") -> PackedByteArray:
	return NetwMultiplayerCore.frame_pack(route, comp, channel, payload, path)


static func unpack_next(r: NetwBitBufferReader) -> Dictionary:
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
		var pr := NetwBitBufferReader.create(payload_bytes)
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
	var r := NetwBitBufferReader.create(framed_bytes)
	while r.remaining_bytes() > 0:
		var frame := unpack_next(r)
		if frame.is_empty():
			break
		out.append(frame)
	return out
