@tool
## Static access point for Networked debug functionality.
##
## Provides consolidated logging, span creation, and component handles.
## All state is static; do not instantiate directly.
class_name NetwDbg
extends Object


## Creates a [NetwHandle] for O(1) debug access from a [NetComponent].
func handle(component: Node) -> NetwHandle:
	return NetwHandle.new(component)


## Logs an [code]INFO[/code] message.
## Accepts [code](msg: String)[/code] or [code](component: NetComponent, msg: String)[/code].
func info(arg1: Variant, arg2: Variant = null) -> void:
	_log(NetLog.Level.INFO, arg1, arg2)


## Logs a [code]DEBUG[/code] message.
## Accepts [code](msg: String)[/code] or [code](component: NetComponent, msg: String)[/code].
func debug(arg1: Variant, arg2: Variant = null) -> void:
	_log(NetLog.Level.DEBUG, arg1, arg2)


## Logs a [code]TRACE[/code] message.
## Accepts [code](msg: String)[/code] or [code](component: NetComponent, msg: String)[/code].
func trace(arg1: Variant, arg2: Variant = null) -> void:
	_log(NetLog.Level.TRACE, arg1, arg2)


## Logs a [code]WARN[/code] message and calls [code]push_warning[/code].
## Accepts [code](msg: String)[/code] or [code](component: NetComponent, msg: String)[/code].
func warn(arg1: Variant, arg2: Variant = null, link_call: Callable = Callable()) -> void:
	_log(NetLog.Level.WARN, arg1, arg2, link_call)


## Logs an [code]ERROR[/code] message and calls [code]push_error[/code].
## Accepts [code](msg: String)[/code] or [code](component: NetComponent, msg: String)[/code].
func error(arg1: Variant, arg2: Variant = null, link_call: Callable = Callable()) -> void:
	_log(NetLog.Level.ERROR, arg1, arg2, link_call)


## Opens a new general-purpose [NetSpan].
func span(context: Object, label: String, meta: Dictionary = {}, follows_from: CheckpointToken = null) -> NetSpan:
	return NetTrace.begin(label, context, meta, "", follows_from)


## Opens a new peer-aware [NetPeerSpan].
func peer_span(context: Object, label: String, peers: Array = [], meta: Dictionary = {}, token: CheckpointToken = null) -> NetPeerSpan:
	return NetTrace.begin_peer(label, peers, context, meta, "", token)


func _log(level: int, arg1: Variant, arg2: Variant, link_call: Callable = Callable()) -> void:
	var component: Node = null
	var msg: String
	
	if typeof(arg1) == TYPE_OBJECT:
		component = arg1 as Node
		msg = str(arg2)
	else:
		msg = str(arg1)
	
	if component:
		var peer_id := -1
		if component.is_inside_tree() and component.multiplayer:
			peer_id = component.multiplayer.get_unique_id()
		
		var peer_label := "S" if peer_id == 1 else "C%d" % peer_id if peer_id > 0 else "?"
		var owner_name := component.owner.name if component.owner else component.name
		
		var cls_name := ""
		var script := component.get_script() as Script
		if script:
			cls_name = script.resource_path.get_file().get_basename()
		else:
			cls_name = component.get_class()
		
		msg = "[%s] [%s] [%s] %s" % [peer_label, owner_name, cls_name, msg]
	
	match level:
		NetLog.Level.TRACE: NetLog.trace(msg)
		NetLog.Level.DEBUG: NetLog.debug(msg)
		NetLog.Level.INFO: NetLog.info(msg)
		NetLog.Level.WARN: NetLog.warn(msg, [], link_call)
		NetLog.Level.ERROR: NetLog.error(msg, [], link_call)
