## Skip gate for live Nakama tests.
##
## Live tests require a running server (see
## [code]tests/support/nakama/docker-compose.yml[/code]). They opt in through the
## [code]networked/tests/nakama_host[/code] project setting (or the
## [code]NAKAMA_TEST_HOST[/code] environment variable as a fallback) and verify
## the server is actually reachable, so a machine without Docker, or with the
## container down, skips cleanly instead of failing or hanging.
## [codeblock]
## # Whole-suite skip (evaluated once at discovery, reports "skipped"):
## func before(
##         do_skip = NakamaTestServer.unavailable(),
##         skip_reason = NakamaTestServer.SKIP_REASON,
## ) -> void:
##     pass
## [/codeblock]
class_name NakamaTestServer

## Default Nakama client API / socket port, matching the Docker stack and
## [code]Nakama.gd[/code] DEFAULT_PORT.
const DEFAULT_PORT := 7350

## Project setting a developer points at a running Nakama to opt in to live
## tests. Lives only in this repo's [code]project.godot[/code]; the test code is
## export-ignored so it never reaches a game's settings.
const HOST_SETTING := "networked/tests/nakama_host"

## Environment variable fallback, honored when [constant HOST_SETTING] is empty so
## CI can opt in without committing a host.
const HOST_ENV := "NAKAMA_TEST_HOST"

## Human-readable reason surfaced by GdUnit when a live suite is skipped.
const SKIP_REASON := \
		"Live Nakama tests need a server: set the networked/tests/nakama_host " \
		+ "project setting (or NAKAMA_TEST_HOST) and start " \
		+ "tests/support/nakama/docker-compose.yml."

# Milliseconds to wait for the reachability probe before giving up.
const _PROBE_TIMEOUT_MS := 500


## Returns the opted-in host, or an empty string when neither
## [constant HOST_SETTING] nor [constant HOST_ENV] is set.
##
## The project setting wins; the environment variable is the fallback. Reading a
## missing setting returns the empty default, so a project without the key (any
## game) is a clean no-op.
static func host() -> String:
	var from_setting := String(ProjectSettings.get_setting(HOST_SETTING, ""))
	if not from_setting.is_empty():
		return from_setting
	return OS.get_environment(HOST_ENV)


## Returns [code]true[/code] when live Nakama tests should be skipped.
##
## Skips when neither [constant HOST_SETTING] nor [constant HOST_ENV] is set (no
## opt-in) or the server is not reachable. Pure static check with a bounded probe,
## safe to evaluate at GdUnit discovery time inside a [code]do_skip[/code]
## expression.
static func unavailable() -> bool:
	var target := host()
	if target.is_empty():
		return true
	return not _reachable(target, DEFAULT_PORT)


# Opens a short-lived TCP connection to confirm the server is accepting
# connections. Returns false on any error or timeout.
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
