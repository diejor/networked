extends SceneTree

const DEFAULT_DURATION_MSEC := 15_000

var _probe: Object
var _bomber: Node
var _duration_msec := DEFAULT_DURATION_MSEC
var _started_msec := 0


func _initialize() -> void:
	# The probe is one extra zone source, and an engine module build has no
	# reason to carry it. Capture without it rather than refusing to run.
	if not ClassDB.class_exists(&"NetwNativeTests"):
		print(
			"No instrumentation probe; capturing without it. "
			+ "For one, build with: scons -C extension netw_tests=yes",
		)
	for argument in OS.get_cmdline_user_args():
		if argument.begins_with("--duration-msec="):
			_duration_msec = int(argument.trim_prefix("--duration-msec="))
	var scene := load("res://examples/bomber/main.tscn") as PackedScene
	_bomber = scene.instantiate()
	if ClassDB.class_exists(&"NetwNativeTests"):
		_probe = ClassDB.instantiate(&"NetwNativeTests")
	_started_msec = Time.get_ticks_msec()


func _process(_delta: float) -> bool:
	var elapsed := Time.get_ticks_msec() - _started_msec
	if _probe != null:
		_probe.instrumentation_probe(elapsed)
	NetwCodec.encode_snapshot(
		elapsed,
		elapsed - 1,
		{ &"position": Vector3(1.0, 2.0, 3.0) },
		[&"position"],
		[null],
	)
	if elapsed >= _duration_msec:
		_bomber.free()
		quit()
	return false
