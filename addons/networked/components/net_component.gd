## Base class for all networked addon components.
##
## Provides ergonomic instance-method access to the session's services via
## [NetSessionAccess] — replacing the old [code]NetworkedAPI[/code] static helper.
##
## The lookup chain is: [code]node.multiplayer[/code] (session-scoped [SceneMultiplayer])
## → metadata key [code]_multiplayer_tree[/code] → [MultiplayerTree] instance.
## This is path-independent and safe across node renames.
class_name NetComponent
extends Node

var _session: NetSessionAccess


## Returns a [NetSessionAccess] for this component's multiplayer session.
## Returns [code]null[/code] if called before [method MultiplayerTree.host] /
## [method MultiplayerTree.join] completes, or in the editor.
func get_session() -> NetSessionAccess:
	if _session == null or not _session.is_valid():
		var mt := MultiplayerTree.resolve(self)
		_session = NetSessionAccess.new(mt) if mt else null
	return _session


## Returns the [MultiplayerTree] that owns this component's multiplayer session.
## Prefer [method get_session] for new code; this shim exists for compatibility.
func get_multiplayer_tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)


## Returns the [MultiplayerLobbyManager] for this session.
func get_lobby_manager() -> MultiplayerLobbyManager:
	var s: NetSessionAccess = get_session()
	return s.get_lobby_manager() if s else null


## Returns the [TPLayerAPI] for visual teleport transitions on the local client.
## Always returns [code]null[/code] on the server.
func get_tp_layer() -> TPLayerAPI:
	if not is_inside_tree() or not multiplayer or multiplayer.is_server():
		return null
	var s: NetSessionAccess = get_session()
	if not s: return null
	var tp_layer: TPLayerAPI = s.get_service(TPLayerAPI)
	return tp_layer


## Returns the [NetworkClock] for this session.
func get_network_clock() -> NetworkClock:
	var s: NetSessionAccess = get_session()
	return s.get_clock() if s else null


## Returns the [PeerContext] for [param peer_id], defaulting to the local peer.
func get_peer_context(peer_id: int = -1) -> PeerContext:
	if peer_id == -1:
		if not is_inside_tree() or not multiplayer:
			return null
		peer_id = multiplayer.get_unique_id()
	var s := get_session()
	return s.get_peer_context(peer_id) if s else null


## Returns the typed bucket for [param bucket_type] from the local peer's context.
## Shorthand for [code]get_peer_context().get_bucket(bucket_type)[/code].
func get_bucket(bucket_type) -> RefCounted:
	var ctx := get_peer_context()
	return ctx.get_bucket(bucket_type) if ctx else null


# [b]Note on Debugging:[/b]
# This component does not provide direct logging or span methods. Use [Netw.dbg]
# or [method NetwDbg.handle] instead.
#
# [b]Editor Jump-to-Line Convention:[/b]
# To preserve the Godot editor's ability to jump to the correct line when
# clicking an error or warning in the Output panel, you MUST pass a lambda
# that calls push_error() or push_warning() to the debug call:
# [codeblock]
# Netw.dbg.error(self, "Failed to load level", func(m): push_error(m))
#
# # Via handle:
# _dbg.error("Failed to load level", func(m): push_error(m))
# [/codeblock]
