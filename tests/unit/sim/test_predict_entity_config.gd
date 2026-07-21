## Laws for the verbs that declare a predicted entity's behavior.
##
## The archetype, the schedule, the island, and the recovery are separate
## decisions, each with parts that only mean something together. Declaring one
## as a unit is what stops a caller from setting half of it and inheriting the
## rest by accident, so these laws hold each verb to copying its config in,
## rejecting what it does not define, and leaving the others alone. The
## archetype is the decision the others refine, so its preset applies whole
## and yields to any part declared on its own.
class_name TestPredictEntityConfig
extends NetwTestSuite

const PredictionHandle := NetwLagCompensationInterface.PredictionHandle
const RecoveryPolicy := PredictionHandle.RecoveryPolicy


func _handle() -> PredictionHandle:
	return PredictionHandle.new()


# --- the archetype presets ---


# An archetype is one decision that derives the schedule and recovery bundle,
# so declaring it writes every part the entity did not decide itself.
func test_the_solver_archetype_applies_its_bundle() -> void:
	var handle := _handle()

	handle.configure_prediction(PredictionHandle.Archetype.SOLVER_BODY)

	assert_int(handle.archetype) \
			.is_equal(PredictionHandle.Archetype.SOLVER_BODY)
	assert_int(handle.schedule).is_equal(PredictionHandle.Schedule.FRAME)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.REPEAT_LAST)
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_RECOVER)
	assert_int(handle.snap_restore) \
			.is_equal(PredictionHandle.RestoreMode.EXTRAPOLATED)
	assert_float(handle.teleport_threshold).is_equal(3.0)


func test_the_kinematic_archetype_applies_its_bundle() -> void:
	var handle := _handle()

	handle.configure_prediction(PredictionHandle.Archetype.KINEMATIC)

	assert_int(handle.schedule).is_equal(PredictionHandle.Schedule.TICK)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.STALL)
	assert_int(handle.resolved_recovery_policy()) \
			.is_equal(RecoveryPolicy.REBASE_REPLAY)


# The preset is a floor, not a cage: a verb after it overrides its part, and a
# key the scene declared keeps the scene's value without tripping the
# two-source error.
func test_the_preset_yields_to_declarations() -> void:
	var handle := _handle()
	handle.configure_prediction(PredictionHandle.Archetype.SOLVER_BODY)
	handle.configure_recovery({ teleport_threshold = 5.0 })
	assert_float(handle.teleport_threshold).is_equal(5.0)

	var declared := _handle()
	declared.scene_declared = { &"tier": true }
	declared.schedule = PredictionHandle.Schedule.TICK
	declared.configure_prediction(PredictionHandle.Archetype.SOLVER_BODY)
	assert_int(declared.schedule).override_failure_message(
		"a scene-declared key must keep the scene's value under the preset",
	).is_equal(PredictionHandle.Schedule.TICK)
	assert_int(declared.missing_policy).override_failure_message(
		"the rest of the bundle still applies",
	).is_equal(PredictionHandle.MissingInput.REPEAT_LAST)


func test_the_schedule_verb_writes_every_part_it_names() -> void:
	var handle := _handle()

	handle.configure_schedule({
		tier = PredictionHandle.Schedule.FRAME,
		buffer_depth = 3,
		hold = PredictionHandle.MissingInput.REPEAT_LAST,
		resync_ceiling = 42,
	})

	assert_int(handle.schedule).is_equal(PredictionHandle.Schedule.FRAME)
	assert_int(handle.replay_buffer_depth).is_equal(3)
	assert_int(handle.missing_policy) \
			.is_equal(PredictionHandle.MissingInput.REPEAT_LAST)
	assert_int(handle.max_consume_lag_ticks).is_equal(42)


func test_an_omitted_key_keeps_what_was_there() -> void:
	var handle := _handle()
	handle.configure_schedule({ buffer_depth = 5, resync_ceiling = 7 })

	handle.configure_schedule({ buffer_depth = 9 })

	assert_int(handle.replay_buffer_depth).is_equal(9)
	assert_int(handle.max_consume_lag_ticks).override_failure_message(
		"a config names the parts it decides, so an omitted part is one the "
		+ "caller left alone rather than one they reset",
	).is_equal(7)


func test_the_recovery_verb_writes_every_part_it_names() -> void:
	var handle := _handle()

	handle.configure_recovery({
		policy = RecoveryPolicy.REBASE_REPLAY,
		epsilon = 0.35,
		teleport_threshold = 3.0,
		cooldown_ticks = 9,
		projection = PredictionHandle.RestoreMode.EXTRAPOLATED,
	})

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
	handle.configure_recovery({ policy = RecoveryPolicy.OBSERVE })
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
		# A scope rollback re-runs its members, so it replays like a lone rebase.
		# What it adds is who else it re-runs, not how any one of them re-runs.
		RecoveryPolicy.ROLLBACK_SCOPE: PredictionHandle.CorrectionMode.REPLAY,
		RecoveryPolicy.DELAY_CLOSED: PredictionHandle.CorrectionMode.SNAP,
		RecoveryPolicy.OBSERVE: PredictionHandle.CorrectionMode.SNAP,
	}
	for policy: RecoveryPolicy in mechanisms:
		var handle := _handle()

		handle.configure_recovery({ policy = policy })

		assert_int(handle.resolved_recovery_policy()).override_failure_message(
			"a declared policy is reported as the one that was declared",
		).is_equal(policy)
		assert_int(handle.correction_mode).override_failure_message(
			"policy %d reached no mechanism, so declaring it decides nothing"
					% policy,
		).is_equal(mechanisms[policy])


func test_a_config_cannot_be_mutated_from_outside_after_it_lands() -> void:
	var handle := _handle()
	var sensors := { ground = func() -> float: return 1.0 }

	handle.configure_sensors(sensors)
	sensors.clear()

	assert_bool(
		(handle.island_config[&"sensors"] as Dictionary).has(&"ground"),
	).override_failure_message(
		"a config crosses as data, so the caller's dictionary is a "
		+ "value the handle copied and never a handle into it",
	).is_true()


# One fact, one source. A key the scene's component declared away from its
# default is not restatable from code: the write is refused loudly, and the
# keys the scene left alone still apply.
func test_a_scene_declared_key_refuses_the_code_write() -> void:
	var handle := _handle()
	handle.scene_declared = { &"epsilon": true }

	await assert_error(
		func() -> void:
			handle.configure_recovery({
				epsilon = 0.5,
				teleport_threshold = 5.0,
			}),
	).is_push_error(
		"PredictionHandle.configure_recovery: 'epsilon' is declared on the "
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

	handle.configure_island({ participants = [], approximate = true })

	assert_bool(handle.island_config[&"approximate"]).is_true()
	assert_bool(handle.island_config[&"declared"]).override_failure_message(
		"configure_island is the verb that opts into the fingerprint compare, so "
		+ "it sets the declared marker the domain label reads",
	).is_true()

	# A second declaration refines the first rather than replacing it, so the
	# island can be built up as the game learns its participants.
	handle.configure_island({ approximate = false })
	assert_bool(handle.island_config[&"approximate"]).is_false()
	assert_bool(handle.island_config[&"declared"]).is_true()


func test_each_verb_leaves_the_other_two_alone() -> void:
	var handle := _handle()
	handle.configure_schedule({ buffer_depth = 4 })
	handle.configure_recovery({ epsilon = 0.5 })
	handle.configure_island({ approximate = true })

	assert_int(handle.replay_buffer_depth).is_equal(4)
	assert_float(handle.divergence_epsilon).is_equal(0.5)
	assert_bool(handle.island_config[&"approximate"]).is_true()


# Sensors and an epoch are environment attribution, folded into the digest that
# tells an environment divergence from a simulation one. Declaring them must not
# set the marker the domain label keys exact compare on, or a lone entity that
# only wants to attribute its divergences would be ratcheted into a fingerprint
# compare it cannot pass. configure_island is the one verb that declares.
func test_declaring_the_environment_does_not_declare_the_island() -> void:
	var handle := _handle()

	handle.configure_sensors({ ground = Callable() })
	handle.configure_epoch(7)

	assert_bool(handle.island_config.has(&"sensors")).is_true()
	assert_int(handle.island_config[&"epoch"]).is_equal(7)
	assert_bool(handle.island_config.get(&"declared", false)) 			.override_failure_message(
				"sensors and an epoch declare the environment, not the island, so "
				+ "they must leave the entity out of the fingerprint compare",
			).is_false()

	handle.configure_island({ participants = [] })
	assert_bool(handle.island_config[&"declared"]).override_failure_message(
		"only configure_island declares the island",
	).is_true()
