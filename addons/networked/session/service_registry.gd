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
class_name NetwServiceRegistry
extends RefCounted

var _services: Dictionary[Script, Node] = { }


## Registers [param service] under [param type], defaulting to its own script.
func register(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()

	# Idempotent: re-registering the identical instance (e.g. NetwService's
	# auto-register on _enter_tree followed by an explicit register_service from
	# get_nakama_session) is a no-op, not an overwrite. Only a genuinely different
	# instance under the same key is a conflict worth warning about.
	var prior: Node = _services.get(type)
	if prior == service:
		return
	if prior != null:
		Netw.dbg.warn(
			"Service %s already registered - overwriting.",
			[type.get_global_name()],
			func(m): push_warning(m)
		)

	_services[type] = service
	Netw.dbg.debug("Service %s registered.", [type.get_global_name()])


## Unregisters [param service] from [param type].
func unregister(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()

	if _services.get(type) == service:
		_services.erase(type)
		Netw.dbg.debug("Service %s unregistered.", [type.get_global_name()])


## Returns the service registered for [param type], or [code]null[/code].
func of(type: Script) -> Node:
	return _services.get(type)


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
func all(base: Script) -> Array[Node]:
	var out: Array[Node] = []
	for type in _services:
		if _script_is_a(type, base):
			out.append(_services[type])
	return out


# Walks the script base chain to test whether [param script] derives from
# [param base].
static func _script_is_a(script: Script, base: Script) -> bool:
	var s := script
	while s:
		if s == base:
			return true
		s = s.get_base_script()
	return false


## Clears all registered services.
func clear() -> void:
	_services.clear()
