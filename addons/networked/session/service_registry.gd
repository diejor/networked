class_name ServiceRegistry
extends RefCounted
## Internal registry for [MultiplayerTree] session services.
##
## Stores service nodes by script type while [MultiplayerTree] remains the
## public API surface and owner of node-tree searches.

var _services: Dictionary[Script, Node] = {}


## Registers [param service] for [param type].
func register_service(service: Node, type: Script = null) -> void:
	if not type:
		type = service.get_script()
	
	if type in _services:
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


## Clears all registered services.
func clear() -> void:
	_services.clear()
