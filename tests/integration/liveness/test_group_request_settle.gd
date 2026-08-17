## A broadcast request nobody is live for, observed with no frame.
##
## [method Netw.request_all] fixes its awaited set at send time, and an entity
## no peer sees fixes it empty. The group is already satisfied the moment it is
## built, and it still cannot settle there: the caller has not been handed the
## promise yet, so nothing has chained onto it. These cases pin the settle to
## the session's own pump.
##
## A listen server with no clients is the peer under test because it is the one
## session that is genuinely online and still has nobody live: an offline
## session resolves its own loopback as the single recipient, so its group is
## never empty and the case would be vacuous.
class_name TestGroupRequestSettle
extends NetwTestSuite


class RequestTarget:
	extends Node

	@rpc("any_peer", "call_remote", "reliable") func poll_ready() -> bool:
		return true


var harness: NetwTestHarness
var host: MultiplayerTree
var target: RequestTarget


func before_test() -> void:
	harness = make_harness()
	await harness.setup_factory(NetwTestSuite.create_scene_manager)
	host = await harness.add_listen_server(
		harness.make_sceneless_payload("request_host"),
	)
	await drain_frames(get_tree(), 2)

	target = RequestTarget.new()
	target.name = "RequestTarget"
	host.add_child(target)
	auto_free(target)
	var entity := NetwEntity.ensure(target)
	host.api._native_core.liveness_bind_route(9, entity)
	Netw.configure_rpc(target.poll_ready)


func after_test() -> void:
	await harness.teardown()
	await super.after_test()


## Nobody is live for the entity, so the group settles at the pump rather than
## on the spot, which is what leaves room to subscribe to it.
func test_a_group_nobody_is_live_for_settles_at_the_pump() -> void:
	assert_array(host.api._replication.live_peers(NetwEntity.of(target))) \
			.override_failure_message(
				"a group with recipients would settle by reply, not by pump",
			).is_empty()

	var batch := Netw.request_all(target.poll_ready)
	var completions: Array[int] = []
	batch.completed.connect(
		func(results: Dictionary) -> void: completions.append(results.size())
	)

	assert_bool(batch.is_completed).is_false()

	host.multiplayer.poll()

	assert_bool(batch.is_completed).is_true()
	assert_array(completions).contains_exactly([0])
