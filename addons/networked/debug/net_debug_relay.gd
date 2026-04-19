## Game-side debug relay bridge — routes telemetry between game processes.
##
## Added as a child of the [MultiplayerTree] when [method OS.has_feature] returns
## [code]"debug"[/code] and a [MultiplayerTree] has registered. Living under the
## MultiplayerTree ensures RPCs use [code]mt.backend.api[/code] (the game multiplayer
## context) rather than the default /root SceneMultiplayer.
@tool
class_name NetDebugRelay
extends Node

const MAX_PAYLOAD_BYTES := 1048576 # 1MB
const STARTUP_BUFFER_LIMIT := 512
const HISTORY_CACHE_LIMIT := 100 # Max batches to keep per tree for late-joiners

# Peer IDs of game peers allowed to send telemetry to this relay.
var _authorized_senders: Array[int] = []

# List of peer IDs that want to receive relayed data.
# Initialized from the reporter's static registry but managed per-instance
# to prevent stale peer IDs from accumulating across editor runs.
var _recipients: Array[int] = []

# Per-frame outgoing batch: [[msg, data], ...]
var _outgoing_batch: Array = []
var _batch_tree_name: String = ""

# Cache of the latest session_registered batch per unique tree instance.
# Key: "rid:tree_name" → PackedByteArray
var _session_cache: Dictionary = {}

# History cache for late-joining recipients: tree_name -> Array[PackedByteArray]
# Ensures remote clients see full Span Trace history even if they join late.
var _history_cache: Dictionary = {}

# Buffer for events generated before the relay is connected (for standalone clients).
# Flushed as the first batch once is_relay_active() becomes true.
var _startup_buffer: Array = []


func _ready() -> void:
	# Synchronize with the global registry but keep our own copy for the run.
	_recipients = NetworkedDebugReporter._process_recipients.duplicate()
	
	if multiplayer.is_server():
		# The server relay is the authority. It should start with a clean slate
		# for its own session and history.
		_session_cache.clear()
		_history_cache.clear()


func is_relay_active() -> bool:
	return is_inside_tree() \
		and multiplayer != null \
		and multiplayer.has_multiplayer_peer()


func send(msg: String, data: Dictionary, source_tree_name: String) -> void:
	_batch_tree_name = source_tree_name
	
	if not is_relay_active():
		if _startup_buffer.size() < STARTUP_BUFFER_LIMIT:
			_startup_buffer.append([msg, data])
		return
	
	_outgoing_batch.append([msg, data])


func flush_immediately() -> void:
	if _outgoing_batch.is_empty() and _startup_buffer.is_empty():
		return
	_flush_batch()


func _physics_process(_dt: float) -> void:
	if not _outgoing_batch.is_empty() or not _startup_buffer.is_empty():
		_flush_batch()


func _flush_batch() -> void:
	if not is_relay_active():
		return

	# First, flush any buffered startup history.
	if not _startup_buffer.is_empty():
		var batch := {"tn": _batch_tree_name, "events": _startup_buffer.duplicate()}
		var bytes := var_to_bytes(batch)
		_startup_buffer.clear()
		
		if multiplayer.is_server():
			_update_caches(batch, _batch_tree_name, bytes)
			_dispatch(bytes)
		else:
			forward_to_relay.rpc_id(1, bytes)

	if _outgoing_batch.is_empty():
		return

	var batch := {"tn": _batch_tree_name, "events": _outgoing_batch.duplicate()}
	var batch_bytes := var_to_bytes(batch)
	_outgoing_batch.clear()

	if batch_bytes.size() > MAX_PAYLOAD_BYTES:
		NetLog.error("Relay: [FlushDropped] Batch too large (%d bytes)" % batch_bytes.size())
		return

	if multiplayer.is_server():
		_update_caches(batch, _batch_tree_name, batch_bytes)
		_dispatch(batch_bytes)
	else:
		forward_to_relay.rpc_id(1, batch_bytes)


@rpc("any_peer", "call_remote", "reliable")
func forward_to_relay(batch_bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	
	if sender != 1 and not multiplayer.get_peers().has(sender):
		return

	if batch_bytes.size() > MAX_PAYLOAD_BYTES:
		return

	var batch: Dictionary = bytes_to_var(batch_bytes)
	var source_tn: String = batch.get("tn", "?")
	
	_update_caches(batch, source_tn, batch_bytes)
	_dispatch(batch_bytes)


@rpc("authority", "call_remote", "reliable")
func forward_to_peer(batch_bytes: PackedByteArray) -> void:
	if not EngineDebugger.is_active():
		return

	var batch: Dictionary = bytes_to_var(batch_bytes)
	var source_tn: String = batch.get("tn", "")
	for event: Array in batch.get("events", []):
		EngineDebugger.send_message("networked:relay_forward", [{
			"msg": event[0],
			"data": event[1],
			"source_tree_name": source_tn,
		}])


@rpc("any_peer", "call_remote", "reliable")
func register_as_recipient(token: String = "") -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender == 0:
		sender = multiplayer.get_unique_id()
		if sender == 0: return
	
	var expected: String = ProjectSettings.get_setting("networked/debug/relay_token", "")
	if not expected.is_empty() and token != expected:
		return
	
	if sender not in _recipients:
		NetLog.info("Relay: [RegisterSuccess] Registered peer %d as debug recipient" % sender)
		_recipients.append(sender)
		# Update the global registry so new relays in THIS process pick it up.
		if sender not in NetworkedDebugReporter._process_recipients:
			NetworkedDebugReporter._process_recipients.append(sender)
	
	var my_id := multiplayer.get_unique_id()
	
	# REPLAY HISTORY: Send all cached registration AND the recent event history
	# to the new recipient so they catch up on what they missed.
	NetLog.info("Relay: [FastForward] Replaying history for peer %d" % sender)
	
	# 1. Replay registrations first so trees exist in the UI.
	for cached: PackedByteArray in _session_cache.values():
		if sender == my_id: forward_to_peer(cached)
		else: forward_to_peer.rpc_id(sender, cached)
	
	# 2. Replay recent event history.
	for tn in _history_cache:
		var history: Array = _history_cache[tn]
		for cached: PackedByteArray in history:
			if sender == my_id: forward_to_peer(cached)
			else: forward_to_peer.rpc_id(sender, cached)


func authorize(peer_id: int) -> void:
	if peer_id not in _authorized_senders:
		_authorized_senders.append(peer_id)


func local_dispatch(msg: String, data: Dictionary, source_tree_name: String) -> void:
	var batch := {"tn": source_tree_name, "events": [[msg, data]]}
	var bytes := var_to_bytes(batch)
	
	_update_caches(batch, source_tree_name, bytes)
	
	for recipient_id: int in _recipients:
		if recipient_id == multiplayer.get_unique_id():
			if EngineDebugger.is_active():
				EngineDebugger.send_message("networked:relay_forward", [{
					"msg": msg, "data": data, "source_tree_name": source_tree_name,
				}])
		else:
			# Safety: if the peer is gone, don't try to RPC.
			if multiplayer.get_peers().has(recipient_id) or recipient_id == 1:
				forward_to_peer.rpc_id(recipient_id, bytes)


func deauthorize(peer_id: int) -> void:
	_authorized_senders.erase(peer_id)
	_recipients.erase(peer_id)
	NetworkedDebugReporter._process_recipients.erase(peer_id)


func _dispatch(batch_bytes: PackedByteArray) -> void:
	var my_id := multiplayer.get_unique_id()
	for recipient_id: int in _recipients:
		if recipient_id == my_id:
			forward_to_peer(batch_bytes)
		else:
			if multiplayer.get_peers().has(recipient_id) or recipient_id == 1:
				forward_to_peer.rpc_id(recipient_id, batch_bytes)


func _update_caches(batch: Dictionary, tree_name: String, batch_bytes: PackedByteArray) -> void:
	if tree_name.is_empty(): return
	
	var is_registration := false
	# Update session cache (permanent per unique tree instance)
	for event: Array in batch.get("events", []):
		if event.size() >= 2 and event[0] == "networked:session_registered":
			var rid: String = event[1].get("_rid", "")
			if not rid.is_empty():
				# Key by RID + TreeName so multiple trees per process don't overwrite each other.
				var cache_key := "%s:%s" % [rid, tree_name]
				_session_cache[cache_key] = batch_bytes
				is_registration = true
			break
	
	if is_registration:
		return # Don't add registration batches to history buffer
		
	# Update history cache (rolling window for late-join catch-up)
	if not _history_cache.has(tree_name):
		_history_cache[tree_name] = []
	
	var history: Array = _history_cache[tree_name]
	history.append(batch_bytes)
	if history.size() > HISTORY_CACHE_LIMIT:
		history.pop_front()
