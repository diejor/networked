## Laws for the read-only prediction-boundary debugger.
class_name TestPredictionBoundaryOverlay
extends NetwTestSuite

const Overlay := preload(
	"res://addons/networked/debug/prediction_boundary_overlay.gd"
)
const PredictionHandle := NetwLagCompensationInterface.PredictionHandle


func test_settled_basis_only_reads_open_episode_evidence() -> void:
	var overlay := Overlay.new()
	var episode := {
		&"generator": { &"transition": 7 },
		&"last_comparison_transition": 11,
		&"disposition": { &"state": PredictionHandle.EpisodeState.OPEN },
	}

	assert_int(overlay._basis_transition(
		episode,
	)).is_equal(11)
	episode.erase(&"last_comparison_transition")
	assert_int(overlay._basis_transition(
		episode,
	)).is_equal(7)
	assert_int(overlay._basis_transition(
		{ },
	)).is_equal(-1)
	episode[&"disposition"] = {
		&"state": PredictionHandle.EpisodeState.CLOSED,
	}
	assert_int(overlay._basis_transition(
		episode,
	)).is_equal(-1)
	overlay.free()


func test_contact_glyph_names_every_witness_class() -> void:
	var overlay := Overlay.new()
	var witness := PredictionHandle.WitnessClass
	var all := witness.SUPPORT | witness.STATIC | witness.DYNAMIC_ENTITY

	assert_str(overlay._witness_glyph(all)).is_equal(
		"SUPPORT+STATIC+DYNAMIC",
	)
	assert_str(overlay._witness_glyph(0)).is_equal("NONE")
	overlay.free()


func test_builds_four_layers_in_two_and_three_dimensions() -> void:
	var overlay_2d := Overlay.new()
	overlay_2d._build_2d()
	var overlay_3d := Overlay.new()
	overlay_3d._build_3d()

	assert_int(overlay_2d._rings_2d.size()).is_equal(4)
	assert_int(overlay_3d._rings_3d.size()).is_equal(4)
	overlay_2d.free()
	overlay_3d.free()


func test_stats_expose_the_current_acknowledgement_age() -> void:
	var handle := PredictionHandle.new()

	assert_int(handle.stats()[&"ack_age_ticks"]).is_equal(
		handle.ack_age_ticks,
	)


func test_demotion_tint_clears_when_speculation_resumes() -> void:
	var overlay := Overlay.new()
	var handle := PredictionHandle.new()
	var snapshot := {
		&"episode": { &"disposition": { &"demoted": true } },
	}
	handle.sim_mode = PredictionHandle.SimMode.DISPLAY
	var demoted := overlay._annotation_color(handle, snapshot)
	handle.sim_mode = PredictionHandle.SimMode.SPECULATIVE
	var resumed := overlay._annotation_color(handle, snapshot)

	assert_that(demoted).is_not_equal(Color.WHITE)
	assert_that(resumed).is_equal(Color.WHITE)
	overlay.free()


func test_debug_gate_attaches_and_unregister_releases_overlay() -> void:
	var tree := get_tree()
	var previous_hint := tree.debug_collisions_hint
	tree.debug_collisions_hint = true
	var scenario := PredictionScenario.new()
	await scenario.setup(self)
	var predicted := await scenario.add_predicted_entity()
	await tree.process_frame
	var overlay := predicted.client_root.get_node_or_null(
		"PredictionBoundaryOverlay",
	)
	var attached := overlay != null

	scenario.client_sim.unregister_prediction(predicted.client_entity)
	await tree.process_frame
	var released := predicted.client_root.get_node_or_null(
		"PredictionBoundaryOverlay",
	) == null
	tree.debug_collisions_hint = previous_hint

	assert_bool(attached).is_true()
	assert_bool(released).is_true()
