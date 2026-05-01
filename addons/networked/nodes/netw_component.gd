## Base class for all networked addon components.
##
## Provides ergonomic instance-method access to the session's services via
## [NetwContext] — replacing the old [code]NetworkedAPI[/code] static helper.
##
## The lookup chain is: [code]node.multiplayer[/code] (session-scoped [SceneMultiplayer])
## → metadata key [code]_multiplayer_tree[/code] → [MultiplayerTree] instance.
## This is path-independent and safe across node renames.
class_name NetwComponent
extends Node

var _context: NetwContext


## Returns a [NetwContext] for this component's multiplayer session.
## Returns [code]null[/code] if called before [method MultiplayerTree.host] /
## [method MultiplayerTree.join] completes, or in the editor.
func get_context() -> NetwContext:
	var scene := MultiplayerTree.scene_for_node(self)
	if is_instance_valid(scene):
		return scene.get_context()
	var mt := MultiplayerTree.resolve(self)
	if not mt:
		return null
	if _context == null or not _context.is_valid():
		_context = NetwContext.new(mt)
	return _context


## Returns the [MultiplayerTree] that owns this component's multiplayer session.
## Prefer [method get_context] for new code; this shim exists for compatibility.
func get_multiplayer_tree() -> MultiplayerTree:
	return MultiplayerTree.resolve(self)


## Returns the [MultiplayerSceneManager] for this session.
func get_scene_manager() -> MultiplayerSceneManager:
	var ctx := get_context()
	return ctx.tree.get_scene_manager() if ctx else null


## Returns the [TPLayerAPI] for visual teleport transitions on the local client.
## Always returns [code]null[/code] on the server.
func get_tp_layer() -> TPLayerAPI:
	if not is_inside_tree() or not multiplayer or multiplayer.is_server():
		return null
	var ctx := get_context()
	if not ctx: return null
	var tp_layer: TPLayerAPI = ctx.tree.get_service(TPLayerAPI)
	return tp_layer


## Returns the [NetworkClock] for this session.
func get_network_clock() -> NetworkClock:
	var ctx := get_context()
	return ctx.tree.get_clock() if ctx else null


## Returns the [NetwPeerContext] for the local peer.
func get_peer_context() -> NetwPeerContext:
	var ctx := get_context()
	if not ctx:
		return null
	return ctx.tree.get_peer_context(ctx.tree.get_unique_id())


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
