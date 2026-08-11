## Coverage for [NetwServerBrowser]: saved-target probing and classification,
## the concurrency cap, directory auto-bind through the service registry, and
## pure-data [NetwServerList] persistence.
class_name TestConnectKitBrowser
extends NetwTestSuite

# A transport whose probe returns a canned result, recording peak concurrency so
# the cap is observable.
class _FakeTransport:
	extends NetwTransport

	var canned: NetwProbeResult
	var probe_delay_frames: int = 0
	var active: int = 0
	var peak: int = 0


	func _can_join(target: NetwConnectTarget) -> bool:
		return target != null and target.scheme == &"fake"


	func _probe(_target: NetwConnectTarget) -> NetwProbeResult:
		active += 1
		peak = maxi(peak, active)
		var loop := Engine.get_main_loop() as SceneTree
		for _i in probe_delay_frames:
			await loop.process_frame
		active -= 1
		return canned if canned else NetwProbeResult.unsupported()


# A directory that emits a fixed lobby list on demand.
class _FakeDirectory:
	extends LobbyDirectory

	var canned_lobbies: Array[LobbyDirectory.LobbyInfo] = []


	func _scheme() -> StringName:
		return &"fake"


	func _list_lobbies() -> void:
		lobby_list_updated.emit(canned_lobbies)


	func _leave_lobby() -> void:
		pass


	func _host_lobby(_config: NetwHostConfig) -> MultiplayerPeer:
		return null


	func _join_lobby_peer(_lobby_id: int) -> MultiplayerPeer:
		return null


func _make_browser(transport: NetwTransport) -> Array:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	NetwConnector.of(api).transports = [transport]
	return [api, null, NetwServerBrowser.new(api)]


func _fake_target(address: String = "1") -> NetwConnectTarget:
	var target := NetwConnectTarget.new()
	target.scheme = &"fake"
	target.address = address
	return target


func test_saved_target_probe_stays_ok_when_app_tags_match() -> void:
	var info := NetwServerInfo.new()
	info.app_id = &"" # matches the default empty local tag
	var transport := _FakeTransport.new()
	transport.canned = NetwProbeResult.ok(info, 12)
	var rig := _make_browser(transport)
	var browser: NetwServerBrowser = rig[2]

	var seen: Array = []
	browser.target_updated.connect(func(t, r): seen.append([t, r]))
	var target := _fake_target()
	browser.probe(target)
	await get_tree().process_frame

	assert_int(seen.size()).is_equal(1)
	var result: NetwProbeResult = browser.get_result(target)
	assert_int(result.status).is_equal(NetwProbeResult.Status.OK)
	rig[0].embedding.dispose()


func test_saved_target_probe_flags_incompatible_app_tag() -> void:
	var info := NetwServerInfo.new()
	info.app_id = &"other-build"
	var transport := _FakeTransport.new()
	transport.canned = NetwProbeResult.ok(info, 5)
	var rig := _make_browser(transport)
	var browser: NetwServerBrowser = rig[2]

	var target := _fake_target()
	browser.probe(target)
	await get_tree().process_frame

	var result: NetwProbeResult = browser.get_result(target)
	assert_int(result.status).is_equal(NetwProbeResult.Status.INCOMPATIBLE)
	rig[0].embedding.dispose()


func test_probe_scheduler_caps_concurrency() -> void:
	var transport := _FakeTransport.new()
	transport.canned = NetwProbeResult.unreachable("no server")
	transport.probe_delay_frames = 3
	var rig := _make_browser(transport)
	var browser: NetwServerBrowser = rig[2]
	browser.max_concurrent_probes = 2

	# A one-element array so the lambda mutates shared state; a captured int would
	# be copied by value.
	var done := [0]
	browser.target_updated.connect(func(_t, _r): done[0] += 1)
	for i in 5:
		browser.probe(_fake_target(str(i)))

	var guard := 0
	while done[0] < 5 and guard < 120:
		await get_tree().process_frame
		guard += 1

	assert_int(done[0]).is_equal(5)
	assert_int(transport.peak).is_less_equal(2)
	rig[0].embedding.dispose()


func test_directory_registered_after_construction_is_bound() -> void:
	var transport := _FakeTransport.new()
	var rig := _make_browser(transport)
	var api: NetwMultiplayer = rig[0]
	var browser: NetwServerBrowser = rig[2]

	var directory := _FakeDirectory.new()
	directory.name = "FakeDir"
	directory.canned_lobbies = [
		LobbyDirectory.LobbyInfo.make(7, "Room 7", 2, 8, { "app_id": "" }),
	]
	add_child(directory)
	api.register_service(directory, LobbyDirectory)

	var updates: Array = []
	browser.directory_list_updated.connect(func(id, l): updates.append(id))
	browser.refresh()

	assert_int(browser.targets.size()).is_equal(1)
	assert_str(browser.targets[0].address).is_equal("7")
	assert_str(String(browser.targets[0].scheme)).is_equal("fake")
	assert_int(updates.size()).is_equal(1)

	directory.queue_free()
	api.embedding.dispose()


func test_directory_registered_before_construction_is_adopted() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var directory := _FakeDirectory.new()
	directory.name = "EarlyDir"
	directory.canned_lobbies = [
		LobbyDirectory.LobbyInfo.make(3, "Early", 1, 4, { "app_id": "" }),
	]
	add_child(directory)
	api.register_service(directory, LobbyDirectory)

	NetwConnector.of(api).transports = [_FakeTransport.new()]
	var browser := NetwServerBrowser.new(api)
	browser.refresh()

	assert_int(browser.get_discovered(&"EarlyDir").size()).is_equal(1)

	directory.queue_free()
	api.embedding.dispose()


func test_unregistering_directory_drops_its_lobbies() -> void:
	var transport := _FakeTransport.new()
	var rig := _make_browser(transport)
	var api: NetwMultiplayer = rig[0]
	var browser: NetwServerBrowser = rig[2]

	var directory := _FakeDirectory.new()
	directory.name = "DropDir"
	directory.canned_lobbies = [
		LobbyDirectory.LobbyInfo.make(1, "L", 0, 4, { "app_id": "" }),
	]
	add_child(directory)
	api.register_service(directory, LobbyDirectory)
	browser.refresh()
	assert_int(browser.targets.size()).is_equal(1)

	api.unregister_service(directory, LobbyDirectory)
	assert_int(browser.targets.size()).is_equal(0)

	directory.queue_free()
	api.embedding.dispose()


func test_saved_target_persistence_round_trip() -> void:
	var path := "user://_test_browser_%d.tres" % Time.get_ticks_usec()
	var transport := _FakeTransport.new()
	var rig := _make_browser(transport)
	var browser: NetwServerBrowser = rig[2]

	var target := _fake_target("host.example:7777")
	target.display_name = "My Server"
	browser.add_target(target)
	browser.save_server_list(path)
	assert_bool(FileAccess.file_exists(path)).is_true()

	var rig2 := _make_browser(_FakeTransport.new())
	var reloaded: NetwServerBrowser = rig2[2]
	var added: Array = []
	reloaded.target_added.connect(func(t): added.append(t))
	reloaded.load_server_list(path)

	var saved := reloaded.saved_targets
	assert_int(saved.size()).is_equal(1)
	assert_str(saved[0].address).is_equal("host.example:7777")
	assert_str(saved[0].display_name).is_equal("My Server")
	assert_int(added.size()).is_equal(1)

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
	rig[0].embedding.dispose()
	rig2[0].embedding.dispose()


func test_legacy_list_referencing_deleted_script_starts_fresh() -> void:
	# A list saved under an older addon layout references scripts that no longer
	# exist. Loading it must not crash and returns an empty list.
	var path := "user://_test_legacy_%d.tres" % Time.get_ticks_usec()
	var file := FileAccess.open(path, FileAccess.WRITE)
	file.store_string(
		"[gd_resource type=\"Resource\" load_steps=2 format=3]\n\n"
		+ "[ext_resource type=\"Script\" "
		+ "path=\"res://addons/networked/transport/enet_backend.gd\" "
		+ "id=\"1_gone\"]\n\n"
		+ "[resource]\n"
		+ "script = ExtResource(\"1_gone\")\n",
	)
	file.close()

	var list := NetwServerList.load_or_new(path)
	assert_object(list).is_not_null()
	assert_int(list.targets.size()).is_equal(0)

	DirAccess.remove_absolute(ProjectSettings.globalize_path(path))
