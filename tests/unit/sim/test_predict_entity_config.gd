## Laws for the verbs that declare a predicted entity's behavior.
##
## The archetype, the schedule, the island, and the recovery are separate
## decisions, each with parts that only mean something together. Declaring one
## through a typed fluent configurator makes its shape a parse-time fact and
## leaves the others alone. The archetype is the decision the others refine,
## so its preset applies whole and yields to any part declared on its own.
class_name TestPredictEntityConfig
extends NetwTestSuite

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const RecoveryPolicy := PredictionHandle.RecoveryPolicy
const BOMBER_PLAYER := preload("res://examples/bomber/game/player.tscn")


func _handle() -> PredictionHandle:
	return PredictionHandle.new()


# --- the archetype presets ---


# An archetype is one decision that derives the schedule and recovery bundle,
# so declaring it writes every part the entity did not decide itself.
func test_the_solver_archetype_applies_its_bundle() -> void:
	var handle := _handle()

	handle.archetype(PredictionHandle.Archetype.SOLVER_BODY)

	assert_int(handle.resolved_archetype()) \
			.is_equal(PredictionHandle.Archetype.SOLVER_BODY)
	assert_int(handle.resolved_schedule()) \
			.is_equal(PredictionHandle.Schedule.FRAME)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.REPEAT_LAST)
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_RECOVER)
	assert_int(handle.snap_restore) \
			.is_equal(PredictionHandle.RestoreMode.EXTRAPOLATED)
	assert_float(handle.teleport_threshold).is_equal(3.0)
	assert_int(handle.breach_response).is_equal(
		PredictionHandle.BreachResponse.DEMOTE,
	)
	assert_int(handle.stats()[&"schedule"]) \
			.is_equal(PredictionHandle.Schedule.FRAME)
	assert_int(handle.stats()[&"archetype"]) \
			.is_equal(PredictionHandle.Archetype.SOLVER_BODY)


func test_the_kinematic_archetype_applies_its_bundle() -> void:
	var handle := _handle()

	handle.archetype(PredictionHandle.Archetype.KINEMATIC)

	assert_int(handle.resolved_schedule()) \
			.is_equal(PredictionHandle.Schedule.TICK)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.STALL)
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_REPLAY)
	assert_int(handle.breach_response).is_equal(
		PredictionHandle.BreachResponse.PREDICT_THROUGH,
	)


func test_bomber_stays_benign_without_a_witness_declaration() -> void:
	var player: CharacterBody2D = auto_free(BOMBER_PLAYER.instantiate())
	var component := player.get_node(^"PredictionComponent") \
			as PredictionComponent
	var handle := _handle()

	component._push_config(handle)

	assert_int(handle.resolved_archetype()) \
			.is_equal(PredictionHandle.Archetype.KINEMATIC)
	assert_dict(handle.witness_config).is_empty()
	assert_int(handle.breach_response).is_equal(
		PredictionHandle.BreachResponse.PREDICT_THROUGH,
	)
	assert_array(component._get_configuration_warnings()).is_empty()


func test_the_fluent_recovery_declaration_overrides_breach_response() -> void:
	var handle := _handle()
	handle.archetype(PredictionHandle.Archetype.SOLVER_BODY)

	var recovery := handle.recovery().on_breach(
		PredictionHandle.BreachResponse.PREDICT_THROUGH,
	)

	assert_object(recovery).is_instanceof(PredictionHandle.RecoveryConfig)
	assert_int(handle.breach_response).is_equal(
		PredictionHandle.BreachResponse.PREDICT_THROUGH,
	)


# The archetype is one fact like any other: a scene that declared it owns it,
# so a code restatement is refused loudly and applies nothing.
func test_a_scene_declared_archetype_refuses_the_code_restatement() -> void:
	var handle := _handle()
	handle.scene_declared = { &"archetype": true }

	await assert_error(
		func() -> void:
			handle.archetype(
				PredictionHandle.Archetype.SOLVER_BODY,
			),
	).is_push_error(
		"PredictionHandle.archetype: the archetype is declared on "
		+ "the scene's PredictionComponent and configured from code. One "
		+ "source per fact: keep the scene value or the code call, not both. "
		+ "The code value is ignored.",
	)

	assert_int(handle.resolved_schedule()).override_failure_message(
		"a refused restatement must apply none of its bundle",
	).is_equal(PredictionHandle.Schedule.TICK)
	assert_int(handle.resolved_archetype()) \
			.is_equal(PredictionHandle.Archetype.NONE)


# The preset is a floor, not a cage: a verb after it overrides its part, and a
# key the scene declared keeps the scene's value without tripping the
# two-source error.
func test_the_preset_yields_to_declarations() -> void:
	var handle := _handle()
	handle.archetype(PredictionHandle.Archetype.SOLVER_BODY)
	handle.recovery().teleport_threshold(5.0)
	assert_float(handle.teleport_threshold).is_equal(5.0)

	var declared := _handle()
	declared.scene_declared = { &"tier": true }
	declared.schedule()._scene_tier(PredictionHandle.Schedule.TICK)
	declared.archetype(PredictionHandle.Archetype.SOLVER_BODY)
	assert_int(declared.resolved_schedule()).override_failure_message(
		"a scene-declared key must keep the scene's value under the preset",
	).is_equal(PredictionHandle.Schedule.TICK)
	assert_int(declared.missing_policy).override_failure_message(
		"the rest of the bundle still applies",
	).is_equal(PredictionHandle.MissingInput.REPEAT_LAST)


func test_the_schedule_verb_writes_every_part_it_names() -> void:
	var handle := _handle()

	handle.schedule().frame().buffer_depth(3) \
			.hold_repeat_last().resync_ceiling(42)

	assert_int(handle.resolved_schedule()) \
			.is_equal(PredictionHandle.Schedule.FRAME)
	assert_int(handle.replay_buffer_depth).is_equal(3)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.REPEAT_LAST)
	assert_int(handle.max_consume_lag_ticks).is_equal(42)


func test_an_omitted_key_keeps_what_was_there() -> void:
	var handle := _handle()
	handle.schedule().buffer_depth(5).resync_ceiling(7)

	handle.schedule().buffer_depth(9)

	assert_int(handle.replay_buffer_depth).is_equal(9)
	assert_int(handle.max_consume_lag_ticks).override_failure_message(
		"a config names the parts it decides, so an omitted part is one the "
		+ "caller left alone rather than one they reset",
	).is_equal(7)


func test_the_recovery_verb_writes_every_part_it_names() -> void:
	var handle := _handle()

	handle.recovery().policy(RecoveryPolicy.REBASE_REPLAY).epsilon(0.35) \
			.teleport_threshold(3.0).cooldown_ticks(9) \
			.projection(PredictionHandle.RestoreMode.EXTRAPOLATED)

	assert_float(handle.divergence_epsilon).is_equal(0.35)
	assert_float(handle.teleport_threshold).is_equal(3.0)
	assert_int(handle.collision_cooldown_ticks).is_equal(9)
	assert_int(handle.snap_restore) \
			.is_equal(PredictionHandle.RestoreMode.EXTRAPOLATED)
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_REPLAY)
	assert_int(handle.correction_mode).override_failure_message(
		"a policy names a strategy, so it must reach the mechanism that runs it",
	).is_equal(PredictionHandle.CorrectionMode.REPLAY)


func test_a_policy_nobody_declared_is_derived_from_the_body() -> void:
	var handle := _handle()

	# An entity that declared no policy still recovers, so the resolved policy
	# has to name what the correction mode actually does rather than nothing.
	handle.correction_mode = PredictionHandle.CorrectionMode.REPLAY
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_REPLAY)

	handle.correction_mode = PredictionHandle.CorrectionMode.SNAP
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_RECOVER)

	# A declared policy outranks the derivation, whatever the mode says.
	handle.recovery().policy(RecoveryPolicy.OBSERVE)
	assert_int(handle.resolved_recovery_policy()).override_failure_message(
		"a declared policy is the entity's own decision, so no body-type guess "
		+ "may override it",
	).is_equal(RecoveryPolicy.OBSERVE)


# A policy names a strategy and a mechanism carries it out, so every policy has
# to reach one. A policy that reached no mechanism would be accepted, reported,
# and silently inert, which is the failure this law exists to make loud.
func test_every_policy_reaches_a_mechanism() -> void:
	var mechanisms := {
		RecoveryPolicy.REBASE_REPLAY: PredictionHandle.CorrectionMode.REPLAY,
		RecoveryPolicy.REBASE_RECOVER: PredictionHandle.CorrectionMode.SNAP,
		RecoveryPolicy.DELAY_CLOSED: PredictionHandle.CorrectionMode.SNAP,
		RecoveryPolicy.OBSERVE: PredictionHandle.CorrectionMode.SNAP,
	}
	for policy: RecoveryPolicy in mechanisms:
		var handle := _handle()

		handle.recovery().policy(policy)

		assert_int(handle.resolved_recovery_policy()).override_failure_message(
			"a declared policy is reported as the one that was declared",
		).is_equal(policy)
		assert_int(handle.correction_mode).override_failure_message(
			"policy %d reached no mechanism, so declaring it decides nothing"
					% policy,
		).is_equal(mechanisms[policy])


func test_sensor_samples_compose_on_the_fluent_configurator() -> void:
	var handle := _handle()
	var ground := func() -> float: return 1.0
	var weather := func() -> float: return 2.0

	handle.sensors().sample(&"ground", ground).sample(&"weather", weather)

	var sensors: Dictionary = handle.island_config[&"sensors"]
	assert_float((sensors[&"ground"] as Callable).call()).is_equal(1.0)
	assert_float((sensors[&"weather"] as Callable).call()).is_equal(2.0)


func test_a_witness_declaration_keeps_its_callable() -> void:
	var handle := _handle()
	var contacts := func() -> Dictionary:
		return { colliders = [], sleeping = false }

	handle.witness().contacts(contacts)

	assert_bool(handle.witness_config[&"contacts"] is Callable).is_true()
	assert_dict((handle.witness_config[&"contacts"] as Callable).call()) \
			.is_equal({ colliders = [], sleeping = false })


func test_a_transport_declaration_keeps_its_corridor_callable() -> void:
	var handle := _handle()
	var sweep := func(
			_current: Dictionary,
			_proposed: Dictionary,
	) -> bool:
		return true

	handle.transport().corridor(sweep)

	var corridor := handle.transport_config[&"corridor"] as Callable
	assert_bool(corridor.is_valid()).is_true()
	assert_bool(corridor.call({ }, { })).is_true()


# One fact, one source. A key the scene's component declared away from its
# default is not restatable from code: the write is refused loudly, and the
# keys the scene left alone still apply.
func test_a_scene_declared_key_refuses_the_code_write() -> void:
	var handle := _handle()
	handle.scene_declared = { &"epsilon": true }

	await assert_error(
		func() -> void:
			handle.recovery().epsilon(0.5).teleport_threshold(5.0),
	).is_push_error(
		"PredictionHandle.recovery().epsilon: 'epsilon' is declared on the "
		+ "scene's PredictionComponent and configured from code. One source "
		+ "per fact: keep the scene value or the code call, not both. The "
		+ "code value is ignored.",
	)

	assert_float(handle.divergence_epsilon).override_failure_message(
		"the scene-declared key must keep the scene's value",
	).is_equal(0.01)
	assert_float(handle.teleport_threshold).override_failure_message(
		"a key the scene left alone still applies from code",
	).is_equal(5.0)


func test_the_island_declaration_is_stored_whole() -> void:
	var handle := _handle()

	handle.island().approximate()

	assert_bool(handle.island_config[&"approximate"]).is_true()
	assert_bool(handle.island_config[&"declared"]).override_failure_message(
		"island is the verb that opts into the fingerprint compare, so "
		+ "it sets the declared marker the domain label reads",
	).is_true()

	# A second declaration refines the first rather than replacing it, so the
	# island can be built up as the game learns its participants.
	handle.island().exact()
	assert_bool(handle.island_config[&"approximate"]).is_false()
	assert_bool(handle.island_config[&"declared"]).is_true()


func test_each_verb_leaves_the_other_two_alone() -> void:
	var handle := _handle()
	handle.schedule().buffer_depth(4)
	handle.recovery().epsilon(0.5)
	handle.island().approximate()

	assert_int(handle.replay_buffer_depth).is_equal(4)
	assert_float(handle.divergence_epsilon).is_equal(0.5)
	assert_bool(handle.island_config[&"approximate"]).is_true()


# Sensors and an epoch are environment attribution, folded into the digest that
# tells an environment divergence from a simulation one. Declaring them must not
# set the marker the domain label keys exact compare on, or a lone entity that
# only wants to attribute its divergences would be ratcheted into a fingerprint
# compare it cannot pass. island() is the one verb that declares.
func test_declaring_the_environment_does_not_declare_the_island() -> void:
	var handle := _handle()

	handle.sensors().sample(&"ground", func() -> bool: return true)
	handle.epoch(7)

	assert_bool(handle.island_config.has(&"sensors")).is_true()
	assert_int(handle.island_config[&"epoch"]).is_equal(7)
	assert_bool(handle.island_config.get(&"declared", false)) \
			.override_failure_message(
				"sensors and an epoch declare the environment, not the island, so "
				+ "they must leave the entity out of the fingerprint compare",
			).is_false()

	handle.island().exact()
	assert_bool(handle.island_config[&"declared"]).override_failure_message(
		"only island() declares the island",
	).is_true()


func test_interest_production_forces_an_approximate_budgeted_island() -> void:
	var handle := _handle()

	handle.island().from_interest().simulate_nearest(1)

	assert_bool(handle.island_config[&"approximate"]).is_true()
	assert_int(handle.island_config[&"producers"].size()).is_equal(1)
	assert_str(handle.island_config[&"promotion"][&"kind"]) \
			.is_equal("nearest")
	assert_int(handle.island_config[&"promotion"][&"count"]).is_equal(1)


func test_scene_island_default_yields_to_entity_declaration() -> void:
	var scene := MultiplayerScene.new()
	scene.prediction_island() \
			.approximate() \
			.from_interest() \
			.simulate_nearest(1)
	var handle := _handle()
	handle.island_config = scene._prediction_island_defaults()
	handle.sensors().sample(&"ground", func() -> bool: return true)

	assert_bool(handle.island_config[&"inherited"]).is_true()
	assert_str(handle.island_config[&"promotion"][&"kind"]) \
			.is_equal("nearest")

	handle.island().exact()
	assert_bool(handle.island_config.get(&"inherited", false)).is_false()
	assert_bool(handle.island_config.has(&"producers")).is_false()
	assert_bool(handle.island_config[&"exact_claim"]).is_true()
	assert_bool(handle.island_config[&"sensors"].has(&"ground")) \
			.override_failure_message(
				"overriding island scope must retain environment declarations",
			).is_true()


func test_explicit_members_can_be_promoted_observed_and_removed() -> void:
	var handle := _handle()
	var member := NetwEntity.new()

	handle.island().simulate(member)
	assert_array(handle.island_config[&"participants"]).contains([member])
	assert_int(handle.island_config[&"fidelity"][member]) \
			.is_equal(PredictionHandle.Fidelity.SIMULATED)

	handle.island().observe(member)
	assert_int(handle.island_config[&"fidelity"][member]) \
			.is_equal(PredictionHandle.Fidelity.PROXY)
	handle.island().remove(member)
	assert_array(handle.island_config[&"participants"]).is_empty()
	assert_bool(handle.island_config[&"fidelity"].has(member)).is_false()


func test_simulated_member_can_declare_a_command_predictor() -> void:
	var handle := _handle()
	var member := NetwEntity.new()
	var predictor := func(_entity: NetwEntity, _tick: int) -> Dictionary:
		return { &"motion": Vector2.RIGHT }

	handle.island().simulate(member, predictor)

	assert_that(handle.island_config[&"command_predictors"][member]) \
			.is_equal(predictor)


func test_produced_exact_and_joint_claims_are_refused_loudly() -> void:
	var produced := _handle()
	produced.island().from_interest()
	await assert_error(
		func() -> void:
			produced.island().exact(),
	).is_push_error(
		"PredictionHandle.island().exact: produced islands are always "
		+ "approximate.",
	)
	assert_bool(produced.island_config[&"approximate"]).is_true()

	var joint := _handle()
	await assert_error(
		func() -> void:
			joint.island().reconcile(PredictionHandle.Reconcile.JOINT),
	).is_push_error(
		"PredictionHandle.island().reconcile: JOINT stays reserved until "
		+ "contact is decisive, error-sensitive, and small-scope.",
	)
	assert_int(joint.island_config[&"reconcile"]) \
			.is_equal(PredictionHandle.Reconcile.INDEPENDENT)
