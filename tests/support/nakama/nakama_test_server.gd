## Skip gate for live Nakama tests.
##
## Live tests require a running server (see
## [code]tests/support/nakama/docker-compose.yml[/code]). They opt in through the
## [code]NAKAMA_TEST_HOST[/code] environment variable and verify the server is
## actually reachable, so a machine without Docker, or with the container down,
## skips cleanly instead of failing or hanging.
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

## Environment variable a developer sets to opt in to live tests.
const HOST_ENV := "NAKAMA_TEST_HOST"

## Human-readable reason surfaced by GdUnit when a live suite is skipped.
const SKIP_REASON := \
		"Live Nakama tests need a server: set NAKAMA_TEST_HOST and start " \
		+ "tests/support/nakama/docker-compose.yml."

# Milliseconds to wait for the reachability probe before giving up.
const _PROBE_TIMEOUT_MS := 500


## Returns the opted-in host, or an empty string when [constant HOST_ENV] is unset.
static func host() -> String:
	return OS.get_environment(HOST_ENV)


## Returns [code]true[/code] when live Nakama tests should be skipped.
##
## Skips when [constant HOST_ENV] is unset (no opt-in) or the server is not
## reachable. Pure static check with a bounded probe, safe to evaluate at
## GdUnit discovery time inside a [code]do_skip[/code] expression.
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
