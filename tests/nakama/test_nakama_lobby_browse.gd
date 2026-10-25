class_name TestNakamaLobbyBrowse
extends NetwTestSuite

const TIMEOUT := 12.0

var trees: Array = []
var run_uid := ""
var sdk: Script


class ReadyBrowser:
	extends Node

	func _ready() -> void:
		Netw.session(self).connection.browsing = true


func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	sdk = load("res://addons/com.heroiclabs.nakama/Nakama.gd")


func before_test() -> void:
	run_uid = "browse-%d-%d" % [Time.get_ticks_usec(), randi()]


func after_test() -> void:
	for tree in trees.duplicate():
		if is_instance_valid(tree):
			await NakamaTestSupport.stop_tree(tree)
	trees.clear()
	await super.after_test()


func test_public_lobby_round_trips_then_clears_on_leave() -> void:
	var host_dir := await make_directory("valeria")
	var settings := {
		"name": "Valeria's Game",
		"visibility": NetwServerInfo.VISIBILITY_PUBLIC,
	}
	var peer := await host_lobby(host_dir, settings)
	assert_object(peer) \
			.override_failure_message("PUBLIC host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir._join_address()

	var browse_dir := await make_directory("jose")
	var listing := await browse_lobbies(browse_dir)
	var at := find_lobby(listing, "Valeria's Game")
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

	var host_tree := host_dir.get_parent() as MultiplayerViewport
	trees.erase(host_tree)
	await NakamaTestSupport.stop_tree(host_tree)

	assert_int(find_lobby(await browse_lobbies(browse_dir), "Valeria's Game")) \
			.override_failure_message("Lobby still listed after the host left.") \
			.is_equal(-1)


func test_private_lobby_is_unlisted_but_joinable() -> void:
	var host_dir := await make_directory("valeria")
	var settings := {
		"name": "Secret",
		"visibility": NetwServerInfo.VISIBILITY_PRIVATE,
	}
	var peer := await host_lobby(host_dir, settings)
	assert_object(peer) \
			.override_failure_message("PRIVATE host_lobby returned no peer.") \
			.is_not_null()
	var match_id := host_dir._join_address()

	var browse_dir := await make_directory("jose")
	assert_int(find_lobby(await browse_lobbies(browse_dir), "Secret")) \
			.override_failure_message("PRIVATE lobby leaked into browse.") \
			.is_equal(-1)

	@warning_ignore("missing_await")
	browse_dir._join_lobby(match_id)
	var join_peer := await await_peer(browse_dir)
	assert_object(join_peer) \
			.override_failure_message("PRIVATE lobby not joinable by match id.") \
			.is_not_null()


func test_browsing_from_a_ready_callback_signs_in_a_guest_and_lists() -> void:
	var tree := MultiplayerViewport.new()
	var account := NakamaAccount.new()
	account.host = NakamaTestServer.host()
	account.port = NakamaTestServer.DEFAULT_PORT
	tree.add_child(account)
	var dir := NakamaLobbyDirectory.new()
	dir.browser_filter_uid = run_uid
	tree.add_child(dir)
	tree.add_child(ReadyBrowser.new())
	var listed := [false]
	dir.lobby_list_published.connect(
		func(_addresses, _names, _infos) -> void: listed[0] = true,
		CONNECT_ONE_SHOT,
	)
	trees.append(tree)
	add_child(tree)

	await wait_for(func() -> bool: return listed[0], "the first listing")
	assert_str(account.user_id) \
			.override_failure_message("Listing lobbies signed nobody in.") \
			.is_not_empty()


func host_lobby(dir: LobbyDirectory, settings: Dictionary) -> MultiplayerPeer:
	@warning_ignore("missing_await")
	dir._host_lobby(settings)
	return await await_peer(dir)


func await_peer(dir: LobbyDirectory) -> MultiplayerPeer:
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
	await wait_for(func() -> bool: return settled[0], "the directory to report")
	return answer[0] as MultiplayerPeer


func make_directory(username: String) -> NakamaLobbyDirectory:
	var tree := NakamaTestSupport.make_client_tree(self, username)
	trees.append(tree)
	await NetwTestSuite.drain_frames(get_tree(), 2)
	await NakamaTestSupport.sign_in(tree, "%s-%s" % [run_uid, username])
	var dir := NakamaTestSupport.directory(tree)
	dir.browser_filter_uid = run_uid
	return dir


func browse_lobbies(dir: NakamaLobbyDirectory) -> Dictionary:
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
	await wait_for(func() -> bool: return done[0], "browse to resolve")
	return captured


func find_lobby(listing: Dictionary, lobby_name: String) -> int:
	var names: PackedStringArray = listing.names
	for at in names.size():
		if names[at] == lobby_name:
			return at
	return -1


func wait_for(cond: Callable, label: String, timeout: float = TIMEOUT) -> void:
	var deadline := get_tree().create_timer(timeout)
	while deadline.time_left > 0.0:
		if cond.call():
			return
		await get_tree().process_frame
	assert_bool(cond.call()) \
			.override_failure_message("Timed out waiting for %s." % label) \
			.is_true()
