@tool
## Instance-method proxy for [NetwDbg] bound to a specific [NetComponent].
##
## Captures a weak reference to the component. If the component is freed, all
## calls become silent no-ops.
class_name NetwHandle
extends RefCounted


var _comp_ref: WeakRef


func _init(component: Node) -> void:
	_comp_ref = weakref(component)


## Logs an [code]INFO[/code] message.
func info(msg: String) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.info(c, msg)


## Logs a [code]DEBUG[/code] message.
func debug(msg: String) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.debug(c, msg)


## Logs a [code]TRACE[/code] message.
func trace(msg: String) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.trace(c, msg)


## Logs a [code]WARN[/code] message and calls [code]push_warning[/code].
func warn(msg: String, link_call: Callable = Callable()) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.warn(c, msg, link_call)


## Logs an [code]ERROR[/code] message and calls [code]push_error[/code].
func error(msg: String, link_call: Callable = Callable()) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.error(c, msg, link_call)


## Opens a new general-purpose [NetSpan].
func span(label: String, meta: Dictionary = {}, follows_from: CheckpointToken = null) -> NetSpan:
	var c := _comp_ref.get_ref() as Node
	if c: return Netw.dbg.span(c, label, meta, follows_from)
	return NetSpan.new(&"", label)


## Opens a new peer-aware [NetPeerSpan].
func peer_span(label: String, peers: Array = [], meta: Dictionary = {}, token: CheckpointToken = null) -> NetPeerSpan:
	var c := _comp_ref.get_ref() as Node
	if c: return Netw.dbg.peer_span(c, label, peers, meta, token)
	return NetPeerSpan.new(&"", label)
