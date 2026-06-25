## Live browse and storage behavior tests for [NakamaLobbyDirectory] and
## [NakamaWrapper], driven against a Docker Nakama server.
##
## The shape tests only pin that the storage and match-list calls exist. These
## exercise the round-trip: hosting publishes a card, browsing reads it back
## through [method NakamaWrapper.read_lobby_cards] merged with
## [method NakamaWrapper.list_matches], leaving deletes the card, and a PRIVATE
## host stays unlisted yet joinable by match id. A unique
## [member NakamaLobbyDirectory.browser_filter_uid] isolates each run on the
## shared storage collection.
class_name TestNakamaLobbyBrowse
extends NetwTestSuite

const _TIMEOUT := 12.0

var _trees: Array = []
var _run_uid := ""


func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	_run_uid = "browse-%d-%d" % [Time.get_ticks_usec(), randi()]


func after_test() -> void:
	for tree in _trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	_trees.clear()
	await super.after_test()


func test_public_lobby_round_trips_then_clears_on_leave() -> void:
	var host_dir := await _make_dir("valeria")
	var peer := await host_dir.host_lobby(
		LobbyDirectory.HostOptions.make(
			"Valeria's Game",
			LobbyDirectory.Visibility.PUBLIC,
		),
	)
	assert_object(peer) \
			.override_failure_message("PUBLIC host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir.get_join_address()

	var browse_dir := await _make_dir("jose")
	var lobbies := await _browse(browse_dir)
	var found := _find_lobby(lobbies, "Valeria's Game")
	assert_object(found) \
			.override_failure_message("PUBLIC lobby missing from browse.") \
			.is_not_null()
	if found != null:
		assert_int(found.players).is_greater_equal(1)
		assert_int(found.visibility).is_equal(LobbyDirectory.Visibility.PUBLIC)
		assert_str(String(found.metadata.get("match_id", ""))).is_equal(match_id)
		assert_str(found.host_name).is_equal(host_dir.get_local_member_name())

	# Leaving deletes the card, and the now-dead match drops from list_matches.
	var host_tree := host_dir.get_parent() as MultiplayerTree
	_trees.erase(host_tree)
	await NakamaTestSupport.stop_tree(host_tree)

	var after := await _browse(browse_dir)
	assert_object(_find_lobby(after, "Valeria's Game")) \
			.override_failure_message("Lobby still listed after the host left.") \
			.is_null()


func test_private_lobby_is_unlisted_but_joinable() -> void:
	var host_dir := await _make_dir("valeria")
	var peer := await host_dir.host_lobby(
		LobbyDirectory.HostOptions.make(
			"Secret",
			LobbyDirectory.Visibility.PRIVATE,
		),
	)
	assert_object(peer) \
			.override_failure_message("PRIVATE host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir.get_join_address()

	var browse_dir := await _make_dir("jose")
	var lobbies := await _browse(browse_dir)
	assert_object(_find_lobby(lobbies, "Secret")) \
			.override_failure_message("PRIVATE lobby leaked into browse.") \
			.is_null()

	# Unlisted, but still reachable when the match id is shared directly.
	var join_peer := await browse_dir.join_match_peer(match_id)
	assert_object(join_peer) \
			.override_failure_message("PRIVATE lobby not joinable by match id.") \
			.is_not_null()


# Builds a Nakama-wired tree, tags its directory for this run, and returns the
# directory once its service is live.
func _make_dir(username: String) -> NakamaLobbyDirectory:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	_trees.append(tree)
	await NetwTestSuite.drain_frames(get_tree(), 2)
	var dir := NakamaTestSupport.directory(tree)
	dir.browser_filter_uid = _run_uid
	return dir


# Triggers one browse and returns the emitted lobby list.
func _browse(dir: NakamaLobbyDirectory) -> Array:
	var captured: Array = []
	var done := [false]
	dir.lobby_list_updated.connect(
		func(lobbies: Array) -> void:
			captured.assign(lobbies)
			done[0] = true,
		CONNECT_ONE_SHOT,
	)
	dir.list_lobbies()
	await _await(func() -> bool: return done[0], "browse to resolve")
	return captured


func _find_lobby(lobbies: Array, lobby_name: String) -> LobbyDirectory.LobbyInfo:
	for info in lobbies:
		if info.lobby_name == lobby_name:
			return info
	return null


func _await(cond: Callable, label: String, timeout: float = _TIMEOUT) -> void:
	var deadline := get_tree().create_timer(timeout)
	while deadline.time_left > 0.0:
		if cond.call():
			return
		await get_tree().process_frame
	assert_bool(cond.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()
