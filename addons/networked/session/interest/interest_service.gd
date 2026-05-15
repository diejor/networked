## Transport node for [NetwInterest]. Owns the RPC handlers that replicate
## layer lifecycle and member membership from the server to clients.
##
## Layers are RefCounted, so they cannot host @rpc handlers directly. This
## node lives as a child of [MultiplayerTree] and acts as the wire. The
## [NetwInterest] facade on [member MultiplayerTree.interest] holds the
## authoritative state and forwards mutations through this service.
##
## [b]Only layer lifecycle and member peer-ids cross the wire.[/b] Subjects
## are server-authoritative and never replicated: clients learn about
## subjects implicitly via the entity nodes the engine replicates to them.
class_name InterestService
extends Node


var _interest: NetwInterest
var _mt: MultiplayerTree

# Per-layer observer set on the server. Layer id -> Dictionary[peer_id, bool].
# Observer == peer that has received `_rpc_layer_created` for this layer and
# should receive subsequent member updates. Currently observer == member,
# but kept separate so spectator semantics can be added later.
var _observers: Dictionary[StringName, Dictionary] = {}


func _enter_tree() -> void:
	if Engine.is_editor_hint():
		return
	_mt = MultiplayerTree.resolve(self)
	if not _mt:
		return
	_interest = _mt.interest
	if _interest:
		_interest._bind_service(self)


func _exit_tree() -> void:
	if Engine.is_editor_hint():
		return
	if _interest:
		_interest._unbind_service(self)
	_interest = null
	_observers.clear()


# ---------------------------------------------------------------------------
# Server-side outbound. Called by NetwInterest when a server-side mutation
# happens. Each method broadcasts to current observers and catches new
# observers up before sending the delta.
# ---------------------------------------------------------------------------

func notify_layer_created(layer: NetwInterestLayer) -> void:
	if not _is_server():
		return
	# No observers yet for ordinary layers. ROOT's observer set is seeded
	# by notify_member_added once peers start joining.


func notify_layer_disposed(layer_id: StringName) -> void:
	if not _is_server():
		return
	var obs := _observers.get(layer_id, {}) as Dictionary
	for peer_id in obs:
		_rpc_layer_disposed.rpc_id(peer_id, layer_id)
	_observers.erase(layer_id)


func notify_member_added(layer: NetwInterestLayer, peer_id: int) -> void:
	if not _is_server():
		return
	var obs: Dictionary = _observers.get_or_add(layer.id, {})
	var is_first_time := not obs.has(peer_id)
	if is_first_time:
		obs[peer_id] = true
		# Bootstrap the new observer with the layer and existing members.
		_rpc_layer_created.rpc_id(peer_id, layer.id, int(layer.policy))
		for existing_peer in layer.members():
			if existing_peer != peer_id:
				_rpc_member_added.rpc_id(peer_id, layer.id, existing_peer)
	# Broadcast the new member to every observer (including themselves).
	for observer_peer in obs:
		_rpc_member_added.rpc_id(observer_peer, layer.id, peer_id)


func notify_member_removed(layer_id: StringName, peer_id: int) -> void:
	if not _is_server():
		return
	var obs: Dictionary = _observers.get(layer_id, {})
	if obs.is_empty():
		return
	# Broadcast removal to all current observers (including the leaver, so
	# their local mirror sees themselves leave one last time).
	for observer_peer in obs:
		_rpc_member_removed.rpc_id(observer_peer, layer_id, peer_id)
	# Leaver loses observer status: tear down their mirror entirely.
	if obs.has(peer_id):
		obs.erase(peer_id)
		_rpc_layer_disposed.rpc_id(peer_id, layer_id)


# ---------------------------------------------------------------------------
# Inbound RPCs. Authority is the server; clients apply changes to their
# local read-only mirror.
# ---------------------------------------------------------------------------

@rpc("authority", "call_local", "reliable")
func _rpc_layer_created(layer_id: StringName, policy: int) -> void:
	if not _interest:
		return
	_interest._client_create_mirror(layer_id, policy)


@rpc("authority", "call_local", "reliable")
func _rpc_layer_disposed(layer_id: StringName) -> void:
	if not _interest:
		return
	_interest._client_dispose_mirror(layer_id)


@rpc("authority", "call_local", "reliable")
func _rpc_member_added(layer_id: StringName, peer_id: int) -> void:
	if not _interest:
		return
	var layer := _interest.layer(layer_id)
	if layer:
		layer._client_apply_member_added(peer_id)


@rpc("authority", "call_local", "reliable")
func _rpc_member_removed(layer_id: StringName, peer_id: int) -> void:
	if not _interest:
		return
	var layer := _interest.layer(layer_id)
	if layer:
		layer._client_apply_member_removed(peer_id)


func _is_server() -> bool:
	if not _mt or not _mt.api:
		return false
	return _mt.api.has_multiplayer_peer() and _mt.api.is_server()
