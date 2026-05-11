## Stable [code]NetworkedDebugger[/code] autoload bootstrap.
##
## The bootstrap is cheap to load in every build. It creates the heavy reporter
## only when debug policy allows reporter activation.
extends Node

const REPORTER_PATH := "res://addons/networked/debug/core/reporter.gd"

var _reporter: Node


func _enter_tree() -> void:
	if _should_create_reporter():
		_create_reporter()
	else:
		Netw.dbg.register_reporter(self)


func _exit_tree() -> void:
	if _reporter:
		_reporter.queue_free()
		_reporter = null
	Netw.dbg.unregister_reporter(self)


## Enables or disables the heavy debug reporter.
func set_enabled(enabled: bool) -> void:
	if enabled:
		NetworkedDebugReporter.set_enabled(true)
		_create_reporter()
	elif _reporter:
		NetworkedDebugReporter.set_enabled(false)
		_reporter.queue_free()
		_reporter = null
		Netw.dbg.register_reporter(self)


## Resets the active reporter or the fallback trace state.
func reset_state() -> void:
	if _reporter:
		_reporter.reset_state()
	else:
		Netw.dbg.reset()


## Registers [param mt] with the active reporter.
func register_tree(mt: MultiplayerTree) -> void:
	if _reporter:
		_reporter.register_tree(mt)


## Unregisters [param mt] from the active reporter.
func unregister_tree(mt: MultiplayerTree) -> void:
	if _reporter:
		_reporter.unregister_tree(mt)


func _create_reporter() -> void:
	if _reporter:
		return
	if Netw.dbg.get_reporter() == self:
		Netw.dbg.unregister_reporter(self)
	var reporter_script := load(REPORTER_PATH) as Script
	_reporter = reporter_script.new()
	_reporter.name = "NetworkedDebugReporter"
	add_child(_reporter)


func _should_create_reporter() -> bool:
	if Netw.is_test_env():
		return false
	for arg in OS.get_cmdline_args():
		if arg == "--headless":
			return false
	if OS.has_feature("debug"):
		return true
	if Engine.is_editor_hint():
		return true
	return false
