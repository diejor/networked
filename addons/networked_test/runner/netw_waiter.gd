## Frame counted predicate waiter for networked test harnesses.
##
## [method until] reports timeout failures through an injected reporter so
## plain Godot and GdUnit callers can share the same polling behavior.
class_name NetwWaiter
extends RefCounted

const _FRAMES_PER_SECOND := 60

var _tree: SceneTree
var _reporter: Callable


func _init(tree: SceneTree, reporter: Callable) -> void:
	_tree = tree
	_reporter = reporter


## Polls [param cond] once per process frame until it passes or times out.
##
## Returns [code]true[/code] when [param timeout] expires. Returns
## [code]false[/code] when [param cond] passes.
func until(cond: Callable, label: String, timeout: float) -> bool:
	if cond.call():
		return false

	var budget := maxi(1, int(ceil(timeout * _FRAMES_PER_SECOND)))
	for i in budget:
		await _tree.process_frame
		if cond.call():
			return false

	_reporter.call(label, timeout)
	return true
