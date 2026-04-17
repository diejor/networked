## Abstract base class for client-side teleport transition overlays.
##
## Assign a concrete subclass instance to [member MultiplayerLobbyManager.tp_layer].
## Subclasses implement [method teleport_out] (fade/cover outgoing scene) and
## [method teleport_in] (reveal incoming scene). Both methods are awaitable.
@abstract
class_name TPLayerAPI
extends CanvasLayer

## Forwarded from [MultiplayerLobbyManager.configured]; used to free this node on the server.
signal configured

## Progress bar driven by the transition animation.
@export var transition_progress: TextureProgressBar
## [AnimationPlayer] that plays the teleport transition clip.
@export var transition_anim: AnimationPlayer


func _init() -> void:
	configured.connect(_on_multiplayer_configured)

## Plays the outgoing transition (cover the screen). Awaitable.
@abstract
func teleport_out() -> void

## Plays the incoming transition (reveal the screen). Awaitable.
@abstract
func teleport_in() -> void


func _on_multiplayer_configured() -> void:
	if multiplayer.is_server():
		queue_free()


#region ── Logging Proxy ──────────────────────────────────────────────────────

## Returns the [MultiplayerTree] that owns this component's multiplayer session.
func get_multiplayer_tree() -> MultiplayerTree:
	var api := multiplayer as SceneMultiplayer
	if not api:
		return null
	return api.get_meta(&"_multiplayer_tree", null) as MultiplayerTree


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
	var tree := get_multiplayer_tree()
	var has_mp := is_inside_tree() and multiplayer
	
	var full_msg: String
	if not tree and not has_mp:
		full_msg = str(msg)
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
		full_msg = "%s[%s]%s %s" % [context, tree_id, player_label, str(msg)]
	
	match level:
		NetLog.Level.TRACE: NetLog.trace(full_msg, args)
		NetLog.Level.DEBUG: NetLog.debug(full_msg, args)
		NetLog.Level.INFO: NetLog.info(full_msg, args)
		NetLog.Level.WARN: NetLog.warn(func(): push_warning(full_msg % args))
		NetLog.Level.ERROR: NetLog.error(func(): push_error(full_msg % args))

#endregion
