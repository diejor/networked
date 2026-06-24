## Pure-logic tests for the Discord Activity layer, no Discord and no Nakama.
##
## Everything here runs headless with no server: the fake-identity seam on
## [DiscordActivityService], the backend-side device-id normalization and proxy
## URL building on [NakamaDiscordRendezvous], and the dedicated WSS rendezvous.
## The live host-or-join path is exercised separately by
## [code]tests/live/nakama/test_discord_rendezvous.gd[/code].
class_name TestDiscordActivity
extends NetwTestSuite


func test_injected_instance_marks_in_discord() -> void:
	# The test fixture injects what the browser would supply, so the service
	# reports embedded and registers, with no SDK and no browser.
	var service := NetwTestDiscordService.new()
	auto_free(service)
	service.fake_instance_id = "room1"
	service.fake_device_id = "alice"

	assert_bool(service.in_discord()).is_true()
	assert_bool(service.should_register()).is_true()
	assert_str(service.instance_id()).is_equal("room1")
	assert_str(service.device_id()).is_equal("alice")


func test_not_in_discord_without_instance() -> void:
	var service := DiscordActivityService.new()
	auto_free(service)

	# No browser query string and no injected id: the service stays dormant so a
	# normal desktop or web build keeps its usual backends.
	assert_bool(service.in_discord()).is_false()
	assert_bool(service.should_register()).is_false()
	assert_str(service.instance_id()).is_equal("")


func test_normalized_device_id_clamps_to_nakama_bounds() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# Empty stays empty so the session falls back to OS.get_unique_id().
	rdv.device_id = ""
	assert_str(rdv._normalized_device_id()).is_equal("")

	# A short fake id is prefixed so it clears Nakama's 10-byte floor and two
	# instances stay distinct users.
	rdv.device_id = "alice"
	assert_str(rdv._normalized_device_id()).is_equal("netw-discord-alice")

	# A full-length Discord snowflake passes through untouched.
	rdv.device_id = "123456789012345678"
	assert_str(rdv._normalized_device_id()).is_equal("123456789012345678")

	# Anything over the 128-byte ceiling is clamped.
	rdv.device_id = "x".repeat(200)
	assert_int(rdv._normalized_device_id().length()).is_equal(128)


func test_proxy_base_uses_config_host_and_prefix() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# A host already on the iframe origin resolves without any service lookup,
	# which is how the standalone smoke scene wires the seam.
	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/nakama")

	# The trailing prefix is the only configurable part of the proxy base.
	rdv.proxy_prefix = "relay"
	assert_str(rdv._resolve_proxy_base(null, "987654321.discordsays.com")) \
			.is_equal("987654321.discordsays.com/.proxy/relay")


func test_proxy_base_empty_for_direct_connection() -> void:
	var rdv := NakamaDiscordRendezvous.new()

	# A direct host with no reachable service leaves the connection untouched.
	assert_str(rdv._resolve_proxy_base(null, "127.0.0.1")).is_equal("")


func test_dedicated_rendezvous_builds_instance_keyed_url() -> void:
	var rdv := DedicatedDiscordRendezvous.new()
	rdv.public_host = "game.example.com"

	var target := rdv.resolve("room1", null)
	assert_object(target).is_not_null()
	assert_str(target.address).is_equal("wss://game.example.com/?instance=room1")
	# A non-empty address keeps the service on the join path; the dedicated server
	# elects the host by keying rooms on ?instance=.
	assert_object(target.backend).is_instanceof(WebSocketBackend)


func test_dedicated_rendezvous_refuses_without_host() -> void:
	var rdv := DedicatedDiscordRendezvous.new()

	# No server configured, so the seam refuses rather than inventing a target.
	var target := rdv.resolve("room1", null)
	assert_object(target).is_null()
