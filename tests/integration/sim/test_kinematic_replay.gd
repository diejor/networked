## CharacterBody2D replay coherence for ack reconciliation (architecture §9.2).
## Single-peer, real-physics, no networking.
##
## Reconciliation restores an authoritative snapshot and replays the local
## player's pending inputs by calling _network_tick N times inside ONE physics
## frame, with no physics step between calls. These tests prove that, for a
## single body against static geometry, that batched replay reproduces what N
## real physics frames produce, that wall slide and floor flags stay correct,
## and that move_and_slide integrates with the physics delta (not the contract's
## delta arg) so a tick equals a physics tick only at tickrate == physics rate.
## This is the permanent real-physics replay regression for bomber, not a
## throwaway. It runs single-peer because the closed-form lockstep rigs never step
## physics frames, which move_and_slide requires.
class_name TestKinematicReplay
extends NetwTestSuite


func _phys_dt() -> float:
	return 1.0 / float(Engine.physics_ticks_per_second)


func _make_arena(with_wall: bool = false) -> KinematicArena:
	var arena := KinematicArena.new(with_wall)
	add_child(arena)
	auto_free(arena)
	return arena


# Bodies collide with the world (mask 1) but are invisible to each other
# (layer 0), so a reference and a replay body can share one space at the same
# start without interacting. The await commits both the arena and the body
# shapes to the physics server before any body_test_motion query runs.
func _make_body(start: Vector2) -> KinematicSimBody:
	var body := KinematicSimBody.new()
	body.collision_layer = 0
	body.collision_mask = 1
	body.position = start
	add_child(body)
	auto_free(body)
	await get_tree().physics_frame
	return body


# Drives one _network_tick per real physics frame: the authoritative baseline.
func _drive_real(body: KinematicSimBody, motion: Vector2, n: int) -> void:
	var d := _phys_dt()
	for i in n:
		body._network_tick({&"motion": motion}, d, i, true)
		await get_tree().physics_frame


# Replays N inputs back-to-back inside one frame: the reconciliation path.
func _replay(body: KinematicSimBody, motion: Vector2, n: int) -> void:
	var d := _phys_dt()
	for i in n:
		body._network_tick({&"motion": motion}, d, i, false)
	await get_tree().physics_frame


func test_replay_in_one_frame_matches_n_frames_free_space() -> void:
	# Control: with no collisions, batched replay must land exactly where the
	# per-frame baseline lands, proving the rig and the no-step-between premise.
	_make_arena(false)
	var start := Vector2(0, 0)
	var ref := await _make_body(start)
	var rep := await _make_body(start)

	await _drive_real(ref, Vector2(1, 0), 6)
	await _replay(rep, Vector2(1, 0), 6)

	assert_bool(ref.position.is_equal_approx(rep.position)).is_true()
	assert_bool(ref.position.x > start.x + 1.0).is_true()


func test_replay_into_wall_matches_real_frames() -> void:
	# A diagonal drive into a wall: x must clamp at the wall while y slides past,
	# and the batched replay must reproduce the baseline's slide exactly.
	_make_arena(true)
	var start := Vector2(80, 0)
	var ref := await _make_body(start)
	var rep := await _make_body(start)

	await _drive_real(ref, Vector2(1, 1), 20)
	await _replay(rep, Vector2(1, 1), 20)

	assert_bool(ref.position.is_equal_approx(rep.position)).is_true()
	# Free travel would reach x = 80 + 20 * 1.5 = 110; the wall clamps it near 92.
	assert_bool(rep.position.x < 95.0).is_true()
	# y is unobstructed, so the body slid down the wall face.
	assert_bool(rep.position.y > start.y + 5.0).is_true()


func test_floor_flag_coherent_after_replay() -> void:
	# is_on_floor() reflects only the last move_and_slide. After replaying the
	# fall in one frame, the flag must match the baseline that fell over N frames.
	_make_arena(false)
	var start := Vector2(0, 150)
	var ref := await _make_body(start)
	var rep := await _make_body(start)

	await _drive_real(ref, Vector2(0, 1), 60)
	var ref_on_floor := ref.is_on_floor()
	await _replay(rep, Vector2(0, 1), 60)

	assert_bool(ref_on_floor).is_true()
	assert_bool(rep.is_on_floor()).is_true()
	assert_bool(ref.position.is_equal_approx(rep.position)).is_true()


func test_move_and_slide_uses_physics_delta_not_arg() -> void:
	# move_and_slide integrates with get_physics_process_delta_time(), ignoring
	# the _network_tick delta arg. So one replayed tick advances exactly one
	# physics tick of motion: replay equals wall-clock time only when
	# tickrate == physics rate, which is why §8.1 pins the bomber first cut to 60.
	_make_arena(false)
	var body := await _make_body(Vector2(0, 0))

	body._network_tick({&"motion": Vector2(1, 0)}, 999.0, 0, true)
	await get_tree().physics_frame

	var expected := KinematicSimBody.SPEED * _phys_dt()
	assert_bool(absf(body.position.x - expected) < 0.05).is_true()
	# The absurd 999 delta arg had no effect: motion is physics-delta scaled.
	assert_bool(body.position.x < 10.0).is_true()
