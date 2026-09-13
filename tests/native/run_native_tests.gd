extends SceneTree

var _runner: Object = null


# The suite runs from a physics frame rather than from _initialize because the
# root is not inside the tree until after initialization, and because a
# scenario that needs the physics server to step is driven one advance per
# callback: the engine steps the space after this returns.
func _physics_process(_delta: float) -> bool:
	if _runner == null:
		if not ClassDB.class_exists(&"NetwNativeTests"):
			push_error(
				"The embedded suite is absent. Build with: "
				+ "scons -C extension netw_tests=yes",
			)
			quit(2)
			return true
		_runner = ClassDB.instantiate(&"NetwNativeTests")
	if _runner.frame_advance():
		return false
	var filter := "*"
	var cells_path := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--native-filter="):
			filter = argument.trim_prefix("--native-filter=")
		elif argument == "--netw-list-cells":
			cells_path = "res://reports/native/cells.txt"
	var result: Dictionary = _runner.run(
		filter,
		"res://reports/native/results.xml",
		cells_path,
	)
	_write_wire_spec(_runner)
	print("NATIVE_JUNIT ", result[&"report_path"])
	if not cells_path.is_empty():
		print("NATIVE_CELLS ", cells_path, " ", result[&"cells"])
	quit(result[&"exit_code"])
	return true


# Written beside the JUnit because it is this run's output: a spec taken from a
# different build describes a different format.
func _write_wire_spec(runner: Object) -> void:
	var spec: Dictionary = runner.wire_spec()
	var path := "res://reports/native/wire.spec.json"
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("Cannot write %s" % path)
		return
	file.store_string(JSON.stringify(spec, "  ", true))
	print("NATIVE_WIRE_SPEC ", path, " ", spec[&"records"].size())
