extends GdUnitTestSuite

const APPLICATION_ID := 1561065725962092666
const SIGN_IN_APPLICATION_ID := 1000000000000000001
const TOKEN_APPLICATION_ID := 1000000000000000002
const WAIT_MSEC := 7000

var viewports: Array[MultiplayerViewport]
var environment: Dictionary[String, String]
var peers: Array[StreamPeerTCP]
var opened_path: String


func before_test() -> void:
	var root := ProjectSettings.globalize_path("res://tmp/discord-tests/")
	var runtime := root.path_join("xdg_runtime_dir")
	var bin := root.path_join("bin")
	DirAccess.make_dir_recursive_absolute(runtime)
	DirAccess.make_dir_recursive_absolute(bin)
	environment["XDG_RUNTIME_DIR"] = OS.get_environment("XDG_RUNTIME_DIR")
	OS.set_environment("XDG_RUNTIME_DIR", runtime)

	opened_path = root.path_join("opened.txt")
	DirAccess.remove_absolute(opened_path)
	var opener := FileAccess.open(bin.path_join("xdg-open"), FileAccess.WRITE)
	opener.store_string('#!/bin/sh\necho "$1" >> "%s"\n' % opened_path)
	opener.close()
	FileAccess.set_unix_permissions(bin.path_join("xdg-open"), 493)
	environment["PATH"] = OS.get_environment("PATH")
	OS.set_environment("PATH", bin + ":" + environment["PATH"])


func after_test() -> void:
	for viewport: MultiplayerViewport in viewports:
		viewport.api.multiplayer_peer = null
		viewport.free()
	viewports.clear()
	peers.clear()
	await get_tree().process_frame
	for key: String in environment:
		if environment[key].is_empty():
			OS.unset_environment(key)
		else:
			OS.set_environment(key, environment[key])
	environment.clear()


func viewport(
		role: NetwSession.Role = NetwSession.ROLE_NONE,
) -> MultiplayerViewport:
	var created := MultiplayerViewport.new()
	if role != NetwSession.ROLE_NONE:
		Netw.configure_session(created).role(role)
	viewports.append(created)
	add_child(created)
	return created


func discord(parent: Node, application_id := APPLICATION_ID) -> DiscordSocial:
	var social := DiscordSocial.new()
	social.application_id = application_id
	parent.add_child(social)
	return social


func pump_until(condition: Callable) -> bool:
	var deadline := Time.get_ticks_msec() + WAIT_MSEC
	while not condition.call() and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	return condition.call()


func test_a_join_click_joins_only_a_party_secret() -> void:
	var party := discord(viewport()).party
	for row: Array in [
		["p1|" + "ab".repeat(32), "ab".repeat(32)],
		["n1|ENetMultiplayerPeer|203.0.113.4:21253", ""],
		["p1|", ""],
	]:
		var before := party.join_secret
		var expected: String = row[1] if not row[1].is_empty() else before
		if row[1].is_empty():
			await assert_error(
				func() -> void: DiscordParty._on_activity_join(row[0]),
			).is_push_warning(any_string())
		else:
			DiscordParty._on_activity_join(row[0])
		assert_str(party.join_secret).override_failure_message(row[0]) \
				.is_equal(expected)


func serve(server: TCPServer, status: int, body: String) -> String:
	var deadline := Time.get_ticks_msec() + 5000
	while not server.is_connection_available() \
			and Time.get_ticks_msec() < deadline:
		await get_tree().process_frame
	if not server.is_connection_available():
		return ""

	var peer := server.take_connection()
	var received := ""
	while Time.get_ticks_msec() < deadline:
		peer.poll()
		received += peer.get_utf8_string(peer.get_available_bytes())
		var head := received.find("\r\n\r\n")
		var length := received.get_slice("Content-Length: ", 1) \
				.get_slice("\r\n", 0).to_int()
		if head >= 0 and received.to_utf8_buffer().size() >= head + 4 + length:
			break
		await get_tree().process_frame

	var reply := "HTTP/1.1 %d Token\r\nContent-Length: %d\r\n" \
			% [status, body.to_utf8_buffer().size()] \
			+ "Content-Type: application/json\r\nConnection: close\r\n\r\n"
	peer.put_data((reply + body).to_utf8_buffer())
	peers.append(peer)
	return received


func test_a_refresh_goes_to_the_token_server_and_checks_its_scopes() -> void:
	var server := TCPServer.new()
	assert_int(server.listen(0, "127.0.0.1")).is_equal(OK)
	var url := "http://127.0.0.1:%d/discord/token" % server.get_local_port()
	var social := discord(viewport(), TOKEN_APPLICATION_ID)
	social.token_url = url
	social.token_headers = ["X-Game-Session: s1"]
	var path := "user://discord_%d.cfg" % TOKEN_APPLICATION_ID
	var presence := DiscordClient.get_default_presence_scopes()
	var scopes := DiscordClient.get_default_communication_scopes()
	var kept := func() -> String:
		var credentials := ConfigFile.new()
		credentials.load(path)
		return credentials.get_value("discord", "refresh_token", "")
	var page := func() -> String:
		return FileAccess.get_file_as_string(opened_path)

	for row: Array in [
		[200, { "access_token": "a", "refresh_token": "b", "scope": scopes }, ""],
		[401, { "error": "invalid_client", "error_description": "private" }, ""],
		[
			200,
			{ "access_token": "a", "refresh_token": "b", "scope": presence },
			scopes,
		],
	]:
		var saved := ConfigFile.new()
		saved.set_value("discord", "refresh_token", "saved")
		saved.save(path)
		var signing_in: NetwPromise = social.sign_in()
		var received := await serve(server, row[0], JSON.stringify(row[1]))
		assert_str(received).contains("X-Game-Session: s1")
		assert_str(received).ends_with(
			"grant_type=refresh_token&refresh_token=saved",
		)
		var expected: String = row[1].get("refresh_token", "")
		var settled := func() -> bool:
			if row[0] != 200:
				return signing_in.is_failed
			if not row[2].is_empty():
				return page.call().contains(
					"scope=" + row[2].uri_encode() + "&",
				)
			return kept.call() == expected
		assert_bool(await pump_until(settled)) \
				.override_failure_message(str(row)).is_true()
		if row[0] != 200:
			assert_str(signing_in.detail).contains("%s answered HTTP 401" % url)
			assert_str(signing_in.detail).not_contains("private")
		social.sign_out()
	server.stop()

	social.token_headers = ["Authorization: Bearer s1"]
	for refused: String in [
		"https://discord.com/api/oauth2/token",
		"http://example.com/token",
	]:
		await assert_error(
			func() -> void: social.token_url = refused,
		).is_push_error(any_string())
		assert_str(social.token_url).override_failure_message(refused) \
				.is_equal(url)


func test_only_the_first_service_holds_the_client_until_it_leaves() -> void:
	var first_viewport := viewport()
	var first := discord(first_viewport, SIGN_IN_APPLICATION_ID)
	var second := discord(viewport(), SIGN_IN_APPLICATION_ID)
	assert_object(first.client).is_not_null()
	assert_object(second.client).is_null()
	var refused: NetwPromise = second.sign_in()
	assert_bool(refused.is_failed).is_true()
	assert_int(refused.code).is_equal(ERR_UNAVAILABLE)

	first_viewport.remove_child(first)
	var third_viewport := viewport()
	var third := discord(third_viewport, SIGN_IN_APPLICATION_ID)
	assert_object(third.client).is_not_null()
	third_viewport.remove_child(third)
	third.free()
	first_viewport.add_child(first)
	assert_object(first.client).is_not_null()
	assert_object(second.client).is_null()


func test_sign_out_cancels_sign_in_and_a_new_sign_in_waits_again() -> void:
	var social := discord(viewport(), SIGN_IN_APPLICATION_ID)
	var first: NetwPromise = social.sign_in()
	assert_object(social.sign_in()).is_same(first)
	social.sign_out()
	assert_bool(first.is_failed).is_true()
	assert_int(first.code).is_equal(ERR_SKIP)
	var second: NetwPromise = social.sign_in()
	assert_object(second).is_not_same(first)
	assert_bool(second.is_settled).is_false()


func test_a_headless_run_leaves_the_saved_sign_in_alone() -> void:
	var path := "user://discord_%d.cfg" % SIGN_IN_APPLICATION_ID
	var saved := ConfigFile.new()
	saved.set_value("discord", "refresh_token", "saved")
	saved.save(path)
	var social := discord(viewport(), SIGN_IN_APPLICATION_ID)
	await get_tree().create_timer(1.0).timeout
	var kept := ConfigFile.new()
	kept.load(path)
	DirAccess.remove_absolute(path)
	assert_str(kept.get_value("discord", "refresh_token", "")).is_equal("saved")
	assert_bool(FileAccess.file_exists(opened_path)).is_false()
	assert_int(social.user_id).is_zero()
