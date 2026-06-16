## Real-node prediction wiring: authority direction and peer-symmetric virtual
## names (ports the lag-comp spike tier A rig cases).
##
## The predicted pair must derive its replication direction from authority alone:
## the [StateSynchronizer] is server-authored, the [InputSynchronizer] is
## controller-authored, and both peers compose the same virtual-name map so the
## timeline keys match without an identity server. Proven on the shipping nodes
## through [PredictionScenario] instead of the retired spike doubles.
class_name TestPredictionWiring
extends NetwTestSuite

const RIGHT := { &"motion": Vector2.RIGHT }


func test_authority_derives_direction() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.latency_both(4)
	s.hold_input(p, RIGHT)
	s.run(40)

	var controller := s.client.multiplayer_peer.get_unique_id()
	# State is server-authored; input is controller-authored.
	assert_int(p.server_state.get_multiplayer_authority()).is_equal(1)
	assert_int(p.client_input.get_multiplayer_authority()).is_equal(controller)

	# Functional direction: client input reached the server, server state reached
	# the client (both timelines advanced).
	assert_int(p.consumed).is_greater(0)
	assert_int(p.observer.divergence_log.size()).is_greater(0)


func test_peer_symmetric_virtual_names() -> void:
	var s := PredictionScenario.new()
	await s.setup(self)
	var p := await s.add_predicted_entity()
	s.run(5)

	# The same scene yields the same virtual map on both peers, so the timeline
	# keys match without an identity server.
	assert_array(p.server_state.get_virtual_properties()) \
			.is_equal(p.client_state.get_virtual_properties())
