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
	var settings := {
		"name": "Valeria's Game",
		"visibility": NetwServerInfo.VISIBILITY_PUBLIC,
	}
	var peer := await _host(host_dir, settings)
	assert_object(peer) \
			.override_failure_message("PUBLIC host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir._join_address()

	var browse_dir := await _make_dir("jose")
	var listing := await _browse(browse_dir)
	var at := _row_of(listing, "Valeria's Game")
	assert_int(at) \
			.override_failure_message("PUBLIC lobby missing from browse.") \
			.is_greater_equal(0)
	if at >= 0:
		var info: NetwServerInfo = listing.infos[at]
		assert_int(info.players).is_greater_equal(1)
		assert_int(info.visibility) \
				.is_equal(NetwServerInfo.VISIBILITY_PUBLIC)
		assert_str(String(listing.addresses[at])).is_equal(match_id)
		assert_str(String(info.metadata.get("host", ""))) \
				.is_equal(host_dir._local_member_name())

	# Leaving deletes the card, and the now-dead match drops from list_matches.
	var host_tree := host_dir.get_parent() as MultiplayerTree
	_trees.erase(host_tree)
	await NakamaTestSupport.stop_tree(host_tree)

	assert_int(_row_of(await _browse(browse_dir), "Valeria's Game")) \
			.override_failure_message("Lobby still listed after the host left.") \
			.is_equal(-1)


func test_private_lobby_is_unlisted_but_joinable() -> void:
	var host_dir := await _make_dir("valeria")
	var settings := {
		"name": "Secret",
		"visibility": NetwServerInfo.VISIBILITY_PRIVATE,
	}
	var peer := await _host(host_dir, settings)
	assert_object(peer) \
			.override_failure_message("PRIVATE host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir._join_address()

	var browse_dir := await _make_dir("jose")
	assert_int(_row_of(await _browse(browse_dir), "Secret")) \
			.override_failure_message("PRIVATE lobby leaked into browse.") \
			.is_equal(-1)

	# Unlisted, but still reachable when the match id is shared directly.
	var join_peer := await _join(browse_dir, match_id)
	assert_object(join_peer) \
			.override_failure_message("PRIVATE lobby not joinable by match id.") \
			.is_not_null()


# Drives [method LobbyDirectory.host_lobby] and returns the peer it
# delivered, or [code]null[/code] when it reported a failure.
func _host(dir: LobbyDirectory, settings: Dictionary) -> MultiplayerPeer:
	@warning_ignore("missing_await")
	dir._host_lobby(settings)
	return await _reported(dir)


# Drives [method LobbyDirectory.join_lobby] and returns the peer it
# delivered, or [code]null[/code] when it reported a failure.
func _join(dir: LobbyDirectory, address: String) -> MultiplayerPeer:
	@warning_ignore("missing_await")
	dir._join_lobby(address)
	return await _reported(dir)


# Awaits whichever of the two report signals a request answers with.
func _reported(dir: LobbyDirectory) -> MultiplayerPeer:
	var answer: Array = [null]
	var settled := [false]
	dir.lobby_peer_ready.connect(
		func(peer: MultiplayerPeer) -> void:
			answer[0] = peer
			settled[0] = true,
		CONNECT_ONE_SHOT,
	)
	dir.lobby_failed.connect(
		func(_error: int, _message: String) -> void:
			settled[0] = true,
		CONNECT_ONE_SHOT,
	)
	await _await(func() -> bool: return settled[0], "the directory to report")
	return answer[0] as MultiplayerPeer


# Builds a Nakama-wired tree, tags its directory for this run, and returns the
# directory once its service is live.
func _make_dir(username: String) -> NakamaLobbyDirectory:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	_trees.append(tree)
	await NetwTestSuite.drain_frames(get_tree(), 2)
	var dir := NakamaTestSupport.directory(tree)
	dir.browser_filter_uid = _run_uid
	return dir


# Triggers one browse and returns the three arrays it published, keyed
# addresses / names / infos.
func _browse(dir: NakamaLobbyDirectory) -> Dictionary:
	var captured := {
		"addresses": PackedStringArray(),
		"names": PackedStringArray(),
		"infos": [] as Array[NetwServerInfo],
	}
	var done := [false]
	dir.lobby_list_published.connect(
		func(
				addresses: PackedStringArray,
				names: PackedStringArray,
				infos: Array[NetwServerInfo],
		) -> void:
			captured.addresses = addresses
			captured.names = names
			captured.infos = infos
			done[0] = true,
		CONNECT_ONE_SHOT,
	)
	@warning_ignore("missing_await")
	dir._list_lobbies()
	await _await(func() -> bool: return done[0], "browse to resolve")
	return captured


# Returns the published row named [param lobby_name], or [code]-1[/code].
func _row_of(listing: Dictionary, lobby_name: String) -> int:
	var names: PackedStringArray = listing.names
	for at in names.size():
		if names[at] == lobby_name:
			return at
	return -1


func _await(cond: Callable, label: String, timeout: float = _TIMEOUT) -> void:
	var deadline := get_tree().create_timer(timeout)
	while deadline.time_left > 0.0:
		if cond.call():
			return
		await get_tree().process_frame
	assert_bool(cond.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()
