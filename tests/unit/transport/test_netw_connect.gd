## Unit tests for [NetwConnect]: discovery signal relay, target round-trip, and
## driving a host attempt through the wrapped [NetwConnector] to ONLINE.
class_name TestNetwConnect
extends NetwTestSuite


func after_test() -> void:
	LocalLoopbackSession.get_shared_session().reset()


func _make_target(address: String = "127.0.0.1") -> NetwConnectTarget:
	var target := NetwConnectTarget.new()
	target.scheme = &"enet"
	target.address = address
	target.display_name = "T_" + address
	return target


func test_relays_target_added() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var facade := NetwConnect.new(api)

	var captured: Array = []
	facade.target_added.connect(func(t): captured.append(t))

	var target := _make_target()
	facade.add_target(target)

	assert_int(captured.size()).is_equal(1)
	assert_that(captured[0]).is_same(target)
	api.dispose()


func test_forwards_add_target_round_trip() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var facade := NetwConnect.new(api)

	var target := _make_target("10.0.0.1")
	facade.add_target(target)

	var targets := facade.targets
	assert_int(targets.size()).is_equal(1)
	assert_that(targets[0]).is_same(target)
	api.dispose()


func test_available_transports_come_from_the_registry() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var facade := NetwConnect.new(api)

	var schemes: Array = []
	for transport in facade.available_transports():
		schemes.append(String(transport.scheme()))

	# The built-in transports register on class load, so a bare facade offers
	# them with no per-browser configuration.
	assert_bool(schemes.has("local")).is_true()
	api.dispose()


func test_host_drives_to_online_and_emits_connected() -> void:
	LocalLoopbackSession.get_shared_session().reset()
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var facade := NetwConnect.new(api)

	var connected := [false]
	facade.connected.connect(func(): connected[0] = true)

	var config := NetwHostConfig.new()
	config.scheme = &"local"

	var err_holder := [ERR_BUG]
	var pump := func() -> void:
		err_holder[0] = await facade.host(config, null)
	pump.call()

	var guard := 0
	while api.state != NetwSessionInterface.State.ONLINE and guard < 40:
		facade.poll(0.05)
		await get_tree().process_frame
		guard += 1

	assert_int(api.state).is_equal(NetwSessionInterface.State.ONLINE)
	assert_bool(connected[0]).is_true()
	api.dispose()


func test_is_valid_flips_after_session_freed() -> void:
	var api := NetwMultiplayer.new(SceneMultiplayer.new())
	var facade := NetwConnect.new(api)

	assert_bool(facade.is_valid()).is_true()

	api.dispose()
	api = null
	# Force the weakref to clear so the facade reports the session gone.
	await get_tree().process_frame
	assert_bool(facade.is_valid()).is_false()
