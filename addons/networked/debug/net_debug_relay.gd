## Game-side debug relay bridge — routes telemetry between game processes.
##
## Lives as a child of [MultiplayerTree] so RPCs use [code]mt.backend.api[/code]
## (the game multiplayer context) rather than the default /root SceneMultiplayer.
##
## Design: pure stateless byte pipe. No deserialization, no caches, no history.
##
## Single inbound RPC: [method forward_to_relay].
## The relay determines internally whether to deliver to the local EngineDebugger
## by checking [method _is_local_peer] on the sender — same-process clients already
## sent directly via EngineDebugger and must not be echoed. The reporter always
## calls the same RPC regardless of topology.
##
## Dedup sites in this file:
##   DEDUP-1 — [method dispatch_to_recipients]: skip local peers on fan-out.
##   DEDUP-2 — [method register_as_recipient]: one slot per reporter_id.
@tool
class_name NetDebugRelay
extends Node

const MAX_PAYLOAD_BYTES := 262144  # 256 KB


func is_relay_active() -> bool:
	return is_inside_tree() \
		and multiplayer != null \
		and multiplayer.has_multiplayer_peer()


# ─── Server: own-event fan-out ────────────────────────────────────────────────

## Forwards server-originated [param envelope_bytes] to all registered remote recipients.
## Server reporter already delivered to its own editor via EngineDebugger.
func dispatch_to_recipients(envelope_bytes: PackedByteArray) -> void:
	for rid: int in NetworkedDebugger._process_recipients:
		if rid != 1 and multiplayer.get_peers().has(rid):
			# DEDUP-1 (transport fan-out): skip peers in this OS process.
			if _is_local_peer(rid):
				continue
			forward_to_peer.rpc_id(rid, envelope_bytes)


# ─── RPC: client → server relay ──────────────────────────────────────────────

## Called by all clients regardless of whether they share an OS process with the relay.
## Delivers to the local editor only when the sender is a different OS process
## (same-process clients already sent via the direct EngineDebugger path).
## Always fans out to all other registered recipients.
@rpc("any_peer", "call_remote", "reliable")
func forward_to_relay(envelope_bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if not _valid_sender(sender, envelope_bytes.size()):
		return
	# DEDUP-1 (local echo): same-process clients already delivered via EngineDebugger.
	if EngineDebugger.is_active() and not _is_local_peer(sender):
		EngineDebugger.send_message("networked:envelope_remote", [envelope_bytes])
	_fanout(envelope_bytes, sender)


# ─── RPC: server → remote client ─────────────────────────────────────────────

@rpc("authority", "call_remote", "reliable")
func forward_to_peer(envelope_bytes: PackedByteArray) -> void:
	if not EngineDebugger.is_active():
		return
	EngineDebugger.send_message("networked:envelope_remote", [envelope_bytes])


# ─── Recipient registration ───────────────────────────────────────────────────

@rpc("any_peer", "call_remote", "reliable")
func register_as_recipient(token: String = "", reporter_id: String = "") -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
	if sender == 0 or sender == 1:
		return  # Server's own editor uses the direct EngineDebugger path

	var expected: String = ProjectSettings.get_setting("networked/debug/relay_token", "")
	if not expected.is_empty() and token != expected:
		return

	# DEDUP-2 (registration): one relay recipient slot per reporter_id.
	# Multiple trees in the same process must not each register a separate slot.
	if not reporter_id.is_empty():
		if reporter_id in NetworkedDebugger._recipient_map:
			var old_peer: int = NetworkedDebugger._recipient_map[reporter_id]
			if old_peer != sender:
				NetworkedDebugger._process_recipients.erase(old_peer)
				NetworkedDebugger._process_recipients.append(sender)
				NetworkedDebugger._recipient_map[reporter_id] = sender
			return # Already registered this process
		else:
			NetworkedDebugger._recipient_map[reporter_id] = sender

	if sender not in NetworkedDebugger._process_recipients:
		NetLog.info("Relay: [RegisterSuccess] Registered peer %d (rid=%s) as debug recipient" % [sender, reporter_id])
		NetworkedDebugger._process_recipients.append(sender)
		if NetworkedDebugger:
			# Push server's current state to the newcomer.
			NetworkedDebugger._on_remote_snapshot_request()
			# Also ask all existing remote clients to re-emit, so the newcomer
			# sees their state too (not just the server's).
			broadcast_snapshot_request()


func deregister_recipient(peer_id: int) -> void:
	NetworkedDebugger._process_recipients.erase(peer_id)
	for rid in NetworkedDebugger._recipient_map.keys():
		if NetworkedDebugger._recipient_map[rid] == peer_id:
			NetworkedDebugger._recipient_map.erase(rid)
			break


# ─── Snapshot Protocol ────────────────────────────────────────────────────────

## Asks all registered remote clients to re-emit their current state.
func broadcast_snapshot_request() -> void:
	for rid: int in NetworkedDebugger._process_recipients:
		if rid != 1 and multiplayer.get_peers().has(rid):
			request_snapshot_from_peer.rpc_id(rid)


## RPC: server → client — asks the client to re-emit its local state.
@rpc("authority", "call_remote", "reliable")
func request_snapshot_from_peer() -> void:
	if NetworkedDebugger:
		NetworkedDebugger._on_remote_snapshot_request()


## RPC: client → server — asks the server to re-emit its local state.
## Used when a client-attached editor triggers a manual Refresh.
@rpc("any_peer", "call_remote", "reliable")
func request_snapshot_from_server() -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender not in NetworkedDebugger._process_recipients:
		return
	if NetworkedDebugger:
		NetworkedDebugger._on_remote_snapshot_request()


## RPC: any → any — asks the remote peer to re-emit its crash manifest history.
@rpc("any_peer", "call_remote", "reliable")
func request_manifest_history(peer_key: String) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender not in NetworkedDebugger._process_recipients:
		return
	if NetworkedDebugger:
		NetworkedDebugger._handle_request_manifest_history(peer_key)


## RPC: any → any — applies a visualizer command from the editor.
@rpc("any_peer", "call_remote", "reliable")
func apply_visualizer_command(d: Dictionary) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender != 1 and sender not in NetworkedDebugger._process_recipients:
		return
	if NetworkedDebugger:
		NetworkedDebugger._handle_visualizer_toggle(d, true)


# ─── Internal ─────────────────────────────────────────────────────────────────

func _is_local_peer(peer_id: int) -> bool:
	for mt in NetworkedDebugger._trees:
		if mt.multiplayer_api and mt.multiplayer_api.get_unique_id() == peer_id:
			return true
	return false


func _valid_sender(sender: int, byte_size: int) -> bool:
	if sender != 1 and not multiplayer.get_peers().has(sender):
		return false
	if byte_size > MAX_PAYLOAD_BYTES:
		NetLog.error("Relay: [DropOversized] %d bytes from peer %d" % [byte_size, sender], [], func(m): push_error(m))
		return false
	return true


func _fanout(envelope_bytes: PackedByteArray, except_sender: int) -> void:
	for rid: int in NetworkedDebugger._process_recipients:
		if rid != 1 and rid != except_sender and multiplayer.get_peers().has(rid):
			forward_to_peer.rpc_id(rid, envelope_bytes)
