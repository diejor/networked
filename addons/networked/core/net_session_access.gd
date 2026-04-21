## Thin facade over [MultiplayerTree] exposing only session-service access.
##
## Components obtain this via [method NetComponent.get_session] rather than
## holding a direct reference to [MultiplayerTree]. This keeps component code
## off the concrete MT class (backend, multiplayer_api, etc.) while preserving
## the existing service API unchanged.
##
## Holds a [WeakRef] so components that cache an instance survive tree teardown
## without keeping the MT alive.
class_name NetSessionAccess
extends RefCounted

var _mt_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_mt_ref = weakref(mt)


## True while the underlying [MultiplayerTree] is still alive.
func is_valid() -> bool:
	return is_instance_valid(_mt_ref.get_ref())


func get_lobby_manager() -> MultiplayerLobbyManager:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt: return null
	return mt.get_service(MultiplayerLobbyManager)


func get_clock() -> NetworkClock:
	var mt: MultiplayerTree = _mt_ref.get_ref()
	if not mt: return null
	return mt.get_service(NetworkClock)


func get_peer_context(peer_id: int) -> PeerContext:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_peer_context(peer_id) if mt else null


func get_service(type: Script) -> Node:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_service(type) if mt else null


func is_server() -> bool:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.is_server if mt else false


func get_unique_id() -> int:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.multiplayer_api.get_unique_id() if mt and mt.multiplayer_api else 0


func get_tree_name() -> String:
	var mt := _mt_ref.get_ref() as MultiplayerTree
	return mt.get_meta(&"_original_name", mt.name) if mt else ""


func begin_span(label: String, meta: Dictionary = {}) -> NetSpan:
	return NetTrace.begin(label, _mt_ref.get_ref(), meta)


func begin_peer_span(label: String, peers: Array = [], meta: Dictionary = {}) -> NetPeerSpan:
	return NetTrace.begin_peer(label, peers, _mt_ref.get_ref(), meta)
