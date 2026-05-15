## Integration tests for [InterestService] mirror replication.
##
## Covers the wire contract between the server (authority) and clients
## (mirror): layer-created / layer-disposed / member-added /
## member-removed. Subjects do not cross the wire so they are not asserted
## here.
class_name TestInterestService
extends NetworkedTestSuite


var harness: NetworkTestHarness
var server: MultiplayerTree
var client0: MultiplayerTree


func before_test() -> void:
	harness = NetworkTestHarness.new()
	add_child(harness)
	auto_free(harness)
	await harness.setup()
	client0 = await harness.add_client()
	server = harness.get_server()


func after_test() -> void:
	if is_instance_valid(harness):
		await harness.teardown()
	await drain_frames(get_tree(), 3)


# ---------------------------------------------------------------------------
# ROOT replication: every connected peer becomes a ROOT member, and that
# membership is reflected in their local mirror.
# ---------------------------------------------------------------------------

func test_root_layer_mirrors_on_connected_client() -> void:
	var client_peer := client0.multiplayer_peer.get_unique_id()
	await wait_until(
		func(): return client0.interest.layer(NetwInterest.ROOT_ID) != null)
	var mirror := client0.interest.layer(NetwInterest.ROOT_ID)
	assert_that(mirror).is_not_null()
	assert_that(mirror.has_member(client_peer)).is_true()


# ---------------------------------------------------------------------------
# Server-side mutation propagates to client mirror.
# ---------------------------------------------------------------------------

func test_server_create_layer_then_add_member_replicates() -> void:
	var l := server.interest.create_layer(
			&"int_test:scene", NetwInterestLayer.Policy.ISOLATE)
	assert_that(l).is_not_null()

	var client_peer := client0.multiplayer_peer.get_unique_id()
	l.add_member(client_peer)

	await wait_until(
		func(): return client0.interest.layer(&"int_test:scene") != null)

	var mirror := client0.interest.layer(&"int_test:scene")
	assert_that(mirror).is_not_null()
	assert_that(mirror._is_mirror).is_true()
	assert_that(mirror.policy).is_equal(NetwInterestLayer.Policy.ISOLATE)
	assert_that(mirror.has_member(client_peer)).is_true()


func test_member_added_signal_fires_on_client_mirror() -> void:
	var l := server.interest.create_layer(
			&"int_test:signals", NetwInterestLayer.Policy.GRANT)
	var client_peer := client0.multiplayer_peer.get_unique_id()

	# First add bootstraps the mirror; wait for it.
	l.add_member(client_peer)
	await wait_until(
		func(): return client0.interest.layer(&"int_test:signals") != null)

	var mirror := client0.interest.layer(&"int_test:signals")
	var received: Array[int] = []
	mirror.member_added.connect(func(p: int): received.append(p))

	# Add a second peer id (synthetic; real peer not needed for transport test).
	l.add_member(9999)
	await wait_until(func(): return mirror.has_member(9999))
	assert_that(received).contains([9999])


# ---------------------------------------------------------------------------
# Removing the local peer from a layer tears its mirror down.
# ---------------------------------------------------------------------------

func test_remove_member_disposes_leaver_mirror() -> void:
	var l := server.interest.create_layer(
			&"int_test:leave", NetwInterestLayer.Policy.ISOLATE)
	var client_peer := client0.multiplayer_peer.get_unique_id()

	l.add_member(client_peer)
	await wait_until(
		func(): return client0.interest.layer(&"int_test:leave") != null)

	l.remove_member(client_peer)
	await wait_until(
		func(): return client0.interest.layer(&"int_test:leave") == null)
	assert_that(client0.interest.layer(&"int_test:leave")).is_null()


# ---------------------------------------------------------------------------
# Late-joiner catch-up: existing members are sent on first observation.
# ---------------------------------------------------------------------------

func test_late_observer_receives_existing_members() -> void:
	var l := server.interest.create_layer(
			&"int_test:catchup", NetwInterestLayer.Policy.GRANT)
	# Pre-populate with synthetic peer ids.
	l.add_member(1001)
	l.add_member(1002)
	# Now the client peer observes; bootstrap should include 1001 and 1002.
	var client_peer := client0.multiplayer_peer.get_unique_id()
	l.add_member(client_peer)

	await wait_until(
		func(): return client0.interest.layer(&"int_test:catchup") != null)
	var mirror := client0.interest.layer(&"int_test:catchup")
	await wait_until(
		func(): return mirror.has_member(1001) and mirror.has_member(1002))
	assert_that(mirror.has_member(1001)).is_true()
	assert_that(mirror.has_member(1002)).is_true()
	assert_that(mirror.has_member(client_peer)).is_true()


# ---------------------------------------------------------------------------
# Disposing a layer on the server tears down the mirror on observers.
# ---------------------------------------------------------------------------

func test_layer_dispose_propagates_to_mirror() -> void:
	var l := server.interest.create_layer(
			&"int_test:dispose", NetwInterestLayer.Policy.GRANT)
	var client_peer := client0.multiplayer_peer.get_unique_id()
	l.add_member(client_peer)
	await wait_until(
		func(): return client0.interest.layer(&"int_test:dispose") != null)

	l.dispose_immediate()
	await wait_until(
		func(): return client0.interest.layer(&"int_test:dispose") == null)
	assert_that(client0.interest.layer(&"int_test:dispose")).is_null()
