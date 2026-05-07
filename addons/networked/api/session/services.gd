## Service locator facade over [MultiplayerTree].
##
## Provides access to backend systems such as [MultiplayerSceneManager]
## and [NetworkClock] without exposing the concrete [MultiplayerTree] class
## to component code.
## [br][br]
## [b]Custom Services[/b]
##
## Register your own session services with
## [method register] and [method unregister]:
## [codeblock]
## class_name MyLobbyManager
## extends Node
##
## func _enter_tree() -> void:
##     NetwServices.register(self)
##
## func _exit_tree() -> void:
##     NetwServices.unregister(self)
## [/codeblock]
##
## Retrieve from any component via [member NetwContext.services]:
## [codeblock]
## var ctx := get_context()
## var lobby: MyLobbyManager = \
##         ctx.services.get_service(MyLobbyManager)
## [/codeblock]
##
## [br][br]
## Obtain via [member NetwContext.services] rather than constructing directly.
## Holds a [WeakRef] so cached references do not keep the tree alive.
class_name NetwServices
extends RefCounted

var _tree_ref: WeakRef


func _init(mt: MultiplayerTree) -> void:
	_tree_ref = weakref(mt)


## Returns [code]true[/code] while the underlying [MultiplayerTree] is still
## alive.
func is_valid() -> bool:
	return is_instance_valid(_tree_ref.get_ref())


## Returns the [MultiplayerSceneManager] service, or [code]null[/code].
func get_scene_manager() -> MultiplayerSceneManager:
	var mt: MultiplayerTree = _tree_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(MultiplayerSceneManager)


## Returns the [NetworkClock] service, or [code]null[/code].
func get_clock() -> NetworkClock:
	var mt: MultiplayerTree = _tree_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(NetworkClock)


## Returns the [TPLayerAPI] for visual teleport transitions on the local
## client, or [code]null[/code].
func get_tp_layer() -> TPLayerAPI:
	var mt: MultiplayerTree = _tree_ref.get_ref()
	if not mt:
		return null
	return mt.get_service(TPLayerAPI)


## Returns the [NetwPeerContext] for [param peer_id].
func get_peer_context(peer_id: int) -> NetwPeerContext:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_peer_context(peer_id) if mt else null


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	var mt := _tree_ref.get_ref() as MultiplayerTree
	return mt.get_service(type) if mt else null


# ---------------------------------------------------------------------------
# Static registration helpers
# ---------------------------------------------------------------------------


## Registers [param service] as a session service.
##
## Walks [param service]'s ancestor chain to find the owning
## [MultiplayerTree], then calls [method MultiplayerTree.register_service].
## [br][br]
## Call this in [code]_enter_tree[/code]:
## [codeblock]
## func _enter_tree() -> void:
##     var mt := NetwServices.register(self)
## [/codeblock]
## If [param type] is [code]null[/code], the service's script class is
## used as the registration key.
## [br][br]
## Returns the owning [MultiplayerTree], or [code]null[/code] if
## [param service] is not a descendant of one.
static func register(service: Node, type: Script = null) -> MultiplayerTree:
	var mt := MultiplayerTree.resolve(service)
	if is_instance_valid(mt):
		mt.register_service(service, type)
	return mt


## Unregisters [param service] from the session.
##
## Call this in [code]_exit_tree[/code]:
## [codeblock]
## func _exit_tree() -> void:
##     NetwServices.unregister(self)
## [/codeblock]
## [br][br]
## Returns the owning [MultiplayerTree], or [code]null[/code] if
## [param service] is not a descendant of one.
static func unregister(service: Node, type: Script = null) -> MultiplayerTree:
	var mt := MultiplayerTree.resolve(service)
	if is_instance_valid(mt):
		mt.unregister_service(service, type)
	return mt
