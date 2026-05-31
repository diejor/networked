## Live shape tests for [TubeBackend.TubeWrapper] against the real [TubeClient].
##
## These pin SEAM B: the duck-typed assumptions the wrapper makes about
## TubeClient (methods, properties, signature arity, and the State enum) still
## hold against the real script. Tube ships in-repo, so these always run.
## [br][br]
## Introspection reads the [b]script resource[/b] rather than an instance:
## [code]TubeClient.new()[/code] constructs a [TubeUPNP] that launches a
## [WorkerThreadPool] UPnP discovery task in its [code]_init[/code], which races
## and crashes on free. Shape checks need the API surface, not a live node.
## [br][br]
## The script is referenced via the [code]TubeClient[/code] class name rather
## than a [code]res://[/code] path, so it survives Tube's files being moved or
## renamed. See [method _client_script].
extends GdUnitTestSuite


## The TubeClient [GDScript], resolved via the class name at runtime. A class
## name is not a constant expression (so it cannot init a const), but as a plain
## value it gives the script without instantiating it or hardcoding a path.
func _client_script() -> Script:
	return TubeClient


func _method_names() -> PackedStringArray:
	var names := PackedStringArray()
	for info in _client_script().get_script_method_list():
		names.append(info.name)
	return names


func _property_names() -> PackedStringArray:
	var names := PackedStringArray()
	for info in _client_script().get_script_property_list():
		names.append(info.name)
	return names


func test_client_exposes_wrapper_methods() -> void:
	var methods := _method_names()
	for method_name in ["create_session", "join_session", "leave_session"]:
		assert_bool(methods.has(method_name)) \
			.override_failure_message(
				"TubeClient is missing method '%s' that TubeWrapper calls."
				% method_name
			).is_true()


func test_client_exposes_wrapper_properties() -> void:
	var properties := _property_names()
	for property_name in [
		"state", "session_id", "multiplayer_api", "multiplayer_root_node"
	]:
		assert_bool(properties.has(property_name)) \
			.override_failure_message(
				"TubeClient is missing property '%s' that TubeWrapper reads/writes."
				% property_name
			).is_true()


func test_join_session_takes_one_argument() -> void:
	var arg_count := -1
	for info in _client_script().get_script_method_list():
		if info.name == "join_session":
			arg_count = (info.args as Array).size()
			break
	assert_int(arg_count) \
		.override_failure_message(
			"TubeClient.join_session arity changed; TubeWrapper calls it with a "
			+ "single address argument."
		).is_equal(1)


func test_state_enum_matches_wrapper() -> void:
	# TubeWrapper.state returns TubeClient.state verbatim, and TubeBackend
	# compares it against TubeWrapper.State values, so the enums must stay
	# numerically aligned. Referencing the enums loads the classes but does not
	# instantiate them, so no UPnP thread is started.
	assert_int(TubeBackend.TubeWrapper.State.IDLE) \
		.is_equal(TubeClient.State.IDLE)
	assert_int(TubeBackend.TubeWrapper.State.CREATING_SESSION) \
		.is_equal(TubeClient.State.CREATING_SESSION)
	assert_int(TubeBackend.TubeWrapper.State.SESSION_CREATED) \
		.is_equal(TubeClient.State.SESSION_CREATED)
	assert_int(TubeBackend.TubeWrapper.State.JOINING_SESSION) \
		.is_equal(TubeClient.State.JOINING_SESSION)
	assert_int(TubeBackend.TubeWrapper.State.SESSION_JOINED) \
		.is_equal(TubeClient.State.SESSION_JOINED)
