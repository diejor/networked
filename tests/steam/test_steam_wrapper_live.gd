## Live shape tests for [SteamWrapper] against the real GodotSteam singleton.
##
## These pin SEAM B: the assumptions [SteamWrapper] hardcodes about the GodotSteam
## API (method names, signals + their arity, enum values) still hold against the
## installed extension. They only need the extension *loaded*, not Steam
## *running*, so they are safe in headless CI.
## [br][br]
## Skipped (early return) when the GodotSteam GDExtension is not installed, e.g.
## on a platform where the binary was not built. This file never references a
## [code]Steam.*[/code] symbol directly so it parses even when the extension is
## absent; everything goes through the singleton [Variant] and [ClassDB].
extends GdUnitTestSuite

## camelCase singleton methods [SteamWrapper] forwards to. Keep in sync with
## steam_wrapper.gd. ([code]run_callbacks[/code] is snake_case in GodotSteam.)
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

## Signals [SteamWrapper] bridges, mapped to the handler arity it connects with.
## A drift in arg count breaks the lambda connection at runtime, so pin both.
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


## Returns true when the GodotSteam singleton is present. When false, the calling
## test returns early as a no-op (the extension is not installed on this build).
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
	# GodotSteam exposes LobbyType as integer constants on the Steam class.
	# Constant names below follow GodotSteam's bindings; if a future version
	# renames them this fails with guidance rather than silently passing.
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
