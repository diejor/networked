extends SceneTree

var runner: Object = null


func _physics_process(_delta: float) -> bool:
	if runner == null:
		if not ClassDB.class_exists(&"NetwNativeTests"):
			push_error(
				"The embedded suite is absent. Build with: "
				+ "scons -C extension netw_tests=yes",
			)
			quit(2)
			return true
		runner = ClassDB.instantiate(&"NetwNativeTests")
	if runner.frame_advance():
		return false
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
	var spec: Dictionary = runner.wire_spec()
	var path := "res://reports/native/wire.spec.json"
	var file := FileAccess.open(path, FileAccess.WRITE)
	if file == null:
		push_error("Cannot write %s" % path)
	else:
		file.store_string(JSON.stringify(spec, "  ", true))
	if not cells_path.is_empty():
		print("NATIVE_CELLS ", cells_path, " ", result[&"cells"])
	print_summary(result)
	quit(result[&"exit_code"])
	return true


func print_summary(result: Dictionary) -> void:
	var lines: Array[String] = [""]
	lines.append(
		"NATIVE  %d cases  %d failed  %.1f s   %s" % [
			result[&"cases"],
			result[&"failed"],
			result[&"seconds"],
			String(result[&"report_path"]).trim_prefix("res://"),
		],
	)
	var failing: Array = result[&"failing"]
	if not failing.is_empty():
		lines.append(
			"FAILED  first %d of %d" % [failing.size(), result[&"failed"]],
		)
		for row: Dictionary in failing:
			lines.append("  %s" % row[&"name"])
			lines.append("    %s" % row[&"detail"])
	if result[&"census_cases"] > 0:
		var top: PackedStringArray = result[&"census_top"]
		lines.append(
			(
					"CENSUS  %d cases ended holding objects, %d held objects "
					+ "that only the reset between cases released. top %d, all in %s"
			) % [
				result[&"census_cases"],
				result[&"census_reset_only"],
				top.size(),
				String(result[&"census_path"]).trim_prefix("res://"),
			],
		)
		for row in top:
			lines.append("  %s" % row)
	var families: Array = result[&"fault_families"]
	lines.append(
		(
				"FAULTS  %d printed, %d expected by their case, "
				+ "%d unexpected in %d families. top %d unexpected"
		) % [
			result[&"faults"],
			result[&"faults_expected"],
			result[&"faults_unexpected"],
			result[&"fault_family_count"],
			families.size(),
		],
	)
	for family: Dictionary in families:
		var line := String(family[&"line"])
		lines.append("  %4d  %s" % [family[&"count"], line.left(120)])
	lines.append("The engine prints its own exit leak lines after this block.")
	print("\n".join(lines))
