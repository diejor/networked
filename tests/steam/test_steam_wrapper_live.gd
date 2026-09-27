extends GdUnitTestSuite

const REQUIRED_METHODS: PackedStringArray = [
	"steamInitEx",
	"run_callbacks",
	"getSteamID",
	"getPersonaName",
	"getFriendPersonaName",
	"createLobby",
	"joinLobby",
	"leaveLobby",
	"getLobbyOwner",
	"getNumLobbyMembers",
	"getLobbyMemberLimit",
	"getLobbyMemberByIndex",
	"setLobbyData",
	"getLobbyData",
	"setLobbyJoinable",
	"allowP2PPacketRelay",
	"requestLobbyList",
	"requestLobbyData",
	"addRequestLobbyListStringFilter",
	"addRequestLobbyListDistanceFilter",
]

const REQUIRED_SIGNALS := {
	"lobby_created": 2,
	"lobby_joined": 4,
	"lobby_match_list": 1,
	"join_requested": 2,
	"lobby_data_update": 3,
}

var _steam: Object


func before_test() -> void:
	if Engine.has_singleton("Steam"):
		_steam = Engine.get_singleton("Steam")


func _require_steam() -> bool:
	return _steam != null


func test_singleton_exposes_required_methods() -> void:
	if not _require_steam():
		return
	for method_name in REQUIRED_METHODS:
		assert_bool(_steam.has_method(method_name)) \
				.override_failure_message(
					"GodotSteam is missing method '%s' that SteamWrapper calls."
					% method_name,
				).is_true()


func test_singleton_exposes_required_signals() -> void:
	if not _require_steam():
		return
	var arity_by_name := { }
	for info in _steam.get_signal_list():
		arity_by_name[info.name] = (info.args as Array).size()

	for signal_name in REQUIRED_SIGNALS:
		assert_bool(arity_by_name.has(signal_name)) \
				.override_failure_message(
					"GodotSteam is missing signal '%s' that SteamWrapper bridges."
					% signal_name,
				).is_true()
		if not arity_by_name.has(signal_name):
			continue
		assert_int(arity_by_name[signal_name]) \
				.override_failure_message(
					"GodotSteam signal '%s' arg count changed; SteamWrapper._init "
					% signal_name
					+ "connects a handler expecting %d args."
					% REQUIRED_SIGNALS[signal_name],
				).is_equal(REQUIRED_SIGNALS[signal_name])


func test_lobby_type_enum_matches_godotsteam() -> void:
	if not _require_steam():
		return
	var cls := _steam.get_class()
	var names := ClassDB.class_get_integer_constant_list(cls, false)
	var expected := {
		"LOBBY_TYPE_PRIVATE": SteamWrapper.LobbyType.PRIVATE,
		"LOBBY_TYPE_FRIENDS_ONLY": SteamWrapper.LobbyType.FRIENDS_ONLY,
		"LOBBY_TYPE_PUBLIC": SteamWrapper.LobbyType.PUBLIC,
		"LOBBY_TYPE_INVISIBLE": SteamWrapper.LobbyType.INVISIBLE,
	}
	for const_name in expected:
		assert_bool(names.has(const_name)) \
				.override_failure_message(
					"GodotSteam (%s) has no integer constant '%s'. " % [cls, const_name]
					+ "SteamWrapper.LobbyType assumes it; reconcile the names with "
					+ "the installed GodotSteam version.",
				).is_true()
		if not names.has(const_name):
			continue
		assert_int(ClassDB.class_get_integer_constant(cls, const_name)) \
				.override_failure_message(
					"SteamWrapper.LobbyType drifted from GodotSteam '%s'." % const_name,
				).is_equal(expected[const_name])
