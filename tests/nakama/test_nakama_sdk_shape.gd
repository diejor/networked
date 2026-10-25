class_name TestNakamaSdkShape
extends NetwTestSuite

const FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"
const CLIENT_PATH := "res://addons/com.heroiclabs.nakama/client/NakamaClient.gd"
const SOCKET_PATH := "res://addons/com.heroiclabs.nakama/socket/NakamaSocket.gd"
const STORAGE_ID_PATH := "res://addons/com.heroiclabs.nakama/api/NakamaStorageObjectId.gd"
const WRITE_OBJECT_PATH := "res://addons/com.heroiclabs.nakama/api/NakamaWriteStorageObject.gd"

const REQUIRED_FACADE_METHODS: PackedStringArray = [
	"create_client",
	"create_socket_from",
	"get_device_id",
]

const REQUIRED_CLIENT_METHODS: PackedStringArray = [
	"authenticate_device_async",
	"authenticate_steam_async",
	"link_steam_async",
	"session_refresh_async",
	"get_account_async",
	"get_users_async",
	"read_storage_objects_async",
	"write_storage_objects_async",
	"list_storage_objects_async",
	"delete_storage_objects_async",
	"list_matches_async",
]

const REQUIRED_SOCKET_METHODS: PackedStringArray = [
	"connect_async",
	"create_match_async",
	"join_match_async",
	"join_matched_async",
	"leave_match_async",
	"send_match_state_async",
	"send_match_state_raw_async",
]

const REQUIRED_SOCKET_SIGNALS := {
	"closed": 0,
	"connected": 0,
	"connection_error": 1,
	"received_match_presence": 1,
	"received_match_state": 1,
	"received_matchmaker_matched": 1,
}


func test_facade_client_and_socket_surface() -> void:
	var facade_script := load(FACADE_PATH) as Script
	for method_name in REQUIRED_FACADE_METHODS:
		assert_bool(script_has_method(facade_script, method_name)) \
				.override_failure_message(
					"Nakama.gd is missing '%s'." % method_name,
				).is_true()

	var client_script := load(CLIENT_PATH) as Script
	for method_name in REQUIRED_CLIENT_METHODS:
		assert_bool(script_has_method(client_script, method_name)) \
				.override_failure_message(
					"NakamaClient is missing '%s'." % method_name,
				).is_true()

	var socket_script := load(SOCKET_PATH) as Script
	for method_name in REQUIRED_SOCKET_METHODS:
		assert_bool(script_has_method(socket_script, method_name)) \
				.override_failure_message(
					"NakamaSocket is missing '%s'." % method_name,
				).is_true()
	assert_signal_arity(socket_script, REQUIRED_SOCKET_SIGNALS)


func test_storage_objects_carry_their_fields() -> void:
	var id: Object = (load(STORAGE_ID_PATH) as Script).new(
		"profiles",
		"player",
		"user",
		"version",
	)
	var write: Object = (load(WRITE_OBJECT_PATH) as Script).new(
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


func script_has_method(script: Script, method_name: String) -> bool:
	for method in script.get_script_method_list():
		if method.name == method_name:
			return true
	return false


func assert_signal_arity(script: Script, expected: Dictionary) -> void:
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
