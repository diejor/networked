## Base class for all networked addon components.
##
## Provides ergonomic instance-method access to the session's [MultiplayerTree],
## [MultiplayerLobbyManager], [TPLayerAPI], and [PeerContext] buckets — replacing
## the old [code]NetworkedAPI[/code] static helper.
##
## The lookup chain is: [code]node.multiplayer[/code] (session-scoped [SceneMultiplayer])
## → metadata key [code]_multiplayer_tree[/code] → [MultiplayerTree] instance.
## This is path-independent and safe across node renames.
class_name NetComponent
extends Node

var _cached_module_path: String = ""

## Returns the [MultiplayerTree] that owns this component's multiplayer session.
## Returns [code]null[/code] if called before [method MultiplayerTree.host] /
## [method MultiplayerTree.join] completes, or in the editor.
func get_multiplayer_tree() -> MultiplayerTree:
	var api := multiplayer as SceneMultiplayer
	if not api:
		return null
	if api.has_meta(&"_multiplayer_tree"):
		return api.get_meta(&"_multiplayer_tree") as MultiplayerTree
	return null


## Returns the [MultiplayerLobbyManager] for this session.
func get_lobby_manager() -> MultiplayerLobbyManager:
	var tree := get_multiplayer_tree()
	return tree.lobby_manager if tree else null


## Returns the [TPLayerAPI] for visual teleport transitions on the local client.
## Always returns [code]null[/code] on the server.
func get_tp_layer() -> TPLayerAPI:
	if not is_inside_tree() or not multiplayer or multiplayer.is_server():
		return null
	var manager := get_lobby_manager()
	return manager.tp_layer if manager else null


## Returns the [NetworkClock] for this session.
func get_network_clock() -> NetworkClock:
	var tree := get_multiplayer_tree()
	return tree.clock if tree else null


## Returns the [PeerContext] for [param peer_id], defaulting to the local peer.
func get_peer_context(peer_id: int = -1) -> PeerContext:
	if peer_id == -1:
		if not is_inside_tree() or not multiplayer:
			return null
		peer_id = multiplayer.get_unique_id()
	
	var tree := get_multiplayer_tree()
	return tree.get_peer_context(peer_id) if tree else null


## Returns the typed bucket for [param bucket_type] from the local peer's context.
## Shorthand for [code]get_peer_context().get_bucket(bucket_type)[/code].
func get_bucket(bucket_type) -> RefCounted:
	var ctx := get_peer_context()
	return ctx.get_bucket(bucket_type) if ctx else null


#region ── Logging Proxy ──────────────────────────────────────────────────────

## Lazily resolves and caches the LOGL module path for this script.
func _get_cached_module_path() -> String:
	if _cached_module_path.is_empty():
		_cached_module_path = NetLog._module_from_path(get_script().resource_path)
	
	return _cached_module_path

## Logs a [code]TRACE[/code] message with rich multiplayer context.
func log_trace(msg: Variant, args: Array = []) -> void:
	_log_proxy(NetLog.Level.TRACE, msg, args)


## Logs a [code]DEBUG[/code] message with rich multiplayer context.
func log_debug(msg: Variant, args: Array = []) -> void:
	_log_proxy(NetLog.Level.DEBUG, msg, args)


## Logs an [code]INFO[/code] message with rich multiplayer context.
func log_info(msg: Variant, args: Array = []) -> void:
	_log_proxy(NetLog.Level.INFO, msg, args)


## Logs a [code]WARN[/code] message with rich multiplayer context.
func log_warn(msg: Variant, args: Array = []) -> void:
	_log_proxy(NetLog.Level.WARN, msg, args)


## Logs an [code]ERROR[/code] message with rich multiplayer context.
func log_error(msg: Variant, args: Array = []) -> void:
	_log_proxy(NetLog.Level.ERROR, msg, args)


func _log_proxy(level: int, msg: Variant, args: Array) -> void:
	if not NetLog.is_level_active_for_module(level, _get_cached_module_path()):
		return

	var resolved_msg: String
	if typeof(msg) == TYPE_CALLABLE:
		resolved_msg = str(msg.call())
	else:
		resolved_msg = str(msg)

	var tree := get_multiplayer_tree()
	var has_mp := is_inside_tree() and multiplayer
	
	var full_msg: String
	if not tree and not has_mp:
		full_msg = resolved_msg
	else:
		var tree_id := "MT:" + tree.name if tree else "null"
		var side_label := "?"
		var auth_label := "?"
		var is_local_auth := false
		
		if has_mp:
			var local_id := multiplayer.get_unique_id()
			var auth_id := get_multiplayer_authority()
			
			side_label = "S" if local_id == 1 else "C%d" % local_id
			auth_label = "S" if auth_id == 1 else "C%d" % auth_id
			is_local_auth = (local_id == auth_id)
		
		var context := "[%s*]" % side_label if is_local_auth else "[%s:%s]" % [side_label, auth_label]
		var display_name := owner.name.split("|")[0] if owner else ""
		var player_label := ("{%s}" % display_name) if not display_name.is_empty() else ""
		
		full_msg = "%s[%s]%s %s" % [context, tree_id, player_label, resolved_msg]
	
	match level:
		NetLog.Level.TRACE: NetLog.trace(full_msg, args)
		NetLog.Level.DEBUG: NetLog.debug(full_msg, args)
		NetLog.Level.INFO: NetLog.info(full_msg, args)
		NetLog.Level.WARN: NetLog.warn(func(): push_warning(full_msg % args))
		NetLog.Level.ERROR: NetLog.error(func(): push_error(full_msg % args))

#endregion


#region ── Debug Events ───────────────────────────────────────────────────────

## Opens a new general-purpose span for this component.
func _begin_span(label: String, meta: Dictionary = {}) -> NetSpan:
	var tree := get_multiplayer_tree()
	return NetTrace.begin(label, meta, tree.name if tree else "")


## Opens a new peer-aware span for a multiplayer operation.
func _begin_peer_span(label: String, peers: Array = [], meta: Dictionary = {}) -> NetPeerSpan:
	var tree := get_multiplayer_tree()
	return NetTrace.begin_peer(label, peers, meta, tree.name if tree else "")


#endregion
