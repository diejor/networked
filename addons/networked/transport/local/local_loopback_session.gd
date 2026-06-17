## Shared in-process session linking [LocalMultiplayerPeer] instances.
##
## One process-wide singleton is maintained via [method get_shared_session].
class_name LocalLoopbackSession
extends Resource

static var shared: LocalLoopbackSession = null

# Absorbs float-accumulation error so a packet due exactly on a poll boundary
# releases on that poll rather than the next one.
const _RELEASE_EPSILON_MS := 0.0001

var server_peer: LocalMultiplayerPeer
var client_peers: Array[LocalMultiplayerPeer] = []
var server_app_id: StringName = &""
var _clock_ms: float = 0.0
var _last_scoped_poll_frame: int = -1
var _links_by_peer: Dictionary = { }
var _held_peers: Dictionary = { }


## Declarative inbound loopback impairment for [LocalLoopbackSession].
##
## One spec describes a link in human units and feeds the simulator directly, so
## there is no compile step. [member latency_ms], [member jitter_ms], and
## [member packet_loss] are read at receive time without quantizing to the
## physics rate, so latencies below one physics frame stay expressible.
##
## [codeblock]
## var conditions := LocalLoopbackSession.LinkConditions.wifi()
## conditions.packet_loss = 0.05
## session.set_link_conditions(server, conditions)
## [/codeblock]
##
## [member packet_loss] drops unreliable packets. Reliable packets are delayed
## by [method effective_retransmit_ms] instead, which derives from
## [member latency_ms] while [member retransmit_ms] stays negative.
class LinkConditions:
	extends RefCounted

	const _DEFAULT_RETRANSMIT_MS := 30.0
	# Negative retransmit means "derive from latency at read time".
	const _AUTO_RETRANSMIT := -1.0

	var latency_ms: float = 0.0
	var jitter_ms: float = 0.0
	var packet_loss: float = 0.0
	var reorder: float = 0.0
	var duplicate: float = 0.0
	var throttle: float = 0.0
	var throttle_ms: float = 0.0
	var retransmit_ms: float = _AUTO_RETRANSMIT
	var seed: int = 0


	func _init(p_seed: int = 0) -> void:
		seed = p_seed


	## Returns a link with no simulated impairment.
	static func perfect() -> LinkConditions:
		return LinkConditions.new()


	## Returns a typical Wi-Fi profile.
	static func wifi() -> LinkConditions:
		var conditions := LinkConditions.new(1)
		conditions.latency_ms = 35.0
		conditions.jitter_ms = 8.0
		conditions.packet_loss = 0.01
		conditions.reorder = 0.005
		conditions.duplicate = 0.001
		return conditions


	## Returns a typical mobile 4G profile.
	static func mobile_4g() -> LinkConditions:
		var conditions := LinkConditions.new(2)
		conditions.latency_ms = 85.0
		conditions.jitter_ms = 25.0
		conditions.packet_loss = 0.03
		conditions.reorder = 0.02
		conditions.duplicate = 0.002
		conditions.throttle = 0.01
		conditions.throttle_ms = 80.0
		return conditions


	## Returns a poor 3G profile.
	static func poor_3g() -> LinkConditions:
		var conditions := LinkConditions.new(3)
		conditions.latency_ms = 220.0
		conditions.jitter_ms = 90.0
		conditions.packet_loss = 0.08
		conditions.reorder = 0.05
		conditions.duplicate = 0.005
		conditions.throttle = 0.04
		conditions.throttle_ms = 250.0
		return conditions


	## Returns a satellite link profile.
	static func satellite() -> LinkConditions:
		var conditions := LinkConditions.new(4)
		conditions.latency_ms = 650.0
		conditions.jitter_ms = 120.0
		conditions.packet_loss = 0.04
		conditions.reorder = 0.03
		conditions.duplicate = 0.003
		conditions.throttle = 0.02
		conditions.throttle_ms = 400.0
		return conditions


	## Returns a copy of this condition spec.
	func clone() -> LinkConditions:
		var copy := LinkConditions.new(seed)
		copy.latency_ms = latency_ms
		copy.jitter_ms = jitter_ms
		copy.packet_loss = packet_loss
		copy.reorder = reorder
		copy.duplicate = duplicate
		copy.throttle = throttle
		copy.throttle_ms = throttle_ms
		copy.retransmit_ms = retransmit_ms
		return copy


	## Returns this link's baseline latency in milliseconds.
	func effective_latency_ms() -> float:
		return maxf(0.0, latency_ms)


	## Returns the reliable retransmit delay in milliseconds, deriving it from
	## [member latency_ms] while [member retransmit_ms] stays negative.
	func effective_retransmit_ms() -> float:
		if retransmit_ms >= 0.0:
			return retransmit_ms
		return maxf(_DEFAULT_RETRANSMIT_MS, 2.0 * maxf(0.0, latency_ms))


class _LinkState:
	var conditions_by_sender: Dictionary = { }
	var rng_by_stream: Dictionary = { }
	var reliable_due_by_channel: Dictionary = { }
	var in_flight: Array[Dictionary] = []
	var throttle_until_ms: float = 0.0
	var seq: int = 0


## Returns the process-wide shared session, creating it on first access.
static func get_shared_session() -> LocalLoopbackSession:
	if not shared:
		shared = LocalLoopbackSession.new()
	return shared


## Returns [code]true[/code] if the server peer exists and is connected.
func has_live_server() -> bool:
	return (
			server_peer != null
			and server_peer.get_connection_status()
			!= MultiplayerPeer.CONNECTION_DISCONNECTED
	)


func init_server_side() -> void:
	if has_live_server():
		return

	if server_peer:
		server_peer.loopback_session = null
		server_peer.close()

	server_peer = LocalMultiplayerPeer.new()
	server_peer.loopback_session = self
	var err := server_peer.create_server()
	if err != OK:
		Netw.dbg.warn(
			"Loopback: server create_server failed",
			func(m): push_warning(m)
		)


## Creates and links a new client peer to the server.
##
## Returns the new [LocalMultiplayerPeer].
func create_client_peer() -> LocalMultiplayerPeer:
	init_server_side()

	var client := LocalMultiplayerPeer.new()
	client.loopback_session = self
	var client_id := randi_range(2, 2147483647)
	var err := client.create_client(client_id)
	if err != OK:
		Netw.dbg.warn(
			"Loopback: client create_client failed",
			func(m): push_warning(m)
		)
		return client

	server_peer.force_connect_peer(client_id, client)
	client.force_connect_peer(1, server_peer)
	client_peers.append(client)
	Netw.dbg.info("Local loopback handshake complete for client %d." % client_id)
	return client


## Returns the server peer, initializing it first if necessary.
func get_server_peer() -> LocalMultiplayerPeer:
	init_server_side()
	return server_peer


## Convenience wrapper around [method create_client_peer].
func get_client_peer() -> LocalMultiplayerPeer:
	return create_client_peer()


## Polls the server and all active client peers, advancing session time by one
## physics frame.
func poll() -> void:
	_clock_ms += _physics_period_ms()
	_poll_peers()


## Polls all active peers and advances session time once per physics frame.
##
## Session time tracks physics frames, not idle frames, so
## [member LinkConditions.latency_ms] maps to a stable wall-clock latency
## regardless of how many idle frames the
## engine runs between physics steps. Latency is therefore reproducible. Idle
## frames in excess of the physics rate still drain delivered packets through the
## peer queue, they just do not advance the delay clock.
func poll_frame_scoped() -> void:
	var frame := Engine.get_physics_frames()
	if frame != _last_scoped_poll_frame:
		if _last_scoped_poll_frame < 0:
			_clock_ms += _physics_period_ms()
		else:
			_clock_ms += float(frame - _last_scoped_poll_frame) * _physics_period_ms()
		_last_scoped_poll_frame = frame
	_poll_peers()


## Advances session time by [param ms] and releases any now-due packets to every
## linked peer. Used by deterministic steppers that drive time directly instead
## of off real physics frames.
func advance_time(ms: float) -> void:
	_clock_ms += maxf(0.0, ms)
	_poll_peers()


func _poll_peers() -> void:
	if server_peer:
		_poll_or_hold(server_peer)
	for client in client_peers:
		if client and not client._closed:
			_poll_or_hold(client)


## Holds inbound packets for [param peer] until
## [method release_inbound_packets] is called.
##
## Existing queued packets are held immediately. New packets are captured
## during [method poll], preserving delivery order for release.
func hold_inbound_packets(peer: LocalMultiplayerPeer) -> void:
	if not peer:
		return
	_held_peers[peer] = true
	_capture_held_packets(peer)


## Releases packets previously captured by
## [method hold_inbound_packets], delivering them before newer queued
## packets on the same [param peer].
func release_inbound_packets(peer: LocalMultiplayerPeer) -> void:
	if not peer or not _held_peers.has(peer):
		return
	_capture_held_packets(peer)
	_held_peers.erase(peer)
	_release_due(peer, true)


## Sets deterministic inbound link conditions for [param peer].
func set_link_conditions(
		peer: LocalMultiplayerPeer,
		conditions: LinkConditions,
		sender_id: int = 0,
) -> void:
	if not peer:
		return
	var state := _ensure_link(peer)
	if not conditions:
		state.conditions_by_sender.erase(sender_id)
		_clear_sender_rng(state, sender_id)
		return
	state.conditions_by_sender[sender_id] = conditions.clone()
	_clear_sender_rng(state, sender_id)
	_capture_linked_queued_packets(peer)


## Clears inbound link conditions for [param peer] and [param sender_id].
func clear_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> void:
	if not peer or not _links_by_peer.has(peer):
		return
	var state: _LinkState = _links_by_peer[peer]
	state.conditions_by_sender.erase(sender_id)
	_clear_sender_rng(state, sender_id)
	if not _has_link_conditions(state) and not _held_peers.has(peer):
		_release_due(peer, true)
		if state.in_flight.is_empty():
			_links_by_peer.erase(peer)


## Clears all installed link conditions and flushes delayed packets.
func clear_all_link_conditions() -> void:
	for peer: LocalMultiplayerPeer in _links_by_peer.keys():
		var state: _LinkState = _links_by_peer[peer]
		state.conditions_by_sender.clear()
		state.rng_by_stream.clear()
		state.reliable_due_by_channel.clear()
		if not _held_peers.has(peer):
			_release_due(peer, true)
			if state.in_flight.is_empty():
				_links_by_peer.erase(peer)


## Drops delayed packets authored by [param sender_id] across all receivers.
func purge_packets_from(sender_id: int) -> void:
	for peer: LocalMultiplayerPeer in _links_by_peer.keys():
		var state: _LinkState = _links_by_peer[peer]
		for i in range(state.in_flight.size() - 1, -1, -1):
			var entry: Dictionary = state.in_flight[i]
			var packet: Dictionary = entry.get("packet", { })
			if int(packet.get("peer", 0)) == sender_id:
				state.in_flight.remove_at(i)
		_clear_sender_rng(state, sender_id)
		if state.in_flight.is_empty() \
				and not _has_link_conditions(state) \
				and not _held_peers.has(peer):
			_links_by_peer.erase(peer)


## Returns the installed inbound link conditions for [param peer].
func get_link_conditions(
		peer: LocalMultiplayerPeer,
		sender_id: int = 0,
) -> LinkConditions:
	if not peer or not _links_by_peer.has(peer):
		return null
	var state: _LinkState = _links_by_peer[peer]
	return state.conditions_by_sender.get(sender_id) as LinkConditions


## Closes all peers and resets the session so a new server can be hosted.
func reset() -> void:
	if server_peer:
		server_peer.loopback_session = null
		server_peer.close()
	for client in client_peers:
		if client:
			client.loopback_session = null
			client.close()
	server_peer = null
	server_app_id = &""
	client_peers.clear()
	_clock_ms = 0.0
	_last_scoped_poll_frame = -1
	_links_by_peer.clear()
	_held_peers.clear()


func _poll_or_hold(peer: LocalMultiplayerPeer) -> void:
	# Conditioning now happens at receive time in capture_incoming. Poll only
	# drives connection events and releases packets whose due poll has arrived.
	if _held_peers.has(peer):
		_capture_held_packets(peer)
		return

	peer.poll()
	if _links_by_peer.has(peer):
		_release_due(peer)


## Offers an inbound [param packet] for [param peer] to link conditions at
## receive time. Returns [code]true[/code] when the session takes ownership of
## the packet (held in flight or dropped), so the caller must not deliver it.
func capture_incoming(peer: LocalMultiplayerPeer, packet: Dictionary) -> bool:
	if _held_peers.has(peer):
		var held := _ensure_link(peer)
		_enqueue_packet(held, packet, INF)
		return true

	var state := _links_by_peer.get(peer) as _LinkState
	if not state or not _has_link_conditions(state):
		return false
	var conditions := _conditions_for_packet(state, packet)
	if not conditions:
		return false

	if _is_reliable_packet(packet):
		_capture_reliable_packet(state, packet, conditions)
	else:
		_capture_unreliable_packet(state, packet, conditions)
	return true


func _capture_held_packets(peer: LocalMultiplayerPeer) -> void:
	if peer._packet_queue.is_empty():
		return
	var state := _ensure_link(peer)
	for packet in peer._packet_queue:
		_enqueue_packet(state, packet, INF)
	peer._packet_queue.clear()


func _release_due(peer: LocalMultiplayerPeer, flush_all: bool = false) -> void:
	if not _links_by_peer.has(peer) or peer._closed or peer._closing:
		return

	var state: _LinkState = _links_by_peer[peer]
	state.in_flight.sort_custom(_compare_in_flight)

	var due: Array[Dictionary] = []
	var remaining: Array[Dictionary] = []
	for entry: Dictionary in state.in_flight:
		if flush_all or float(entry.get("due_time_ms", 0.0)) <= _clock_ms + _RELEASE_EPSILON_MS:
			due.append(entry)
		else:
			remaining.append(entry)
	state.in_flight = remaining

	if due.is_empty():
		return

	var existing := peer._packet_queue.duplicate()
	peer._packet_queue.clear()
	peer._packet_queue.append_array(existing)
	for entry: Dictionary in due:
		var packet: Dictionary = entry.get("packet", { })
		if _sender_is_linked(peer, packet):
			peer._packet_queue.append(packet)
	_prune_reliable_due(state)
	if state.in_flight.is_empty() \
			and not _has_link_conditions(state) \
			and not _held_peers.has(peer):
		_links_by_peer.erase(peer)


func _has_link_conditions(state: _LinkState) -> bool:
	return state.conditions_by_sender.size() > 0


func _conditions_for_packet(
		state: _LinkState,
		packet: Dictionary,
) -> LinkConditions:
	var sender_id: int = packet.get("peer", 0)
	if state.conditions_by_sender.has(sender_id):
		return state.conditions_by_sender[sender_id]
	return state.conditions_by_sender.get(0) as LinkConditions


func _ensure_link(peer: LocalMultiplayerPeer) -> _LinkState:
	if not _links_by_peer.has(peer):
		_links_by_peer[peer] = _LinkState.new()
	return _links_by_peer[peer]


func _enqueue_packet(
		state: _LinkState,
		packet: Dictionary,
		due_time_ms: float,
) -> void:
	state.in_flight.append(
		{
			"packet": packet,
			"due_time_ms": due_time_ms,
			"seq": state.seq,
		},
	)
	state.seq += 1


func _capture_linked_queued_packets(peer: LocalMultiplayerPeer) -> void:
	if peer._packet_queue.is_empty():
		return

	var state := _ensure_link(peer)
	var remaining: Array[Dictionary] = []
	for packet: Dictionary in peer._packet_queue:
		var conditions := _conditions_for_packet(state, packet)
		if not conditions:
			remaining.append(packet)
		elif _is_reliable_packet(packet):
			_capture_reliable_packet(state, packet, conditions)
		else:
			_capture_unreliable_packet(state, packet, conditions)
	peer._packet_queue = remaining


func _capture_reliable_packet(
		state: _LinkState,
		packet: Dictionary,
		conditions: LinkConditions,
) -> void:
	var sender_id: int = packet.get("peer", 0)
	var due_ms := _clock_ms + conditions.effective_latency_ms()
	if _roll(state, sender_id, "loss", conditions.seed, conditions.packet_loss):
		due_ms += conditions.effective_retransmit_ms()
	var key := "%d:%d" % [sender_id, int(packet.get("channel", 0))]
	var last_due: float = state.reliable_due_by_channel.get(key, -1.0)
	due_ms = maxf(due_ms, last_due)
	state.reliable_due_by_channel[key] = due_ms
	_enqueue_packet(state, packet, due_ms)


func _capture_unreliable_packet(
		state: _LinkState,
		packet: Dictionary,
		conditions: LinkConditions,
) -> void:
	var sender_id: int = packet.get("peer", 0)
	if _roll(state, sender_id, "loss", conditions.seed, conditions.packet_loss):
		return

	var due_ms := _clock_ms + conditions.effective_latency_ms()
	due_ms += _draw_jitter(state, sender_id, conditions)
	if _roll(
		state,
		sender_id,
		"reorder",
		conditions.seed,
		conditions.reorder,
	):
		due_ms += _physics_period_ms()
	if state.throttle_until_ms <= _clock_ms and _roll(
		state,
		sender_id,
		"throttle",
		conditions.seed,
		conditions.throttle,
	):
		state.throttle_until_ms = \
		_clock_ms + maxf(_physics_period_ms(), conditions.throttle_ms)
	if state.throttle_until_ms > _clock_ms:
		due_ms = maxf(due_ms, state.throttle_until_ms)

	_enqueue_packet(state, packet, due_ms)
	if _roll(
		state,
		sender_id,
		"duplicate",
		conditions.seed,
		conditions.duplicate,
	):
		_enqueue_packet(state, packet.duplicate(true), due_ms + _physics_period_ms())


func _is_reliable_packet(packet: Dictionary) -> bool:
	return packet.get("mode", MultiplayerPeer.TRANSFER_MODE_RELIABLE) \
			== MultiplayerPeer.TRANSFER_MODE_RELIABLE


func _draw_jitter(
		state: _LinkState,
		sender_id: int,
		conditions: LinkConditions,
) -> float:
	if conditions.jitter_ms <= 0.0:
		return 0.0
	var rng := _rng_for(state, sender_id, "jitter", conditions.seed)
	return rng.randf_range(0.0, conditions.jitter_ms)


func _roll(
		state: _LinkState,
		sender_id: int,
		stream: String,
		seed: int,
		probability: float,
) -> bool:
	if probability <= 0.0:
		return false
	if probability >= 1.0:
		return true
	return _rng_for(state, sender_id, stream, seed).randf() < probability


func _rng_for(
		state: _LinkState,
		sender_id: int,
		stream: String,
		seed: int,
) -> RandomNumberGenerator:
	var key := "%d:%s" % [sender_id, stream]
	if state.rng_by_stream.has(key):
		return state.rng_by_stream[key]
	var rng := RandomNumberGenerator.new()
	rng.seed = seed ^ hash(stream)
	state.rng_by_stream[key] = rng
	return rng


func _clear_sender_rng(state: _LinkState, sender_id: int) -> void:
	var prefix := "%d:" % sender_id
	for key: String in state.rng_by_stream.keys():
		if key.begins_with(prefix):
			state.rng_by_stream.erase(key)
	for key: String in state.reliable_due_by_channel.keys():
		if key.begins_with(prefix):
			state.reliable_due_by_channel.erase(key)


func _prune_reliable_due(state: _LinkState) -> void:
	for key: String in state.reliable_due_by_channel.keys():
		if float(state.reliable_due_by_channel[key]) <= _clock_ms:
			state.reliable_due_by_channel.erase(key)


func _physics_period_ms() -> float:
	var setting := ProjectSettings.get_setting(
		"physics/common/physics_ticks_per_second",
		60,
	)
	return 1000.0 / float(maxi(1, int(setting)))


func _sender_is_linked(
		peer: LocalMultiplayerPeer,
		packet: Dictionary,
) -> bool:
	var sender_id: int = packet.get("peer", 0)
	return sender_id == 0 or peer.linked_peers.has(sender_id)


func _compare_in_flight(a: Dictionary, b: Dictionary) -> bool:
	var a_due: float = a.get("due_time_ms", 0.0)
	var b_due: float = b.get("due_time_ms", 0.0)
	if is_equal_approx(a_due, b_due):
		return int(a.get("seq", 0)) < int(b.get("seq", 0))
	return a_due < b_due
