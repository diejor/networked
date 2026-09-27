class_name NakamaTestServer

const DEFAULT_PORT := 7350

const HOST_SETTING := "networked/tests/nakama_host"

const HOST_ENV := "NAKAMA_TEST_HOST"

const SKIP_REASON := "Live Nakama tests need a server: set the networked/tests/nakama_host " \
		+ "project setting (or NAKAMA_TEST_HOST) and start " \
		+ "tests/nakama/docker-compose.yml."

const _PROBE_TIMEOUT_MS := 500


static func host() -> String:
	var from_setting := String(ProjectSettings.get_setting(HOST_SETTING, ""))
	if not from_setting.is_empty():
		return from_setting
	return OS.get_environment(HOST_ENV)


static func unavailable() -> bool:
	var target := host()
	if target.is_empty():
		return true
	return not _reachable(target, DEFAULT_PORT)


static func _reachable(target: String, port: int) -> bool:
	var tcp := StreamPeerTCP.new()
	if tcp.connect_to_host(target, port) != OK:
		return false
	var deadline := Time.get_ticks_msec() + _PROBE_TIMEOUT_MS
	while Time.get_ticks_msec() < deadline:
		tcp.poll()
		match tcp.get_status():
			StreamPeerTCP.STATUS_CONNECTED:
				tcp.disconnect_from_host()
				return true
			StreamPeerTCP.STATUS_ERROR:
				return false
		OS.delay_msec(10)
	tcp.disconnect_from_host()
	return false
