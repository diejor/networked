## Base class for all networked addon components.
##
## Provides instance-method access to session services by resolving the
## component's [NetwMultiplayer] through [method Netw.of], which stays valid
## across node renames.
@icon("res://addons/networked/assets/NetwComponent.svg")
class_name NetwComponent
extends Node

## Returns the [MultiplayerTree] that owns this component's multiplayer session.
func get_multiplayer_tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)


## Returns the [MultiplayerSceneManager] for this session, resolved through the
## service registry like any other [NetwService].
func get_scene_manager() -> MultiplayerSceneManager:
	var api := Netw.of(self)
	return api.get_service(MultiplayerSceneManager) as MultiplayerSceneManager if api else null


## Returns the [TPLayerAPI] for visual teleport transitions on the local
## client. Dedicated servers return [code]null[/code]; listen-server hosts
## can still return a layer because they also render a local client view.
func get_tp_layer() -> TPLayerAPI:
	if not is_inside_tree() or not multiplayer:
		return null
	var api := Netw.of(self)
	if not api or not api.is_local_client:
		return null
	return api.get_service(TPLayerAPI) as TPLayerAPI


## Returns the [NetwClockHandle] for this session.
func get_multiplayer_clock() -> NetwClockHandle:
	var api := Netw.of(self)
	if not api or not api.clock.is_configured:
		return null
	return api._native_core.clock_handle


## Returns the session service registered for [param type], or
## [code]null[/code].
func get_service(type: Script) -> Node:
	var api := Netw.of(self)
	return api.get_service(type) if api else null


## Returns the [NetwPeerContext] for the local peer.
func get_peer_context() -> NetwPeerContext:
	var api := Netw.of(self)
	if not api:
		return null
	return api.peer_get_context(self.multiplayer.get_unique_id())


## Returns the typed bucket for [param bucket_type] from the local peer's
## context.
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
