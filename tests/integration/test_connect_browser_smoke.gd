## Headless smoke test for [ConnectSession] + [ConnectBrowser]:
## drive a session against a live host, assert the public API
## (signals, get_result) reports a probed direct target as OK.
##
## Reaches into the browser's session via the typed export rather
## than walking its scene tree, so the test does not break when the
## reference UI's node layout changes.
class_name TestConnectBrowserSmoke
extends NetwTestSuite


const _BROWSER_SCENE := preload(
	"res://addons/networked/connect/ui/connect_browser.tscn"
)


class _SmokeSource:
	extends ServerInfoSource
	func build_server_info(_tree: MultiplayerTree) -> ServerInfo:
		var info := ServerInfo.new()
		info.players = 1
		info.max_players = 8
		info.is_local_listener = true
		return info


func test_session_reports_ok_for_probed_direct_target() -> void:
	var source := _SmokeSource.new()
	var host := await EnetTestSupport.start_host(self, source)
	assert_that(host).is_not_empty()

	var temp_path := "user://_test_connect_smoke_%d.tres" % (
		Time.get_ticks_usec()
	)

	var target := JoinTarget.new()
	target.display_name = "Smoke Host"
	target.address = "127.0.0.1"
	var client_backend := ENetBackend.new()
	client_backend.port = host.port
	target.backend = client_backend

	var list := ServerList.new()
	list.targets = [target]
	ServerList.save(list, temp_path)

	var browser: ConnectBrowser = _BROWSER_SCENE.instantiate()
	browser.server_list_path = temp_path
	add_child(browser)

	var session: ConnectSession = browser.session
	assert_that(session).is_not_null()

	var loaded := session.get_direct_targets()
	assert_int(loaded.size()).is_equal(1)
	var loaded_target: JoinTarget = loaded[0]

	await wait_until(
		func() -> bool:
			var r := session.get_result(loaded_target)
			return r != null and r.is_ok(),
		3.0,
	)

	var result := session.get_result(loaded_target)
	assert_that(result).is_not_null()
	assert_int(result.status).is_equal(ServerInfoResult.Status.OK)
	assert_int(result.info.players).is_equal(1)

	browser.queue_free()
	DirAccess.remove_absolute(ProjectSettings.globalize_path(temp_path))
	await EnetTestSupport.stop_tree(host.tree)
