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
func info(arg1: Variant, arg2: Variant = null, arg3: Variant = null) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.info(c, arg1, arg2, arg3)


## Logs a [code]DEBUG[/code] message.
func debug(arg1: Variant, arg2: Variant = null, arg3: Variant = null) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.debug(c, arg1, arg2, arg3)


## Logs a [code]TRACE[/code] message.
func trace(arg1: Variant, arg2: Variant = null, arg3: Variant = null) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.trace(c, arg1, arg2, arg3)


## Logs a [code]WARN[/code] message and calls [code]push_warning[/code].
func warn(arg1: Variant, arg2: Variant = null, arg3: Variant = null) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.warn(c, arg1, arg2, arg3)


## Logs an [code]ERROR[/code] message and calls [code]push_error[/code].
func error(arg1: Variant, arg2: Variant = null, arg3: Variant = null) -> void:
	var c := _comp_ref.get_ref() as Node
	if c: Netw.dbg.error(c, arg1, arg2, arg3)


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
