## The role ladder's prediction rung, resolved from a real engine rather than
## from a fact set a case wrote by hand.
##
## [NetwDisplayRoleFacts] is proven natively over every combination, but
## [member NetwDisplayRoleFacts.prediction_registered] and the simulation fact
## beside it are only as good as what computes them, and nothing until now
## drove a registered engine into a display case at all. This suite is that
## rig: one predicted pair whose predicted value is also a displayed track, so
## the ladder answers from
## [method NetwPredictionHandle.is_registered] and
## [member NetwPredictionHandle.sim_mode].
class_name TestPredictedDisplayRoles
extends NetwTestSuite

var scene: PredictionScenario


func before_test() -> void:
	scene = PredictionScenario.new()
	scene.body_type = DisplayedSimBody


## Verify the predicting peer displays its prediction and its authority
## displays the simulation it consumes, which is the ladder's first rung and
## the rung under it answering from one registered engine.
func test_the_predicting_peer_and_its_authority_resolve_apart() -> void:
	await scene.setup(self)
	var p := await scene.add_predicted_entity()
	scene.run(8)

	var client_display := _display_of(p.client_body)
	var server_display := _display_of(p.server_body)
	assert_object(client_display).is_not_null()
	assert_object(server_display).is_not_null()

	assert_int(client_display.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.PREDICTED
	)
	assert_int(server_display.resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.AUTHORITY
	)


## Verify a registered engine demoted to
## [constant NetwPredict.SimMode.DISPLAY] displays as remote, which is the rung
## that exists because the authority rule under it would disable the pump
## instead and nothing else writes the display once the simulation stops.
func test_a_demoted_engine_displays_remote_rather_than_disabled() -> void:
	await scene.setup(self)
	var p := await scene.add_predicted_entity()
	scene.run(8)

	var entity := NetwEntity.of(p.client_body)
	assert_bool(entity.prediction.is_registered()).is_true()
	entity.prediction.sim_mode = NetwPredict.SimMode.DISPLAY
	scene.run(2)

	assert_int(_display_of(p.client_body).resolved_display_role).is_equal(
		NetwDisplayHandle.DisplayRole.REMOTE
	)


func _display_of(node: Node) -> NetwDisplayHandle:
	var entity := NetwEntity.of(node)
	return entity.interpolation as NetwDisplayHandle if entity else null
