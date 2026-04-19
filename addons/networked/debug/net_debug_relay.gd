## Game-side debug relay bridge — routes telemetry between game processes.
##
## Added as a child of [NetworkedDebugReporter] when [method OS.has_feature]
## returns [code]"debug"[/code] and a [MultiplayerTree] has registered.
## Only exists in debug builds; completely absent from release exports.
##
## [b]Topology:[/b] The relay always lives on the server (peer 1). Every process
## sends its telemetry to the server relay via [method forward_to_relay]. The relay
## checks authorization and payload size, then forwards the raw bytes to all
## registered editor-connected recipients via [method forward_to_peer]. Each
## recipient independently pipes the payload into [EngineDebugger] if it has an
## active session — the relay never deserializes the content.
##
## [b]Failure mode:[/b] When the server peer disconnects, [method is_relay_active]
## returns false and the reporter silently skips the relay path. Local
## [EngineDebugger] connections are unaffected.
@tool
class_name NetDebugRelay
extends Node

const MAX_PAYLOAD_BYTES := 65536

# Peer IDs of game peers allowed to send telemetry to this relay.
# Maintained by the reporter's peer_connected / peer_disconnected handlers.
var _authorized_senders: Array[int] = []

# Peer IDs of processes that have a live editor and want to receive relayed data.
# Self-populated via register_as_recipient RPC.
var _recipients: Array[int] = []


## Returns true when this node is inside the tree and the multiplayer peer is set.
## The reporter calls this before attempting to send via relay.
func is_relay_active() -> bool:
	return is_inside_tree() \
		and multiplayer != null \
		and multiplayer.has_multiplayer_peer()


## Sends [param msg] + [param data] through the relay.
##
## On the server this skips the round-trip RPC and dispatches directly.
## On clients it calls [method forward_to_relay] on the server (peer 1).
func send(msg: String, data: Dictionary, source_tree_name: String) -> void:
	if not is_relay_active():
		return
	var p := NetRelayPayload.new()
	p.msg = msg
	p.data = data
	p.source_tree_name = source_tree_name
	var bytes := p.to_bytes()
	if multiplayer.is_server():
		_dispatch(bytes)
	else:
		forward_to_relay.rpc_id(1, bytes)


## Called by any peer to deliver telemetry to the server-side relay.
@rpc("any_peer", "call_remote", "reliable")
func forward_to_relay(payload_bytes: PackedByteArray) -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender not in _authorized_senders:
		return
	if payload_bytes.size() > MAX_PAYLOAD_BYTES:
		return
	_dispatch(payload_bytes)


## Called by the server relay to deliver telemetry to an editor-connected peer.
@rpc("authority", "call_remote", "reliable")
func forward_to_peer(payload_bytes: PackedByteArray) -> void:
	if not EngineDebugger.is_active():
		return
	var p := NetRelayPayload.from_bytes(payload_bytes)
	EngineDebugger.send_message("networked:relay_forward", [{
		"msg": p.msg,
		"data": p.data,
		"source_tree_name": p.source_tree_name,
	}])


## Called by an editor-connected peer after connecting to register as a recipient.
## The server relay stores the sender's peer ID and forwards future payloads to it.
@rpc("any_peer", "call_remote", "reliable")
func register_as_recipient() -> void:
	var sender := multiplayer.get_remote_sender_id()
	if sender not in _recipients:
		_recipients.append(sender)


## Authorizes [param peer_id] to send telemetry through this relay.
## Called by the reporter when a peer connects to the server's MultiplayerTree.
func authorize(peer_id: int) -> void:
	if peer_id not in _authorized_senders:
		_authorized_senders.append(peer_id)


## Removes [param peer_id] from both the sender and recipient lists.
## Called by the reporter when a peer disconnects.
func deauthorize(peer_id: int) -> void:
	_authorized_senders.erase(peer_id)
	_recipients.erase(peer_id)


func _dispatch(payload_bytes: PackedByteArray) -> void:
	for rid: int in _recipients:
		forward_to_peer.rpc_id(rid, payload_bytes)
