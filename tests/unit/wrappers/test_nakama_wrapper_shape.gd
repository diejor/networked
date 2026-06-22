## Shape tests for [NakamaWrapper] against the installed Nakama addon.
##
## These pin the optional addon API that [NakamaWrapper],
## [NakamaBackend], and [NakamaLobbyDirectory] call. The suite is skipped when
## the addon is absent, so the project still parses on builds without Nakama.
class_name TestNakamaWrapperShape
extends NetwTestSuite

const _FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"
const _BRIDGE_PATH := \
		"res://addons/com.heroiclabs.nakama/utils/NakamaMultiplayerBridge.gd"
const _CLIENT_PATH := \
		"res://addons/com.heroiclabs.nakama/client/NakamaClient.gd"
const _SOCKET_PATH := \
		"res://addons/com.heroiclabs.nakama/socket/NakamaSocket.gd"
const _STORAGE_ID_PATH := \
		"res://addons/com.heroiclabs.nakama/api/NakamaStorageObjectId.gd"
const _WRITE_OBJECT_PATH := \
		"res://addons/com.heroiclabs.nakama/api/NakamaWriteStorageObject.gd"

const REQUIRED_CLIENT_METHODS: PackedStringArray = [
	"authenticate_device_async",
	"read_storage_objects_async",
	"write_storage_objects_async",
	"rpc_async",
]

const REQUIRED_SOCKET_METHODS: PackedStringArray = [
	"connect_async",
	"create_match_async",
	"join_match_async",
	"leave_match_async",
	"send_match_state_async",
	"send_match_state_raw_async",
]

const REQUIRED_BRIDGE_METHODS: PackedStringArray = [
	"create_match",
	"join_match",
	"leave",
	"get_user_presence_for_peer",
]

const REQUIRED_BRIDGE_SIGNALS := {
	"match_joined": 0,
	"match_join_error": 1,
}

const REQUIRED_SOCKET_SIGNALS := {
	"closed": 0,
}


func before(
		do_skip = not NakamaWrapper.is_addon_present(),
		skip_reason = "Nakama addon is not installed.",
) -> void:
	pass


func test_facade_exposes_client_and_socket_factories() -> void:
	var facade: Object = (load(_FACADE_PATH) as Script).new()
	auto_free(facade)

	assert_bool(facade.has_method("create_client")).is_true()
	assert_bool(facade.has_method("create_socket_from")).is_true()


func test_client_exposes_required_async_methods() -> void:
	var script := load(_CLIENT_PATH) as Script
	for method_name in REQUIRED_CLIENT_METHODS:
		assert_bool(_script_has_method(script, method_name)) \
				.override_failure_message(
					"NakamaClient is missing '%s'." % method_name,
				).is_true()


func test_socket_exposes_required_methods_and_signals() -> void:
	var script := load(_SOCKET_PATH) as Script
	for method_name in REQUIRED_SOCKET_METHODS:
		assert_bool(_script_has_method(script, method_name)) \
				.override_failure_message(
					"NakamaSocket is missing '%s'." % method_name,
				).is_true()
	_assert_signal_arity(script, REQUIRED_SOCKET_SIGNALS)


func test_bridge_exposes_required_methods_and_signals() -> void:
	var script := load(_BRIDGE_PATH) as Script
	for method_name in REQUIRED_BRIDGE_METHODS:
		assert_bool(_script_has_method(script, method_name)) \
				.override_failure_message(
					"NakamaMultiplayerBridge is missing '%s'." % method_name,
				).is_true()
	_assert_signal_arity(script, REQUIRED_BRIDGE_SIGNALS)


func test_storage_helper_shapes_match_wrapper_usage() -> void:
	var id: Object = (load(_STORAGE_ID_PATH) as Script).new(
		"profiles",
		"player",
		"user",
		"version",
	)
	var write: Object = (load(_WRITE_OBJECT_PATH) as Script).new(
		"profiles",
		"player",
		1,
		1,
		"{\"ok\":true}",
		"",
	)

	assert_str(id.collection).is_equal("profiles")
	assert_str(id.key).is_equal("player")
	assert_str(id.user_id).is_equal("user")
	assert_str(id.version).is_equal("version")
	assert_bool(id.has_method("as_read")).is_true()
	assert_bool(id.has_method("as_delete")).is_true()

	assert_str(write.collection).is_equal("profiles")
	assert_str(write.key).is_equal("player")
	assert_int(write.permission_read).is_equal(1)
	assert_int(write.permission_write).is_equal(1)
	assert_str(write.value).is_equal("{\"ok\":true}")
	assert_bool(write.has_method("as_write")).is_true()


func _script_has_method(script: Script, method_name: String) -> bool:
	for method in script.get_script_method_list():
		if method.name == method_name:
			return true
	return false


func _assert_signal_arity(script: Script, expected: Dictionary) -> void:
	var arity_by_name := { }
	for info in script.get_script_signal_list():
		arity_by_name[info.name] = (info.args as Array).size()

	for signal_name in expected:
		assert_bool(arity_by_name.has(signal_name)) \
				.override_failure_message(
					"%s is missing signal '%s'." % [
						script.resource_path,
						signal_name,
					],
				).is_true()
		if not arity_by_name.has(signal_name):
			continue
		assert_int(arity_by_name[signal_name]) \
				.override_failure_message(
					"%s signal '%s' arity changed." % [
						script.resource_path,
						signal_name,
					],
				).is_equal(expected[signal_name])
