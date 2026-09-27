class_name TestNakamaProxyOverride
extends NetwTestSuite

const _FACADE_PATH := "res://addons/com.heroiclabs.nakama/Nakama.gd"

var _rendezvous: NakamaDiscordRendezvous


@warning_ignore("unused_parameter")
func before(
		do_skip = not NakamaWrapper.is_addon_present(),
		skip_reason = "Nakama addon is not installed.",
) -> void:
	_rendezvous = NakamaDiscordRendezvous.new()
	_rendezvous.bind(null)


func after() -> void:
	NakamaWrapper.proxy_base_resolver = Callable()


func test_proxy_override_when_active() -> void:
	var mt: MultiplayerTree = auto_free(MultiplayerTree.new())

	var activity: DiscordActivityService = auto_free(DiscordActivityService.new())
	activity.client_id = "123456789"

	mt.add_child(activity)
	mt.api.service_register(activity, DiscordActivityService)

	var service := NakamaSessionService.new()
	mt.add_child(service)
	mt.api.service_register(service, NakamaSessionService)

	var facade_script := load(_FACADE_PATH) as Script
	service._facade = facade_script.new()
	auto_free(service._facade)

	service._client = service._facade.create_client(
		"defaultkey",
		"127.0.0.1",
		7350,
		"http",
	)

	var socket: RefCounted = service.create_socket()
	auto_free(socket)

	assert_str(socket._base_uri) \
			.is_equal("wss://123456789.discordsays.com/.proxy/nakama")


func test_proxy_override_via_host_name() -> void:
	var mt: MultiplayerTree = auto_free(MultiplayerTree.new())

	var service: NakamaSessionService = auto_free(NakamaSessionService.new())
	service.host = "987654321.discordsays.com"
	mt.add_child(service)
	mt.api.service_register(service, NakamaSessionService)

	var facade_script := load(_FACADE_PATH) as Script
	service._facade = facade_script.new()
	auto_free(service._facade)

	service._client = service._facade.create_client(
		"defaultkey",
		service.host,
		7350,
		"http",
	)

	var socket: RefCounted = service.create_socket()
	auto_free(socket)

	assert_str(socket._base_uri) \
			.is_equal("wss://987654321.discordsays.com/.proxy/nakama")


func test_no_override_without_client_id() -> void:
	var mt: MultiplayerTree = auto_free(MultiplayerTree.new())

	var activity: DiscordActivityService = auto_free(DiscordActivityService.new())
	mt.add_child(activity)
	mt.api.service_register(activity, DiscordActivityService)

	var service: NakamaSessionService = auto_free(NakamaSessionService.new())
	mt.add_child(service)
	mt.api.service_register(service, NakamaSessionService)

	var facade_script := load(_FACADE_PATH) as Script
	service._facade = facade_script.new()
	auto_free(service._facade)

	service._client = service._facade.create_client(
		"defaultkey",
		"127.0.0.1",
		7350,
		"http",
	)

	var socket: RefCounted = service.create_socket()
	auto_free(socket)

	assert_str(socket._base_uri) \
			.is_equal("ws://127.0.0.1:7350")


func test_no_override_when_no_activity_service() -> void:
	var mt: MultiplayerTree = auto_free(MultiplayerTree.new())

	var service: NakamaSessionService = auto_free(NakamaSessionService.new())
	mt.add_child(service)
	mt.api.service_register(service, NakamaSessionService)

	var facade_script := load(_FACADE_PATH) as Script
	service._facade = facade_script.new()
	auto_free(service._facade)

	service._client = service._facade.create_client(
		"defaultkey",
		"127.0.0.1",
		7350,
		"http",
	)

	var socket: RefCounted = service.create_socket()
	auto_free(socket)

	assert_str(socket._base_uri) \
			.is_equal("ws://127.0.0.1:7350")
