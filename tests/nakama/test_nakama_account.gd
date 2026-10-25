class_name TestNakamaAccount
extends NetwTestSuite

const GUEST_PREFIX := "nakama_guest_"

const GUEST_PROCESS := "res://tests/nakama/nakama_guest_process.gd"

const TIMEOUT := 10.0

var trees: Array = []
var saved_guests: Dictionary[String, String] = { }


@warning_ignore("unused_parameter")
func before(
		do_skip = NakamaTestServer.unavailable(),
		skip_reason = NakamaTestServer.SKIP_REASON,
) -> void:
	pass


func before_test() -> void:
	saved_guests.clear()
	for file in DirAccess.get_files_at("user://"):
		if file.begins_with(GUEST_PREFIX):
			saved_guests[file] = FileAccess.get_file_as_string("user://" + file)
			DirAccess.remove_absolute("user://" + file)


func after_test() -> void:
	for tree in trees:
		await NakamaTestSupport.stop_tree(tree)
	trees.clear()
	for file in DirAccess.get_files_at("user://"):
		if file.begins_with(GUEST_PREFIX):
			DirAccess.remove_absolute("user://" + file)
	for file: String in saved_guests:
		FileAccess.open("user://" + file, FileAccess.WRITE) \
				.store_string(saved_guests[file])
	await super.after_test()


func guest_account() -> NakamaAccount:
	var account: NakamaAccount = auto_free(NakamaAccount.new())
	account.host = NakamaTestServer.host()
	account.port = NakamaTestServer.DEFAULT_PORT
	add_child(account)
	return account


func guest_of_another_process() -> String:
	var output: Array = []
	OS.execute(
		OS.get_executable_path(),
		[
			"--headless",
			"--path",
			ProjectSettings.globalize_path("res://"),
			"-s",
			GUEST_PROCESS,
		],
		output,
		true,
	)
	for line in "\n".join(output).split("\n"):
		if line.begins_with("NAKAMA_GUEST="):
			return line.trim_prefix("NAKAMA_GUEST=").strip_edges()
	return ""


func test_a_guest_is_a_random_id_kept_on_this_device() -> void:
	var first := guest_account()
	await first.sign_in().wait()
	assert_str(first.user_id).is_not_empty()

	var second := guest_account()
	await second.sign_in().wait()
	assert_str(second.user_id) \
			.override_failure_message(
				"A second sign in in this instance made a new guest.",
			).is_equal(first.user_id)


func test_each_running_instance_is_its_own_guest() -> void:
	var here := guest_account()
	await here.sign_in().wait()
	assert_str(here.user_id).is_not_empty()

	var there := guest_of_another_process()
	assert_str(there) \
			.override_failure_message("The other instance did not sign in.") \
			.is_not_empty()
	assert_str(there) \
			.override_failure_message(
				"Two instances running at once signed in as one guest.",
			).is_not_equal(here.user_id)
	assert_str(guest_of_another_process()) \
			.override_failure_message(
				"An instance that restarted signed in as a new guest.",
			).is_equal(there)


func test_a_guest_signs_in_with_no_username() -> void:
	var account := guest_account()
	await account.sign_in().wait()
	assert_object(account.session) \
			.override_failure_message("The guest could not sign in.") \
			.is_not_null()
	if account.session == null:
		return
	assert_str(account.session.username) \
			.override_failure_message(
				"The guest was created with the player's name as its username.",
			).is_not_equal(LobbyDirectory.local_member_name_default())
	assert_str(account.display_name).is_equal(account.session.username)


func test_the_relay_answers_accounts_and_display_names() -> void:
	var host := await NakamaTestSupport.start_host(self, "valeria")
	trees.append(host.tree)
	var client := NakamaTestSupport.make_client_tree(self, "jose")
	trees.append(client)
	assert_int(await NakamaTestSupport.sign_in(client, "jose")).is_equal(OK)
	var jose := NakamaTestSupport.account(client)
	var shown := "Jose %d" % randi()
	await jose.client.update_account_async(jose.session, null, shown)
	assert_int(await NakamaTestSupport.join_client(client, host.room, "jose")) \
			.is_equal(OK)

	var serving := "nakama:" + NakamaTestSupport.account(host.tree).user_id
	for tree: MultiplayerViewport in [host.tree, client]:
		assert_str(NakamaTestSupport.directory(tree)._account_of(1)) \
				.override_failure_message(
					"%s does not answer the server's account." % tree.name,
				).is_equal(serving)

	var session: NetwSession = Netw.session(host.tree)
	var deadline := get_tree().create_timer(TIMEOUT)
	while session.find_player("nakama:" + jose.user_id) == null \
			and deadline.time_left > 0.0:
		await get_tree().process_frame
	var player := session.find_player("nakama:" + jose.user_id)
	assert_object(player) \
			.override_failure_message("The server never admitted Jose's account.") \
			.is_not_null()
	if player != null:
		assert_str(player.account_name).is_equal(shown)
