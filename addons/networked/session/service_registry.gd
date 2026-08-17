## The session's service registry, keyed by script type.
##
## A service is a [Node] a session offers to its own code by type rather than by
## path, so a scene can find the one gamestate or lobby director without knowing
## where it sits. The registry holds one instance per script, and re-registering
## the identical instance is a no-op so a [NetwService] that auto-registers on
## [method Node._enter_tree] can also be registered explicitly.
## [codeblock]
## api.services.register(gamestate)
## var gs := api.services.of(BomberGamestate) as BomberGamestate
##
## # Every service deriving from a base, in registration order.
## for director in api.services.all(LobbyDirector):
##     director.refresh()
## [/codeblock]
##
## The verbs are short because the receiver already carries the noun.
## [method of] carries that name rather than the shorter read verb because
## [method Object.get] is engine-defined with an incompatible signature and
## cannot be overridden.
##
## Registering announces through [signal NetwMultiplayer.service_registered]
## whichever door it arrives at, this one or
## [method NetwMultiplayer.register_service].
class_name NetwServiceRegistry
extends RefCounted

# The owning NetwMultiplayer. A weakref because the owner holds this interface
# strongly and both are reference counted.
var _api_ref: WeakRef


func _init(api: NetwMultiplayer = null) -> void:
	_api_ref = weakref(api) if api else null


func _core() -> NetwMultiplayerCore:
	var api := _api_ref.get_ref() as NetwMultiplayer if _api_ref else null
	return api._native_core if api else null


## Registers [param service] under [param type], defaulting to its own script.
##
## Idempotent for the same instance, so a [NetwService] that auto-registers on
## [method Node._enter_tree] can also be registered explicitly.
func register(service: Node, type: Script = null) -> void:
	var core := _core()
	if core:
		core.service_register(type, service)


## Unregisters [param service] from [param type].
func unregister(service: Node, type: Script = null) -> void:
	var core := _core()
	if core:
		core.service_unregister(type, service)


## Returns the service registered for [param type], or [code]null[/code].
func of(type: Script) -> Node:
	var core := _core()
	return core.service_of(type) as Node if core else null


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
func all(base: Script) -> Array[Node]:
	var core := _core()
	var out: Array[Node] = []
	if core == null:
		return out
	for service: Node in core.service_all(base):
		out.append(service)
	return out


## Clears all registered services.
func clear() -> void:
	var core := _core()
	if core:
		core.service_clear()
