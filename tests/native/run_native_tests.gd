extends SceneTree

func _initialize() -> void:
	if not ClassDB.class_exists(&"NetwNativeTests"):
		push_error(
			"The embedded suite is absent. Build with: "
			+ "scons -C extension netw_tests=yes",
		)
		quit(2)
		return
	var runner: Object = ClassDB.instantiate(&"NetwNativeTests")
	var filter := "*"
	var cells_path := ""
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--native-filter="):
			filter = argument.trim_prefix("--native-filter=")
		elif argument == "--netw-list-cells":
			cells_path = "res://reports/native/cells.txt"
	var result: Dictionary = runner.run(
		filter,
		"res://reports/native/results.xml",
		cells_path,
	)
	_write_wire_spec(runner)
	print("NATIVE_JUNIT ", result[&"report_path"])
	if not cells_path.is_empty():
		print("NATIVE_CELLS ", cells_path, " ", result[&"cells"])
	quit(result[&"exit_code"])


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
