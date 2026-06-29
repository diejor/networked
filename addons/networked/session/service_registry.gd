class_name ServiceRegistry
extends RefCounted
## Internal registry for [MultiplayerTree] session services.
##
## Stores service nodes by script type while [MultiplayerTree] remains the
## public API surface and owner of node-tree searches.

var _services: Dictionary[Script, Node] = { }


## Registers [param service] for [param type].
func register_service(service: Node, type: Script = null) -> void:
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
func unregister_service(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()

	if _services.get(type) == service:
		_services.erase(type)
		Netw.dbg.debug("Service %s unregistered.", [type.get_global_name()])


## Returns the service registered for [param type], or [code]null[/code].
func get_service(type: Script) -> Node:
	return _services.get(type)


## Returns every registered service whose script is [param base] or a
## subclass of it, in registration order.
func get_services(base: Script) -> Array[Node]:
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
